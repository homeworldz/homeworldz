package httpapi

import (
	"context"
	"net/http"
	"os"
	"path/filepath"
	"testing"

	"github.com/homeworldz/homeworldz/grid/internal/locations"
	"github.com/homeworldz/homeworldz/grid/internal/provisioning"
)

type memoryWritableLocationStore struct{ value locations.Location }

func (s *memoryWritableLocationStore) Get(_ context.Context, userID string) (locations.Location, error) {
	if s.value.UserID != userID {
		return locations.Location{}, locations.ErrNotFound
	}
	return s.value, nil
}

func (s *memoryWritableLocationStore) Update(
	_ context.Context, value locations.Location,
) (locations.Location, error) {
	s.value = value
	return value, nil
}

func TestUpdateLastLocation(t *testing.T) {
	store := &memoryWritableLocationStore{}
	const userID = "20000000-0000-4000-8000-000000000001"
	const regionID = "30000000-0000-4000-8000-000000000001"
	path := filepath.Join(t.TempDir(), "regions.json")
	if err := os.WriteFile(path, []byte(`[{"id":"`+regionID+`","name":"Large","mapX":1000,"mapY":1000,"size":2,"accessKey":"large-key"}]`), 0600); err != nil {
		t.Fatal(err)
	}
	provisioned, err := provisioning.Load(path)
	if err != nil {
		t.Fatal(err)
	}
	handler := New(checker{}, "test", Options{ServiceToken: "secret", Locations: store,
		Provisioned: provisioned})
	value := requestRegion[locations.Location](t, handler, http.MethodPut,
		"/api/v1/locations/"+userID,
		`{"regionId":"`+regionID+`","position":{"x":400,"y":384,"z":35},`+
			`"lookAt":{"x":-0.7,"y":-0.7,"z":0},"flying":true}`,
		http.StatusOK)
	if value.RegionID != regionID || value.Position != [3]float32{400, 384, 35} ||
		value.LookAt != [3]float32{-0.7, -0.7, 0} || !value.Flying {
		t.Fatalf("updated location = %#v", value)
	}
	requestRegion[Error](t, handler, http.MethodPut, "/api/v1/locations/"+userID,
		`{"regionId":"`+regionID+`","position":{"x":600,"y":128,"z":35},`+
			`"lookAt":{"x":1,"y":0,"z":0},"flying":false}`,
		http.StatusBadRequest)
}
