package httpapi

import (
	"bytes"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"testing"

	"github.com/homeworldz/homeworldz/grid/internal/estate"
	"github.com/homeworldz/homeworldz/grid/internal/provisioning"
)

func TestRegionRuntimeEstateLifecycle(t *testing.T) {
	const regionID = "22222222-2222-4222-8222-222222222222"
	const owner = "33333333-3333-4333-8333-333333333333"
	path := filepath.Join(t.TempDir(), "regions.json")
	if err := os.WriteFile(path, []byte(`[
  {"id":"`+regionID+`","name":"Sandbox","ownerUserId":"`+owner+`","mapX":1001,"mapY":1000,"publicEndpoint":"https://sandbox.example/region","viewerPort":43002,"accessKey":"sandbox-key"}
]`), 0600); err != nil {
		t.Fatal(err)
	}
	registry, err := provisioning.Load(path)
	if err != nil {
		t.Fatal(err)
	}
	handler := New(checker{}, "test", Options{ServiceToken: "grid-secret", GridName: "Homeworldz Test",
		GridPublicURL: "https://grid.example", Regions: newMemoryRegionStore(), Provisioned: registry,
		Estates: estate.NewMemoryStore()})

	post := func(t *testing.T, target, body string) *httptest.ResponseRecorder {
		t.Helper()
		request := httptest.NewRequest(http.MethodPost, target, bytes.NewBufferString(body))
		request.Header.Set("Authorization", "Bearer sandbox-key")
		request.Header.Set("Content-Type", "application/json")
		response := httptest.NewRecorder()
		handler.ServeHTTP(response, request)
		return response
	}

	// Registration returns the region's default estate.
	registration := post(t, "/api/v1/region-runtime/Sandbox",
		`{"publicEndpoint":"http://127.0.0.1:42011","viewerPort":42012,"leaseSeconds":60}`)
	if registration.Code != http.StatusOK {
		t.Fatalf("registration status = %d: %s", registration.Code, registration.Body.String())
	}
	var registered ProvisionedRegionRuntimeResult
	if err := json.NewDecoder(registration.Body).Decode(&registered); err != nil {
		t.Fatal(err)
	}
	if registered.Estate == nil || registered.Estate.ID != estate.DefaultEstateID ||
		registered.Estate.OwnerUserID != owner || !registered.Estate.PublicAccess {
		t.Fatalf("unexpected registration estate: %#v", registered.Estate)
	}

	// Make the estate private.
	settings := post(t, "/api/v1/region-runtime/"+regionID+"/estate", `{"publicAccess":false,"name":"Locked"}`)
	if settings.Code != http.StatusOK {
		t.Fatalf("settings status = %d: %s", settings.Code, settings.Body.String())
	}
	var afterSettings EstateResult
	if err := json.NewDecoder(settings.Body).Decode(&afterSettings); err != nil {
		t.Fatal(err)
	}
	if afterSettings.Estate.PublicAccess || afterSettings.Estate.Name != "Locked" {
		t.Fatalf("settings not applied: %#v", afterSettings.Estate)
	}

	// Ban a user, then confirm GET reflects it.
	member := post(t, "/api/v1/region-runtime/"+regionID+"/estate/members",
		`{"memberId":"55555555-5555-4555-8555-555555555555","role":3,"present":true}`)
	if member.Code != http.StatusOK {
		t.Fatalf("member status = %d: %s", member.Code, member.Body.String())
	}
	getRequest := httptest.NewRequest(http.MethodGet, "/api/v1/region-runtime/"+regionID+"/estate", nil)
	getRequest.Header.Set("Authorization", "Bearer sandbox-key")
	getResponse := httptest.NewRecorder()
	handler.ServeHTTP(getResponse, getRequest)
	if getResponse.Code != http.StatusOK {
		t.Fatalf("get status = %d: %s", getResponse.Code, getResponse.Body.String())
	}
	var fetched EstateResult
	if err := json.NewDecoder(getResponse.Body).Decode(&fetched); err != nil {
		t.Fatal(err)
	}
	if len(fetched.Estate.Bans) != 1 || fetched.Estate.Bans[0] != "55555555-5555-4555-8555-555555555555" {
		t.Fatalf("ban not persisted: %#v", fetched.Estate.Bans)
	}
}
