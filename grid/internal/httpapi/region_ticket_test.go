package httpapi

import (
	"bytes"
	"context"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"testing"
	"time"

	"github.com/homeworldz/server/grid/internal/provisioning"
	"github.com/homeworldz/server/grid/internal/webtoken"
)

const sandboxRegionID = "22222222-2222-4222-8222-222222222222"

// newTicketHarness builds a region-runtime handler with a ticket verifier and
// one live session, and returns the handler, the region-ticket signer, and
// the session it should validate against.
func newTicketHarness(t *testing.T) (http.Handler, *webtoken.Signer, string, string) {
	t.Helper()
	path := filepath.Join(t.TempDir(), "regions.json")
	if err := os.WriteFile(path, []byte(`[
  {"id":"`+sandboxRegionID+`","name":"Sandbox","mapX":1001,"mapY":1000,"accessKey":"sandbox-key"}
]`), 0600); err != nil {
		t.Fatal(err)
	}
	registry, err := provisioning.Load(path)
	if err != nil {
		t.Fatal(err)
	}
	verifier, err := webtoken.NewSigner([]byte("0123456789abcdef0123456789abcdef"),
		"https://issuer.test", webtoken.RegionTicketAudience, 5*time.Minute)
	if err != nil {
		t.Fatal(err)
	}
	identities := newMemoryIdentityStore()
	user, err := identities.CreateUser(context.Background(), "ticket.user", "development-password")
	if err != nil {
		t.Fatal(err)
	}
	session, err := identities.CreateSession(context.Background(), "ticket.user", "development-password", time.Hour)
	if err != nil {
		t.Fatal(err)
	}
	handler := New(checker{}, "test", Options{ServiceToken: "grid-secret",
		Regions: newMemoryRegionStore(), Provisioned: registry,
		Identity: identities, TicketVerifier: verifier})
	return handler, verifier, user.ID, session.ID
}

func postTicket(t *testing.T, handler http.Handler, body string) *httptest.ResponseRecorder {
	t.Helper()
	request := httptest.NewRequest(http.MethodPost,
		"/api/v1/region-runtime/"+sandboxRegionID+"/validate-ticket", bytes.NewBufferString(body))
	request.Header.Set("Authorization", "Bearer sandbox-key")
	request.Header.Set("Content-Type", "application/json")
	response := httptest.NewRecorder()
	handler.ServeHTTP(response, request)
	return response
}

func TestValidateRegionTicketResolvesIdentity(t *testing.T) {
	handler, verifier, userID, sessionID := newTicketHarness(t)
	ticket, _, err := verifier.SignRegionTicket(time.Now(), userID, "ticket.user", "Ticket User",
		time.Date(2026, 1, 1, 0, 0, 0, 0, time.UTC), "", 1, sandboxRegionID, sessionID, []float64{200, 210, 24})
	if err != nil {
		t.Fatal(err)
	}
	encoded, _ := json.Marshal(ValidateRegionTicketRequest{Token: ticket})
	response := postTicket(t, handler, string(encoded))
	if response.Code != http.StatusOK {
		t.Fatalf("status = %d: %s", response.Code, response.Body.String())
	}
	var result ValidateRegionTicketResult
	if err := json.NewDecoder(response.Body).Decode(&result); err != nil {
		t.Fatal(err)
	}
	if result.UserID != userID || result.Userid != "ticket.user" ||
		result.DisplayName != "Ticket User" || result.SessionID != sessionID || result.ExpiresAt.IsZero() {
		t.Fatalf("unexpected identity: %+v", result)
	}
	// The arrival position world entry resolved rides the ticket, so the
	// region spawns there rather than trusting the client.
	if len(result.Position) != 3 || result.Position[0] != 200 ||
		result.Position[1] != 210 || result.Position[2] != 24 {
		t.Fatalf("unexpected arrival position: %v", result.Position)
	}
}

func TestValidateRegionTicketRefusesWrongRegion(t *testing.T) {
	handler, verifier, userID, sessionID := newTicketHarness(t)
	// A genuine ticket for a different region: the signature is valid, the
	// region binding is not.
	ticket, _, err := verifier.SignRegionTicket(time.Now(), userID, "ticket.user", "Ticket User",
		time.Date(2026, 1, 1, 0, 0, 0, 0, time.UTC), "", 1,
		"33333333-3333-4333-8333-333333333333", sessionID, nil)
	if err != nil {
		t.Fatal(err)
	}
	encoded, _ := json.Marshal(ValidateRegionTicketRequest{Token: ticket})
	if response := postTicket(t, handler, string(encoded)); response.Code != http.StatusUnauthorized {
		t.Fatalf("wrong-region status = %d: %s", response.Code, response.Body.String())
	}
}

func TestValidateRegionTicketRefusesUnknownSessionAndGarbage(t *testing.T) {
	handler, verifier, userID, _ := newTicketHarness(t)
	ticket, _, err := verifier.SignRegionTicket(time.Now(), userID, "ticket.user", "Ticket User",
		time.Date(2026, 1, 1, 0, 0, 0, 0, time.UTC), "", 1,
		sandboxRegionID, "40000000-0000-4000-8000-000000000000", nil)
	if err != nil {
		t.Fatal(err)
	}
	encoded, _ := json.Marshal(ValidateRegionTicketRequest{Token: ticket})
	if response := postTicket(t, handler, string(encoded)); response.Code != http.StatusUnauthorized {
		t.Fatalf("unknown-session status = %d: %s", response.Code, response.Body.String())
	}
	if response := postTicket(t, handler, `{"token":"not-a-ticket"}`); response.Code != http.StatusUnauthorized {
		t.Fatalf("garbage status = %d: %s", response.Code, response.Body.String())
	}
}

func TestValidateRegionTicketRefusesAccountToken(t *testing.T) {
	handler, _, userID, _ := newTicketHarness(t)
	// An account-audience token with a forged region claim shape cannot pass:
	// the verifier's audience check refuses it before any claim is read.
	accountSigner, err := webtoken.NewSigner([]byte("0123456789abcdef0123456789abcdef"),
		"https://issuer.test", "https://audience.test", time.Hour)
	if err != nil {
		t.Fatal(err)
	}
	token, _, err := accountSigner.Sign(time.Now(), userID, "ticket.user", "Ticket User",
		time.Date(2026, 1, 1, 0, 0, 0, 0, time.UTC), "", 1)
	if err != nil {
		t.Fatal(err)
	}
	encoded, _ := json.Marshal(ValidateRegionTicketRequest{Token: token})
	if response := postTicket(t, handler, string(encoded)); response.Code != http.StatusUnauthorized {
		t.Fatalf("account-token status = %d: %s", response.Code, response.Body.String())
	}
}

func TestRegistrationCarriesSessionEndpoint(t *testing.T) {
	handler, _, _, _ := newTicketHarness(t)
	response := httptest.NewRecorder()
	request := httptest.NewRequest(http.MethodPost, "/api/v1/region-runtime/Sandbox",
		bytes.NewBufferString(`{"publicEndpoint":"http://127.0.0.1:42011","viewerPort":42012,"leaseSeconds":60,"regionProtocol":1,"sessionEndpoint":"wss://sandbox.example/session"}`))
	request.Header.Set("Authorization", "Bearer sandbox-key")
	request.Header.Set("Content-Type", "application/json")
	handler.ServeHTTP(response, request)
	if response.Code != http.StatusOK {
		t.Fatalf("registration status = %d: %s", response.Code, response.Body.String())
	}
	var registered ProvisionedRegionRuntimeResult
	if err := json.NewDecoder(response.Body).Decode(&registered); err != nil {
		t.Fatal(err)
	}
	if registered.SessionEndpoint != "wss://sandbox.example/session" {
		t.Fatalf("session endpoint = %q", registered.SessionEndpoint)
	}

	// A non-WebSocket URL is refused by name.
	rejected := httptest.NewRecorder()
	bad := httptest.NewRequest(http.MethodPost, "/api/v1/region-runtime/Sandbox",
		bytes.NewBufferString(`{"publicEndpoint":"http://127.0.0.1:42011","viewerPort":42012,"leaseSeconds":60,"sessionEndpoint":"http://sandbox.example/session"}`))
	bad.Header.Set("Authorization", "Bearer sandbox-key")
	bad.Header.Set("Content-Type", "application/json")
	handler.ServeHTTP(rejected, bad)
	if rejected.Code != http.StatusBadRequest {
		t.Fatalf("invalid endpoint status = %d: %s", rejected.Code, rejected.Body.String())
	}
}
