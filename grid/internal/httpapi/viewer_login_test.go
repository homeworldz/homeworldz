package httpapi

import (
	"bytes"
	"context"
	"crypto/md5"
	"encoding/hex"
	"encoding/xml"
	"fmt"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
	"time"

	"github.com/homeworldz/homeworldz/grid/internal/regions"
)

func viewerRequest(first, last, password, start string) string {
	digest := md5.Sum([]byte(password))
	start = strings.ReplaceAll(start, "&", "&amp;")
	return `<?xml version="1.0"?><methodCall><methodName>login_to_simulator</methodName><params><param><value><struct>` +
		`<member><name>first</name><value><string>` + first + `</string></value></member>` +
		`<member><name>last</name><value><string>` + last + `</string></value></member>` +
		`<member><name>passwd</name><value><string>$1$` + hex.EncodeToString(digest[:]) + `</string></value></member>` +
		`<member><name>start</name><value><string>` + start + `</string></value></member>` +
		`</struct></value></param></params></methodCall>`
}

func viewerResponse(t *testing.T, handler http.Handler, body string) map[string]rpcValue {
	t.Helper()
	r := httptest.NewRequest(http.MethodPost, "/login", bytes.NewBufferString(body))
	r.Header.Set("Content-Type", "text/xml")
	w := httptest.NewRecorder()
	handler.ServeHTTP(w, r)
	if w.Code != http.StatusOK {
		t.Fatalf("status = %d: %s", w.Code, w.Body.String())
	}
	if !strings.HasPrefix(w.Header().Get("Content-Type"), "text/xml") {
		t.Fatalf("content type = %q", w.Header().Get("Content-Type"))
	}
	var response struct {
		Value rpcValue `xml:"params>param>value"`
	}
	if err := xml.NewDecoder(w.Body).Decode(&response); err != nil {
		t.Fatalf("decode response: %v\n%s", err, w.Body.String())
	}
	return response.Value.fields()
}

func TestViewerLoginResolvesNamedRegion(t *testing.T) {
	identities := newMemoryIdentityStore()
	if _, err := identities.CreateUser(context.Background(), "test.user", "development-password"); err != nil {
		t.Fatal(err)
	}
	regionStore := newMemoryRegionStore()
	_, _ = regionStore.Register(context.Background(), regions.Registration{Name: "Fallback", GridX: 1000, GridY: 1000,
		PublicEndpoint: "http://fallback.example:42001", LeaseDuration: time.Minute})
	target, _ := regionStore.Register(context.Background(), regions.Registration{Name: "Welcome", GridX: 1001, GridY: 1002,
		PublicEndpoint: "http://127.0.0.1:42001", ViewerPort: 43002, LeaseDuration: time.Minute})
	handler := New(checker{}, "test", Options{Identity: identities, Regions: regionStore})
	fields := viewerResponse(t, handler, viewerRequest("Test", "User", "development-password", "uri:Welcome&128&128&25"))
	if fields["login"].text() != "true" {
		t.Fatalf("login = %q, reason = %q, message = %q", fields["login"].text(), fields["reason"].text(), fields["message"].text())
	}
	if fields["agent_id"].text() == "" || fields["session_id"].text() == "" || fields["secure_session_id"].text() == "" {
		t.Fatalf("missing session identity: %#v", fields)
	}
	if fields["sim_ip"].text() != "127.0.0.1" || fields["sim_port"].text() != "43002" ||
		fields["region_x"].text() != "256256" || fields["region_y"].text() != "256512" {
		t.Fatalf("unexpected destination: %#v", fields)
	}
	wantSeed := strings.TrimRight(target.PublicEndpoint, "/") + "/caps/seed/" + fields["session_id"].text()
	if fields["seed_capability"].text() != wantSeed {
		t.Fatalf("seed = %q, want %q", fields["seed_capability"].text(), wantSeed)
	}
	session, err := identities.ValidateSession(context.Background(), fields["session_id"].text())
	if err != nil || session.ViewerCircuitCode == 0 || session.DestinationRegionID != target.ID ||
		fields["circuit_code"].text() != fmt.Sprint(session.ViewerCircuitCode) {
		t.Fatalf("viewer circuit was not persisted: session=%#v error=%v", session, err)
	}
}

func TestViewerLoginRejectsCredentialsAndMissingDestination(t *testing.T) {
	identities := newMemoryIdentityStore()
	_, _ = identities.CreateUser(context.Background(), "test.user", "development-password")
	regionStore := newMemoryRegionStore()
	_, _ = regionStore.Register(context.Background(), regions.Registration{Name: "Welcome", GridX: 1, GridY: 2,
		PublicEndpoint: "http://127.0.0.1:42001", LeaseDuration: time.Minute})
	handler := New(checker{}, "test", Options{Identity: identities, Regions: regionStore})
	fields := viewerResponse(t, handler, viewerRequest("Test", "User", "wrong-password", "home"))
	if fields["login"].text() != "false" || fields["reason"].text() != "key" {
		t.Fatalf("credential failure = %#v", fields)
	}
	fields = viewerResponse(t, handler, viewerRequest("Test", "User", "development-password", "uri:Missing&1&2&3"))
	if fields["login"].text() != "false" || fields["reason"].text() != "destination" {
		t.Fatalf("destination failure = %#v", fields)
	}
}
