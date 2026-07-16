package httpapi

import (
	"context"
	"errors"
	"net/http"
	"testing"
	"time"

	"github.com/homeworldz/homeworldz/grid/internal/regions"
	"github.com/homeworldz/homeworldz/grid/internal/transit"
)

type memoryTransitStore struct {
	now    time.Time
	values map[string]transit.Transit
}

func newMemoryTransitStore(now time.Time) *memoryTransitStore {
	return &memoryTransitStore{now: now, values: make(map[string]transit.Transit)}
}

func (s *memoryTransitStore) Prepare(_ context.Context, input transit.Prepare) (transit.Transit, error) {
	if value, ok := s.values[input.ID]; ok {
		return value, nil
	}
	for _, value := range s.values {
		if value.AgentID == input.AgentID && (value.State == transit.Prepared || value.State == transit.Accepted) {
			return transit.Transit{}, transit.ErrConflict
		}
	}
	value := transit.Transit{
		ID: input.ID, Generation: 1, AgentID: input.AgentID, SessionID: input.SessionID,
		SourceRegionID: input.SourceRegionID, DestinationRegionID: input.DestinationRegionID,
		Position: input.Position, LookAt: input.LookAt, Flying: input.Flying,
		State: transit.Prepared, ExpiresAt: s.now.Add(input.Lifetime), CreatedAt: s.now, UpdatedAt: s.now,
	}
	s.values[value.ID] = value
	return value, nil
}

func (s *memoryTransitStore) Get(_ context.Context, id string) (transit.Transit, error) {
	value, ok := s.values[id]
	if !ok {
		return transit.Transit{}, transit.ErrNotFound
	}
	return value, nil
}

func (s *memoryTransitStore) Accept(_ context.Context, id, regionID string) (transit.Transit, error) {
	return s.change(id, regionID, transit.Prepared, transit.Accepted, false, "")
}

func (s *memoryTransitStore) Activate(_ context.Context, id, regionID string) (transit.Transit, error) {
	return s.change(id, regionID, transit.Accepted, transit.Activated, false, "")
}

func (s *memoryTransitStore) Rollback(_ context.Context, id, regionID, reason string) (transit.Transit, error) {
	return s.change(id, regionID, "", transit.RolledBack, true, reason)
}

func (s *memoryTransitStore) change(id, regionID string, from, to transit.State, rollback bool, reason string) (transit.Transit, error) {
	value, ok := s.values[id]
	if !ok {
		return transit.Transit{}, transit.ErrNotFound
	}
	actor := value.DestinationRegionID == regionID || (rollback && value.SourceRegionID == regionID)
	if !actor || (!rollback && value.State != from) ||
		(rollback && value.State != transit.Prepared && value.State != transit.Accepted) {
		if actor && value.State == to {
			return value, nil
		}
		return transit.Transit{}, transit.ErrInvalidTransition
	}
	value.State, value.RollbackReason, value.UpdatedAt = to, reason, s.now
	s.values[id] = value
	return value, nil
}

func TestAvatarTransitHTTPStateMachine(t *testing.T) {
	identities := newMemoryIdentityStore()
	user, err := identities.CreateUser(context.Background(), "transit.user", "password")
	if err != nil {
		t.Fatal(err)
	}
	session, err := identities.CreateSession(context.Background(), "transit.user", "password", time.Minute)
	if err != nil {
		t.Fatal(err)
	}
	const source = "11111111-1111-4111-8111-111111111111"
	const destination = "22222222-2222-4222-8222-222222222222"
	if err := identities.AssignViewerDestination(context.Background(), session.ID, 1234, source); err != nil {
		t.Fatal(err)
	}
	regionStore := newMemoryRegionStore()
	for _, item := range []struct {
		id, name string
		x        int
	}{{source, "Welcome", 1000}, {destination, "Sandbox", 1001}} {
		_, err := regionStore.RegisterProvisioned(context.Background(), item.id, regions.Registration{
			Name: item.name, GridX: item.x, GridY: 1000, PublicEndpoint: "http://region.example",
			ViewerPort: 42002, LeaseDuration: time.Minute,
		})
		if err != nil {
			t.Fatal(err)
		}
	}
	store := newMemoryTransitStore(identities.now)
	handler := New(checker{}, "test", Options{
		ServiceToken: "secret", Identity: identities, Regions: regionStore, Transits: store,
	})
	const transitID = "33333333-3333-4333-8333-333333333333"
	body := `{"id":"` + transitID + `","agentId":"` + user.ID + `","sessionId":"` + session.ID +
		`","sourceRegionId":"` + source + `","destinationRegionId":"` + destination +
		`","position":{"x":128,"y":64,"z":30},"lookAt":{"x":1,"y":0,"z":0},"flying":true}`
	prepared := requestRegion[transit.Transit](t, handler, http.MethodPost, "/api/v1/transits", body, http.StatusOK)
	if prepared.State != transit.Prepared || prepared.Generation != 1 {
		t.Fatalf("prepared transit = %#v", prepared)
	}
	accepted := requestRegion[transit.Transit](t, handler, http.MethodPost,
		"/api/v1/transits/"+transitID+"/accept", `{"regionId":"`+destination+`"}`, http.StatusOK)
	if accepted.State != transit.Accepted {
		t.Fatalf("accepted transit = %#v", accepted)
	}
	activated := requestRegion[transit.Transit](t, handler, http.MethodPost,
		"/api/v1/transits/"+transitID+"/activate", `{"regionId":"`+destination+`"}`, http.StatusOK)
	if activated.State != transit.Activated {
		t.Fatalf("activated transit = %#v", activated)
	}
	found := requestRegion[transit.Transit](t, handler, http.MethodGet,
		"/api/v1/transits/"+transitID, "", http.StatusOK)
	if found.State != transit.Activated {
		t.Fatalf("stored transit = %#v", found)
	}

	_, err = store.Rollback(context.Background(), transitID, source, "late rollback")
	if !errors.Is(err, transit.ErrInvalidTransition) {
		t.Fatalf("activated rollback error = %v", err)
	}
}
