package httpapi

import (
	"context"
	"net/http"
	"testing"
	"time"

	"github.com/homeworldz/homeworldz/grid/internal/presence"
)

type memoryPresenceStore struct {
	now    time.Time
	values map[string]presence.Presence
}

func newMemoryPresenceStore() *memoryPresenceStore {
	return &memoryPresenceStore{now: time.Date(2026, 7, 13, 12, 0, 0, 0, time.UTC), values: make(map[string]presence.Presence)}
}

func (s *memoryPresenceStore) Update(_ context.Context, userID, regionID string) (presence.Presence, error) {
	value := presence.Presence{UserID: userID, RegionID: regionID, LastSeenAt: s.now}
	s.values[userID] = value
	return value, nil
}

func (s *memoryPresenceStore) Clear(_ context.Context, userID string) error {
	if _, ok := s.values[userID]; !ok {
		return presence.ErrNotFound
	}
	delete(s.values, userID)
	return nil
}

func (s *memoryPresenceStore) Get(_ context.Context, userID string) (presence.Presence, error) {
	value, ok := s.values[userID]
	if !ok || s.now.Sub(value.LastSeenAt) >= presence.StaleAfter {
		return presence.Presence{}, presence.ErrNotFound
	}
	return value, nil
}

func (s *memoryPresenceStore) List(context.Context) ([]presence.Presence, error) {
	values := make([]presence.Presence, 0)
	for userID, value := range s.values {
		if s.now.Sub(value.LastSeenAt) >= presence.StaleAfter {
			delete(s.values, userID)
		} else {
			values = append(values, value)
		}
	}
	return values, nil
}

func TestPresenceLifecycleAndStaleCleanup(t *testing.T) {
	store := newMemoryPresenceStore()
	handler := New(checker{}, "test", Options{ServiceToken: "secret", Presence: store})
	userID := "20000000-0000-4000-8000-000000000001"
	regionID := "30000000-0000-4000-8000-000000000001"

	updated := requestRegion[presence.Presence](t, handler, http.MethodPut, "/api/v1/presence/"+userID,
		`{"regionId":"`+regionID+`"}`, http.StatusOK)
	if updated.RegionID != regionID {
		t.Fatalf("updated presence = %#v", updated)
	}
	requestRegion[presence.Presence](t, handler, http.MethodGet, "/api/v1/presence/"+userID, "", http.StatusOK)
	list := requestRegion[PresenceList](t, handler, http.MethodGet, "/api/v1/presence", "", http.StatusOK)
	if len(list.Presence) != 1 {
		t.Fatalf("presence list = %#v", list)
	}
	store.now = store.now.Add(presence.StaleAfter)
	requestRegion[Error](t, handler, http.MethodGet, "/api/v1/presence/"+userID, "", http.StatusNotFound)
	list = requestRegion[PresenceList](t, handler, http.MethodGet, "/api/v1/presence", "", http.StatusOK)
	if len(list.Presence) != 0 {
		t.Fatalf("stale presence was discoverable: %#v", list)
	}
	requestRegion[presence.Presence](t, handler, http.MethodPut, "/api/v1/presence/"+userID,
		`{"regionId":"`+regionID+`"}`, http.StatusOK)
	requestRegion[any](t, handler, http.MethodDelete, "/api/v1/presence/"+userID, "", http.StatusNoContent)
	requestRegion[Error](t, handler, http.MethodGet, "/api/v1/presence/"+userID, "", http.StatusNotFound)
}
