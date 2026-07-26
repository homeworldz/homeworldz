package httpapi

import (
	"bytes"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/homeworldz/homeworldz/grid/internal/provisioning"
)

func newRegionProtocolHarness(t *testing.T) http.Handler {
	t.Helper()
	path := filepath.Join(t.TempDir(), "regions.json")
	if err := os.WriteFile(path, []byte(`[
  {"id":"22222222-2222-4222-8222-222222222222","name":"Sandbox","mapX":1001,"mapY":1000,"accessKey":"sandbox-key"}
]`), 0600); err != nil {
		t.Fatal(err)
	}
	registry, err := provisioning.Load(path)
	if err != nil {
		t.Fatal(err)
	}
	return New(checker{}, "test", Options{ServiceToken: "grid-secret", GridName: "Homeworldz Test",
		GridPublicURL: "https://grid.example", Regions: newMemoryRegionStore(), Provisioned: registry})
}

func regionRuntimeRequest(t *testing.T, handler http.Handler, method, path, body string) *httptest.ResponseRecorder {
	t.Helper()
	request := httptest.NewRequest(method, path, bytes.NewBufferString(body))
	request.Header.Set("Authorization", "Bearer sandbox-key")
	request.Header.Set("Content-Type", "application/json")
	response := httptest.NewRecorder()
	handler.ServeHTTP(response, request)
	return response
}

func TestRegionRegistrationReportsGridProtocolAndAcceptsMatch(t *testing.T) {
	handler := newRegionProtocolHarness(t)

	// A matching protocol registers, and the reply names the grid's current
	// protocol so a region can see an increment coming.
	response := regionRuntimeRequest(t, handler, http.MethodPost, "/api/v1/region-runtime/Sandbox",
		`{"publicEndpoint":"http://127.0.0.1:42011","viewerPort":42012,"leaseSeconds":60,"regionProtocol":1}`)
	if response.Code != http.StatusOK {
		t.Fatalf("matching registration status = %d: %s", response.Code, response.Body.String())
	}
	var registered ProvisionedRegionRuntimeResult
	if err := json.NewDecoder(response.Body).Decode(&registered); err != nil {
		t.Fatal(err)
	}
	if registered.RegionProtocol != 1 {
		t.Fatalf("registration reply protocol = %d, want 1", registered.RegionProtocol)
	}
}

func TestRegionRegistrationAcceptsUnreportedProtocol(t *testing.T) {
	handler := newRegionProtocolHarness(t)

	// A region that predates the handshake sends no protocol and is accepted;
	// enforcement begins as a no-op (docs/CLIENT2.md build order, step 2).
	response := regionRuntimeRequest(t, handler, http.MethodPost, "/api/v1/region-runtime/Sandbox",
		`{"publicEndpoint":"http://127.0.0.1:42011","viewerPort":42012,"leaseSeconds":60}`)
	if response.Code != http.StatusOK {
		t.Fatalf("unreported registration status = %d: %s", response.Code, response.Body.String())
	}
}

func TestRegionRegistrationRefusesMismatchNamingBothVersions(t *testing.T) {
	handler := newRegionProtocolHarness(t)

	response := regionRuntimeRequest(t, handler, http.MethodPost, "/api/v1/region-runtime/Sandbox",
		`{"publicEndpoint":"http://127.0.0.1:42011","viewerPort":42012,"leaseSeconds":60,"regionProtocol":2}`)
	if response.Code != http.StatusConflict {
		t.Fatalf("mismatch status = %d: %s", response.Code, response.Body.String())
	}
	var failure Error
	if err := json.NewDecoder(response.Body).Decode(&failure); err != nil {
		t.Fatal(err)
	}
	if failure.Code != "region_protocol_mismatch" {
		t.Fatalf("mismatch code = %q", failure.Code)
	}
	// The refusal is actionable: it names both versions.
	if !strings.Contains(failure.Message, "protocol 2") || !strings.Contains(failure.Message, "requires 1") {
		t.Fatalf("refusal does not name both versions: %q", failure.Message)
	}
}

func TestRegionLeaseRenewalEnforcesProtocol(t *testing.T) {
	handler := newRegionProtocolHarness(t)

	response := regionRuntimeRequest(t, handler, http.MethodPost, "/api/v1/region-runtime/Sandbox",
		`{"publicEndpoint":"http://127.0.0.1:42011","viewerPort":42012,"leaseSeconds":60,"regionProtocol":1}`)
	if response.Code != http.StatusOK {
		t.Fatalf("registration status = %d: %s", response.Code, response.Body.String())
	}

	// Renewal is where a protocol increment takes effect for regions that
	// registered before it, so the same check runs there.
	renewal := regionRuntimeRequest(t, handler, http.MethodPut,
		"/api/v1/region-runtime/22222222-2222-4222-8222-222222222222/lease",
		`{"leaseSeconds":120,"regionProtocol":7}`)
	if renewal.Code != http.StatusConflict {
		t.Fatalf("mismatched renewal status = %d: %s", renewal.Code, renewal.Body.String())
	}

	accepted := regionRuntimeRequest(t, handler, http.MethodPut,
		"/api/v1/region-runtime/22222222-2222-4222-8222-222222222222/lease",
		`{"leaseSeconds":120,"regionProtocol":1}`)
	if accepted.Code != http.StatusOK {
		t.Fatalf("matching renewal status = %d: %s", accepted.Code, accepted.Body.String())
	}
}
