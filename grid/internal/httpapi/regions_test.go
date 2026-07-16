package httpapi

import (
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"sort"
	"testing"
	"time"

	"github.com/homeworldz/homeworldz/grid/internal/provisioning"
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
	if input.ViewerPort == 0 {
		input.ViewerPort = 42002
	}
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
		PublicEndpoint: input.PublicEndpoint, ViewerPort: input.ViewerPort,
		LeaseExpiresAt: s.now.Add(input.LeaseDuration),
	}
	s.regions[region.ID] = region
	return region, nil
}

func (s *memoryRegionStore) RegisterProvisioned(_ context.Context, id string, input regions.Registration) (regions.Region, error) {
	for otherID, region := range s.regions {
		if otherID != id && region.LeaseExpiresAt.After(s.now) && region.GridX == input.GridX && region.GridY == input.GridY {
			return regions.Region{}, regions.ErrConflict
		}
	}
	region := regions.Region{ID: id, Name: input.Name, GridX: input.GridX, GridY: input.GridY,
		PublicEndpoint: input.PublicEndpoint, ViewerPort: input.ViewerPort, LeaseExpiresAt: s.now.Add(input.LeaseDuration)}
	s.regions[id] = region
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

func TestRegionDiscoveryIsReadOnly(t *testing.T) {
	store := newMemoryRegionStore()
	handler := New(checker{}, "test", Options{ServiceToken: "secret", Regions: store})
	created, err := store.RegisterProvisioned(context.Background(),
		"11111111-1111-4111-8111-111111111111", regions.Registration{
			Name: "Test Region", GridX: 1000, GridY: 1001,
			PublicEndpoint: "http://region.example:42001", ViewerPort: 42002,
			LeaseDuration: 60 * time.Second,
		})
	if err != nil {
		t.Fatal(err)
	}

	list := requestRegion[RegionList](t, handler, http.MethodGet, "/api/v1/regions", "", http.StatusOK)
	if len(list.Regions) != 1 || list.Regions[0].ID != created.ID {
		t.Fatalf("unexpected discovery response: %#v", list)
	}

	requestRegion[regions.Region](t, handler, http.MethodGet,
		"/api/v1/regions/"+created.ID, "", http.StatusOK)
	methodError := requestRegion[Error](t, handler, http.MethodPost, "/api/v1/regions", `{}`, http.StatusMethodNotAllowed)
	if methodError.Code != "method_not_allowed" {
		t.Fatalf("mutation response = %#v", methodError)
	}
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

	errorResponse := requestRegion[Error](t, handler, http.MethodPost, "/api/v1/regions", `{}`,
		http.StatusMethodNotAllowed)
	if errorResponse.Code != "method_not_allowed" {
		t.Fatalf("validation code = %q", errorResponse.Code)
	}
}

func TestProvisionedRegionRegistrationUsesPerRegionCredentials(t *testing.T) {
	path := filepath.Join(t.TempDir(), "regions.json")
	if err := os.WriteFile(path, []byte(`[
  {"id":"11111111-1111-4111-8111-111111111111","name":"Welcome","mapX":1000,"mapY":1000,"accessKey":"welcome-key"},
  {"id":"22222222-2222-4222-8222-222222222222","name":"Sandbox","mapX":1001,"mapY":1000,"accessKey":"sandbox-key"}
]`), 0600); err != nil {
		t.Fatal(err)
	}
	registry, err := provisioning.Load(path)
	if err != nil {
		t.Fatal(err)
	}
	store := newMemoryRegionStore()
	handler := New(checker{}, "test", Options{ServiceToken: "grid-secret", Regions: store, Provisioned: registry})

	request := httptest.NewRequest(http.MethodPost,
		"/api/v1/region-runtime/22222222-2222-4222-8222-222222222222",
		bytes.NewBufferString(`{"publicEndpoint":"http://127.0.0.1:42011","viewerPort":42012,"leaseSeconds":60}`))
	request.Header.Set("Authorization", "Bearer sandbox-key")
	request.Header.Set("Content-Type", "application/json")
	response := httptest.NewRecorder()
	handler.ServeHTTP(response, request)
	if response.Code != http.StatusOK {
		t.Fatalf("registration status = %d: %s", response.Code, response.Body.String())
	}
	var registered regions.Region
	if err := json.NewDecoder(response.Body).Decode(&registered); err != nil {
		t.Fatal(err)
	}
	if registered.ID != "22222222-2222-4222-8222-222222222222" || registered.Name != "Sandbox" ||
		registered.GridX != 1001 || registered.GridY != 1000 || registered.ViewerPort != 42012 {
		t.Fatalf("unexpected registered region: %#v", registered)
	}

	request = httptest.NewRequest(http.MethodPut,
		"/api/v1/region-runtime/22222222-2222-4222-8222-222222222222/lease",
		bytes.NewBufferString(`{"leaseSeconds":120}`))
	request.Header.Set("Authorization", "Bearer sandbox-key")
	request.Header.Set("Content-Type", "application/json")
	response = httptest.NewRecorder()
	handler.ServeHTTP(response, request)
	if response.Code != http.StatusOK {
		t.Fatalf("renewal status = %d: %s", response.Code, response.Body.String())
	}

	request = httptest.NewRequest(http.MethodPost,
		"/api/v1/region-runtime/11111111-1111-4111-8111-111111111111",
		bytes.NewBufferString(`{"publicEndpoint":"http://127.0.0.1:42001","leaseSeconds":60}`))
	request.Header.Set("Authorization", "Bearer wrong-key")
	response = httptest.NewRecorder()
	handler.ServeHTTP(response, request)
	if response.Code != http.StatusUnauthorized {
		t.Fatalf("wrong key status = %d, want 401", response.Code)
	}

	request = httptest.NewRequest(http.MethodDelete,
		"/api/v1/region-runtime/22222222-2222-4222-8222-222222222222", nil)
	request.Header.Set("Authorization", "Bearer sandbox-key")
	response = httptest.NewRecorder()
	handler.ServeHTTP(response, request)
	if response.Code != http.StatusNoContent {
		t.Fatalf("deregistration status = %d: %s", response.Code, response.Body.String())
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
