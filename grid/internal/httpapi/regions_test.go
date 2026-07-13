package httpapi

import (
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"net/http"
	"net/http/httptest"
	"sort"
	"testing"
	"time"

	"github.com/homeworldz/homeworldz/grid/internal/regions"
)

type memoryRegionStore struct {
	now     time.Time
	nextID  int
	regions map[string]regions.Region
}

func newMemoryRegionStore() *memoryRegionStore {
	return &memoryRegionStore{
		now:     time.Date(2026, 7, 13, 12, 0, 0, 0, time.UTC),
		regions: make(map[string]regions.Region),
	}
}

func (s *memoryRegionStore) Register(_ context.Context, input regions.Registration) (regions.Region, error) {
	for id, region := range s.regions {
		if !region.LeaseExpiresAt.After(s.now) {
			delete(s.regions, id)
			continue
		}
		if region.GridX == input.GridX && region.GridY == input.GridY {
			return regions.Region{}, regions.ErrConflict
		}
	}
	s.nextID++
	region := regions.Region{
		ID:   fmt.Sprintf("00000000-0000-4000-8000-%012d", s.nextID),
		Name: input.Name, GridX: input.GridX, GridY: input.GridY,
		PublicEndpoint: input.PublicEndpoint, LeaseExpiresAt: s.now.Add(input.LeaseDuration),
	}
	s.regions[region.ID] = region
	return region, nil
}

func (s *memoryRegionStore) Renew(_ context.Context, id string, duration time.Duration) (regions.Region, error) {
	region, ok := s.regions[id]
	if !ok || !region.LeaseExpiresAt.After(s.now) {
		return regions.Region{}, regions.ErrNotFound
	}
	region.LeaseExpiresAt = s.now.Add(duration)
	s.regions[id] = region
	return region, nil
}

func (s *memoryRegionStore) Deregister(_ context.Context, id string) error {
	if _, ok := s.regions[id]; !ok {
		return regions.ErrNotFound
	}
	delete(s.regions, id)
	return nil
}

func (s *memoryRegionStore) Get(_ context.Context, id string) (regions.Region, error) {
	region, ok := s.regions[id]
	if !ok || !region.LeaseExpiresAt.After(s.now) {
		return regions.Region{}, regions.ErrNotFound
	}
	return region, nil
}

func (s *memoryRegionStore) List(context.Context) ([]regions.Region, error) {
	items := make([]regions.Region, 0)
	for _, region := range s.regions {
		if region.LeaseExpiresAt.After(s.now) {
			items = append(items, region)
		}
	}
	sort.Slice(items, func(i, j int) bool { return items[i].ID < items[j].ID })
	return items, nil
}

func TestRegionRegistrationLifecycle(t *testing.T) {
	store := newMemoryRegionStore()
	handler := New(checker{}, "test", Options{ServiceToken: "secret", Regions: store})

	created := requestRegion[regions.Region](t, handler, http.MethodPost, "/api/v1/regions", `{
        "name":"Test Region","gridX":1000,"gridY":1001,
        "publicEndpoint":"http://region.example:42001","leaseSeconds":60
    }`, http.StatusCreated)
	if created.Name != "Test Region" || created.GridX != 1000 || created.GridY != 1001 {
		t.Fatalf("unexpected registered region: %#v", created)
	}

	conflict := requestRegion[Error](t, handler, http.MethodPost, "/api/v1/regions", `{
        "name":"Conflict","gridX":1000,"gridY":1001,
        "publicEndpoint":"http://other.example:42001","leaseSeconds":60
    }`, http.StatusConflict)
	if conflict.Code != "region_coordinates_in_use" {
		t.Fatalf("conflict code = %q", conflict.Code)
	}

	list := requestRegion[RegionList](t, handler, http.MethodGet, "/api/v1/regions", "", http.StatusOK)
	if len(list.Regions) != 1 || list.Regions[0].ID != created.ID {
		t.Fatalf("unexpected discovery response: %#v", list)
	}

	renewed := requestRegion[regions.Region](t, handler, http.MethodPut,
		"/api/v1/regions/"+created.ID+"/lease", `{"leaseSeconds":120}`, http.StatusOK)
	if !renewed.LeaseExpiresAt.Equal(store.now.Add(120 * time.Second)) {
		t.Fatalf("renewed lease expires at %s", renewed.LeaseExpiresAt)
	}

	store.now = store.now.Add(121 * time.Second)
	notFound := requestRegion[Error](t, handler, http.MethodGet,
		"/api/v1/regions/"+created.ID, "", http.StatusNotFound)
	if notFound.Code != "region_not_found" {
		t.Fatalf("expired lookup code = %q", notFound.Code)
	}

	replacement := requestRegion[regions.Region](t, handler, http.MethodPost, "/api/v1/regions", `{
        "name":"Replacement","gridX":1000,"gridY":1001,
        "publicEndpoint":"https://replacement.example/region","leaseSeconds":30
    }`, http.StatusCreated)
	requestRegion[any](t, handler, http.MethodDelete, "/api/v1/regions/"+replacement.ID, "", http.StatusNoContent)
	requestRegion[Error](t, handler, http.MethodGet, "/api/v1/regions/"+replacement.ID, "", http.StatusNotFound)
}

func TestRegionRegistrationValidationAndAuthentication(t *testing.T) {
	store := newMemoryRegionStore()
	handler := New(checker{}, "test", Options{ServiceToken: "secret", Regions: store})

	r := httptest.NewRequest(http.MethodGet, "/api/v1/regions", nil)
	w := httptest.NewRecorder()
	handler.ServeHTTP(w, r)
	if w.Code != http.StatusUnauthorized {
		t.Fatalf("unauthenticated status = %d, want 401", w.Code)
	}

	errorResponse := requestRegion[Error](t, handler, http.MethodPost, "/api/v1/regions",
		`{"name":"","gridX":-1,"gridY":0,"publicEndpoint":"not-a-url","leaseSeconds":5}`,
		http.StatusBadRequest)
	if errorResponse.Code != "invalid_lease" {
		t.Fatalf("validation code = %q, want invalid_lease", errorResponse.Code)
	}
}

func requestRegion[T any](t *testing.T, handler http.Handler, method, path, body string, wantStatus int) T {
	t.Helper()
	r := httptest.NewRequest(method, path, bytes.NewBufferString(body))
	r.Header.Set("Authorization", "Bearer secret")
	if body != "" {
		r.Header.Set("Content-Type", "application/json")
	}
	w := httptest.NewRecorder()
	handler.ServeHTTP(w, r)
	if w.Code != wantStatus {
		t.Fatalf("%s %s status = %d, want %d: %s", method, path, w.Code, wantStatus, w.Body.String())
	}
	var value T
	if wantStatus == http.StatusNoContent {
		return value
	}
	if err := json.NewDecoder(w.Body).Decode(&value); err != nil {
		t.Fatalf("decode %s %s response: %v", method, path, err)
	}
	return value
}
