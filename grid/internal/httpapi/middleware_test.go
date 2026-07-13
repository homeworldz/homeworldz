package httpapi

import (
	"bytes"
	"encoding/json"
	"log/slog"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
)

func TestRequestIDAndStructuredLog(t *testing.T) {
	var output bytes.Buffer
	logger := slog.New(slog.NewJSONHandler(&output, nil))
	r := httptest.NewRequest(http.MethodGet, "/ping", nil)
	r.Header.Set(RequestIDHeader, "caller-request-123")
	w := httptest.NewRecorder()

	New(nil, "test", Options{Logger: logger}).ServeHTTP(w, r)

	if got := w.Header().Get(RequestIDHeader); got != "caller-request-123" {
		t.Fatalf("request ID = %q, want caller-request-123", got)
	}
	var entry map[string]any
	if err := json.Unmarshal(output.Bytes(), &entry); err != nil {
		t.Fatalf("decode structured log: %v\n%s", err, output.String())
	}
	for key, want := range map[string]any{
		"msg":       "http request",
		"requestId": "caller-request-123",
		"method":    http.MethodGet,
		"path":      "/ping",
		"status":    float64(http.StatusOK),
	} {
		if got := entry[key]; got != want {
			t.Errorf("log field %s = %#v, want %#v", key, got, want)
		}
	}
}

func TestUnsafeRequestIDIsReplaced(t *testing.T) {
	r := httptest.NewRequest(http.MethodGet, "/ping", nil)
	r.Header.Set(RequestIDHeader, "unsafe\nvalue")
	w := httptest.NewRecorder()

	New(nil, "test", Options{}).ServeHTTP(w, r)

	got := w.Header().Get(RequestIDHeader)
	if got == "unsafe\nvalue" || !validRequestID(got) {
		t.Fatalf("generated request ID = %q", got)
	}
}

func TestInternalAPIAuthentication(t *testing.T) {
	tests := []struct {
		name          string
		configured    string
		authorization string
		wantStatus    int
		wantCode      string
	}{
		{"unconfigured", "", "", http.StatusServiceUnavailable, "service_auth_unconfigured"},
		{"missing", "secret", "", http.StatusUnauthorized, "unauthorized"},
		{"wrong scheme", "secret", "Basic secret", http.StatusUnauthorized, "unauthorized"},
		{"wrong token", "secret", "Bearer incorrect", http.StatusUnauthorized, "unauthorized"},
		{"valid", "secret", "Bearer secret", http.StatusNotFound, "not_found"},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			r := httptest.NewRequest(http.MethodGet, "/api/v1/example", nil)
			if tt.authorization != "" {
				r.Header.Set("Authorization", tt.authorization)
			}
			w := httptest.NewRecorder()
			New(nil, "test", Options{ServiceToken: tt.configured}).ServeHTTP(w, r)
			if w.Code != tt.wantStatus {
				t.Fatalf("status = %d, want %d", w.Code, tt.wantStatus)
			}
			if got := decode[Error](t, w); got.Code != tt.wantCode {
				t.Fatalf("error code = %q, want %q", got.Code, tt.wantCode)
			}
			if tt.wantStatus == http.StatusUnauthorized &&
				!strings.EqualFold(w.Header().Get("WWW-Authenticate"), "Bearer") {
				t.Errorf("WWW-Authenticate = %q, want Bearer", w.Header().Get("WWW-Authenticate"))
			}
		})
	}
}

func TestAuthenticationTokenIsNotLogged(t *testing.T) {
	var output bytes.Buffer
	logger := slog.New(slog.NewJSONHandler(&output, nil))
	r := httptest.NewRequest(http.MethodGet, "/api/v1/example", nil)
	r.Header.Set("Authorization", "Bearer super-secret-token")
	w := httptest.NewRecorder()

	New(nil, "test", Options{
		ServiceToken: "super-secret-token",
		Logger:       logger,
	}).ServeHTTP(w, r)

	if strings.Contains(output.String(), "super-secret-token") {
		t.Fatalf("structured log contains authentication token: %s", output.String())
	}
}
