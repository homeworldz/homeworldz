package api

import (
	"encoding/json"
	"fmt"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
	"time"

	"github.com/homeworldz/server/grid/internal/webaccount"
)

const testAdminID = "55555555-5555-4555-8555-555555555555"

// newDeliveryHarness is the world-entry harness plus an admin account able to
// send notices, with a bearer token for each.
func newDeliveryHarness(t *testing.T) (*worldEntryHarness, string) {
	t.Helper()
	admin := webaccount.Account{ID: testAdminID, Userid: "admin.user", DisplayName: "Admin User",
		RezDate: time.Date(2026, 1, 1, 0, 0, 0, 0, time.UTC), Privileges: webaccount.PrivUsers, AuthVersion: 1}
	harness := newWorldEntryHarness(t, func(options *Options) {
		store := options.Accounts.(*memoryAccountStore)
		store.accounts[testAdminID] = admin
	})
	adminToken, _, err := harness.signer.Sign(time.Now(), admin.ID, admin.Userid, admin.DisplayName,
		admin.RezDate, admin.Privileges, admin.AuthVersion)
	if err != nil {
		t.Fatalf("sign admin token: %v", err)
	}
	return harness, adminToken
}

func postNotice(t *testing.T, handler http.Handler, targetID, bearer, body string) *httptest.ResponseRecorder {
	t.Helper()
	request := httptest.NewRequest(http.MethodPost,
		fmt.Sprintf("/v1/admin/users/%s/notice", targetID), strings.NewReader(body))
	request.Header.Set("Authorization", "Bearer "+bearer)
	request.Header.Set("Content-Type", "application/json")
	response := httptest.NewRecorder()
	handler.ServeHTTP(response, request)
	return response
}

func TestChannelDeliversAdminNotice(t *testing.T) {
	harness, adminToken := newDeliveryHarness(t)
	conn, done := dialChannel(t, harness)
	defer done()
	authenticate(t, conn, harness.token)

	response := postNotice(t, harness.handler, testUserID, adminToken, `{"message":"maintenance in ten minutes"}`)
	if response.Code != http.StatusOK {
		t.Fatalf("notice status = %d: %s", response.Code, response.Body.String())
	}
	var result NoticeResult
	if err := json.NewDecoder(response.Body).Decode(&result); err != nil {
		t.Fatal(err)
	}
	if result.Delivered != 1 {
		t.Fatalf("delivered = %d, want 1", result.Delivered)
	}

	// The connected channel receives the server-initiated envelope.
	notification := readEnvelope(t, conn)
	if notification.Type != "notification" || notification.Version != 1 {
		t.Fatalf("unexpected envelope: %+v", notification)
	}
	var payload channelNotificationPayload
	if err := json.Unmarshal(notification.Payload, &payload); err != nil {
		t.Fatal(err)
	}
	if payload.Kind != "system_notice" || payload.Message != "maintenance in ten minutes" || payload.SentAt.IsZero() {
		t.Fatalf("unexpected payload: %+v", payload)
	}

	// The channel still answers the client afterwards: delivery and the read
	// loop share one writer without interfering.
	sendEnvelope(t, conn, channelEnvelope{Type: "ping", Version: 1, CorrelationID: "after-notice"})
	pong := readEnvelope(t, conn)
	if pong.Type != "pong" || pong.CorrelationID != "after-notice" {
		t.Fatalf("unexpected pong after notice: %+v", pong)
	}
}

func TestChannelNoticeReachesEveryConnection(t *testing.T) {
	harness, adminToken := newDeliveryHarness(t)
	first, doneFirst := dialChannel(t, harness)
	defer doneFirst()
	authenticate(t, first, harness.token)
	second, doneSecond := dialChannel(t, harness)
	defer doneSecond()
	authenticate(t, second, harness.token)

	response := postNotice(t, harness.handler, testUserID, adminToken, `{"message":"to both"}`)
	var result NoticeResult
	if err := json.NewDecoder(response.Body).Decode(&result); err != nil {
		t.Fatal(err)
	}
	if result.Delivered != 2 {
		t.Fatalf("delivered = %d, want 2", result.Delivered)
	}
	if envelope := readEnvelope(t, first); envelope.Type != "notification" {
		t.Fatalf("first connection got %+v", envelope)
	}
	if envelope := readEnvelope(t, second); envelope.Type != "notification" {
		t.Fatalf("second connection got %+v", envelope)
	}
}

func TestChannelNoticeToOfflineUserDeliversZero(t *testing.T) {
	harness, adminToken := newDeliveryHarness(t)

	response := postNotice(t, harness.handler, testUserID, adminToken, `{"message":"anyone home"}`)
	if response.Code != http.StatusOK {
		t.Fatalf("notice status = %d: %s", response.Code, response.Body.String())
	}
	var result NoticeResult
	if err := json.NewDecoder(response.Body).Decode(&result); err != nil {
		t.Fatal(err)
	}
	if result.Delivered != 0 {
		t.Fatalf("delivered = %d, want 0", result.Delivered)
	}
}

func TestChannelNoticeRequiresUsersPrivilege(t *testing.T) {
	harness, _ := newDeliveryHarness(t)

	// The unprivileged account token cannot send notices.
	response := postNotice(t, harness.handler, testAdminID, harness.token, `{"message":"nope"}`)
	if response.Code != http.StatusForbidden {
		t.Fatalf("status = %d, want 403", response.Code)
	}
}

func TestChannelNoticeValidatesTargetAndMessage(t *testing.T) {
	harness, adminToken := newDeliveryHarness(t)

	missing := postNotice(t, harness.handler, "99999999-9999-4999-8999-999999999999", adminToken, `{"message":"hello"}`)
	if missing.Code != http.StatusNotFound {
		t.Fatalf("missing-user status = %d, want 404", missing.Code)
	}
	empty := postNotice(t, harness.handler, testUserID, adminToken, `{"message":"   "}`)
	if empty.Code != http.StatusBadRequest {
		t.Fatalf("empty-message status = %d, want 400", empty.Code)
	}
}

// After a connection closes, its registration is gone: delivery reports the
// remaining connections only.
func TestChannelDeregistersOnDisconnect(t *testing.T) {
	harness, adminToken := newDeliveryHarness(t)
	conn, done := dialChannel(t, harness)
	authenticate(t, conn, harness.token)
	done()

	deadline := time.Now().Add(5 * time.Second)
	for {
		response := postNotice(t, harness.handler, testUserID, adminToken, `{"message":"still there?"}`)
		var result NoticeResult
		if err := json.NewDecoder(response.Body).Decode(&result); err != nil {
			t.Fatal(err)
		}
		if result.Delivered == 0 {
			return
		}
		if time.Now().After(deadline) {
			t.Fatalf("connection never deregistered; delivered = %d", result.Delivered)
		}
		time.Sleep(10 * time.Millisecond)
	}
}
