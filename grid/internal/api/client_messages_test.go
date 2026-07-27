package api

import (
	"context"
	"encoding/json"
	"fmt"
	"net/http"
	"net/http/httptest"
	"strings"
	"sync"
	"testing"
	"time"

	"github.com/homeworldz/server/grid/internal/messages"
)

// memoryMessageStore satisfies messages.Store for the delivery tests. The
// mutex mirrors the concurrency the real store gets from the database: the
// HTTP handler and the channel's serve goroutine call in concurrently.
type memoryMessageStore struct {
	mu     sync.Mutex
	stored []messages.Message
	nextID int
}

func (s *memoryMessageStore) Create(_ context.Context, fromUserID, toUserID, text string) (messages.Message, error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.nextID++
	message := messages.Message{
		ID:         fmt.Sprintf("50000000-0000-4000-8000-%012d", s.nextID),
		FromUserID: fromUserID, ToUserID: toUserID, Message: text,
		SentAt: time.Date(2026, 7, 27, 0, 0, 0, 0, time.UTC).Add(time.Duration(s.nextID) * time.Second),
	}
	s.stored = append(s.stored, message)
	return message, nil
}

func (s *memoryMessageStore) Undelivered(_ context.Context, toUserID string, limit int) ([]messages.Message, error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	result := make([]messages.Message, 0)
	for _, message := range s.stored {
		if message.ToUserID == toUserID && message.DeliveredAt == nil && len(result) < limit {
			result = append(result, message)
		}
	}
	return result, nil
}

func (s *memoryMessageStore) MarkDelivered(_ context.Context, ids []string) error {
	s.mu.Lock()
	defer s.mu.Unlock()
	now := time.Now()
	for _, id := range ids {
		for index := range s.stored {
			if s.stored[index].ID == id {
				s.stored[index].DeliveredAt = &now
			}
		}
	}
	return nil
}

func (s *memoryMessageStore) undeliveredCount() int {
	s.mu.Lock()
	defer s.mu.Unlock()
	count := 0
	for _, message := range s.stored {
		if message.DeliveredAt == nil {
			count++
		}
	}
	return count
}

func newMessagingHarness(t *testing.T) (*worldEntryHarness, string, *memoryMessageStore) {
	t.Helper()
	store := &memoryMessageStore{}
	harness, adminToken := newDeliveryHarness(t, func(options *Options) { options.Messages = store })
	return harness, adminToken, store
}

func postMessage(t *testing.T, handler http.Handler, bearer, body string) *httptest.ResponseRecorder {
	t.Helper()
	request := httptest.NewRequest(http.MethodPost, "/v1/client/messages", strings.NewReader(body))
	request.Header.Set("Authorization", "Bearer "+bearer)
	request.Header.Set("Content-Type", "application/json")
	response := httptest.NewRecorder()
	handler.ServeHTTP(response, request)
	return response
}

func TestMessageDeliversLiveToConnectedRecipient(t *testing.T) {
	harness, adminToken, store := newMessagingHarness(t)
	conn, done := dialChannel(t, harness)
	defer done()
	authenticate(t, conn, harness.token)

	body := fmt.Sprintf(`{"to":%q,"message":"hello from the admin"}`, testUserID)
	response := postMessage(t, harness.handler, adminToken, body)
	if response.Code != http.StatusOK {
		t.Fatalf("send status = %d: %s", response.Code, response.Body.String())
	}
	var result MessageResult
	if err := json.NewDecoder(response.Body).Decode(&result); err != nil {
		t.Fatal(err)
	}
	if result.Delivered != 1 || result.ID == "" {
		t.Fatalf("unexpected result: %+v", result)
	}

	notification := readEnvelope(t, conn)
	var payload channelMessagePayload
	if err := json.Unmarshal(notification.Payload, &payload); err != nil {
		t.Fatal(err)
	}
	if notification.Type != "notification" || payload.Kind != "instant_message" ||
		payload.ID != result.ID || payload.From.Userid != "admin.user" ||
		payload.Message != "hello from the admin" {
		t.Fatalf("unexpected notification: %+v %+v", notification, payload)
	}
	// Handed to a connection counts as delivered: nothing waits in backlog.
	if store.undeliveredCount() != 0 {
		t.Fatalf("undelivered = %d, want 0", store.undeliveredCount())
	}
}

func TestMessageToOfflineUserWaitsInBacklogUntilConnect(t *testing.T) {
	harness, adminToken, store := newMessagingHarness(t)

	first := postMessage(t, harness.handler, adminToken,
		fmt.Sprintf(`{"to":%q,"message":"first while away"}`, testUserID))
	second := postMessage(t, harness.handler, adminToken,
		fmt.Sprintf(`{"to":%q,"message":"second while away"}`, testUserID))
	for _, response := range []*httptest.ResponseRecorder{first, second} {
		var result MessageResult
		if err := json.NewDecoder(response.Body).Decode(&result); err != nil {
			t.Fatal(err)
		}
		if result.Delivered != 0 {
			t.Fatalf("offline delivery = %d, want 0", result.Delivered)
		}
	}
	if store.undeliveredCount() != 2 {
		t.Fatalf("undelivered = %d, want 2", store.undeliveredCount())
	}

	// The next channel connection replays the backlog in sent order, before
	// anything else.
	conn, done := dialChannel(t, harness)
	defer done()
	authenticate(t, conn, harness.token)
	replayFirst := readEnvelope(t, conn)
	replaySecond := readEnvelope(t, conn)
	var payloadFirst, payloadSecond channelMessagePayload
	if err := json.Unmarshal(replayFirst.Payload, &payloadFirst); err != nil {
		t.Fatal(err)
	}
	if err := json.Unmarshal(replaySecond.Payload, &payloadSecond); err != nil {
		t.Fatal(err)
	}
	if payloadFirst.Message != "first while away" || payloadSecond.Message != "second while away" {
		t.Fatalf("backlog order wrong: %q then %q", payloadFirst.Message, payloadSecond.Message)
	}
	if store.undeliveredCount() != 0 {
		t.Fatalf("undelivered after replay = %d, want 0", store.undeliveredCount())
	}
}

// A backlog deeper than one batch drains fully on a single connection: 100
// is a batch size, not a total.
func TestMessageBacklogDeeperThanOneBatchDrains(t *testing.T) {
	harness, adminToken, store := newMessagingHarness(t)
	for index := 0; index < 150; index++ {
		response := postMessage(t, harness.handler, adminToken,
			fmt.Sprintf(`{"to":%q,"message":"backlog %03d"}`, testUserID, index))
		if response.Code != http.StatusOK {
			t.Fatalf("send %d status = %d", index, response.Code)
		}
	}
	if store.undeliveredCount() != 150 {
		t.Fatalf("undelivered = %d, want 150", store.undeliveredCount())
	}

	conn, done := dialChannel(t, harness)
	defer done()
	authenticate(t, conn, harness.token)
	for index := 0; index < 150; index++ {
		envelope := readEnvelope(t, conn)
		var payload channelMessagePayload
		if err := json.Unmarshal(envelope.Payload, &payload); err != nil {
			t.Fatal(err)
		}
		if payload.Message != fmt.Sprintf("backlog %03d", index) {
			t.Fatalf("replay %d out of order: %q", index, payload.Message)
		}
	}
	if store.undeliveredCount() != 0 {
		t.Fatalf("undelivered after replay = %d, want 0", store.undeliveredCount())
	}
}

func TestMessageValidation(t *testing.T) {
	harness, adminToken, _ := newMessagingHarness(t)

	if response := postMessage(t, harness.handler, adminToken,
		fmt.Sprintf(`{"to":%q,"message":"   "}`, testUserID)); response.Code != http.StatusBadRequest {
		t.Fatalf("blank message status = %d", response.Code)
	}
	if response := postMessage(t, harness.handler, adminToken,
		`{"to":"99999999-9999-4999-8999-999999999999","message":"hi"}`); response.Code != http.StatusNotFound {
		t.Fatalf("unknown recipient status = %d", response.Code)
	}
	// The limit counts characters, not bytes: 700 emoji are 2,800 bytes and
	// well inside the documented 2,048 characters.
	emoji := strings.Repeat("\U0001F600", 700)
	if response := postMessage(t, harness.handler, adminToken,
		fmt.Sprintf(`{"to":%q,"message":%q}`, testUserID, emoji)); response.Code != http.StatusOK {
		t.Fatalf("emoji message status = %d: %s", response.Code, response.Body.String())
	}
	over := strings.Repeat("\U0001F600", 2049)
	if response := postMessage(t, harness.handler, adminToken,
		fmt.Sprintf(`{"to":%q,"message":%q}`, testUserID, over)); response.Code != http.StatusBadRequest {
		t.Fatalf("over-limit message status = %d", response.Code)
	}
	request := httptest.NewRequest(http.MethodPost, "/v1/client/messages",
		strings.NewReader(`{"to":"x","message":"hi"}`))
	request.Header.Set("Content-Type", "application/json")
	response := httptest.NewRecorder()
	harness.handler.ServeHTTP(response, request)
	if response.Code != http.StatusUnauthorized {
		t.Fatalf("unauthenticated status = %d", response.Code)
	}
}
