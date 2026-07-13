package httpapi

import (
	"context"
	"encoding/json"
	"errors"
	"net/http"
	"net/http/httptest"
	"testing"
)

type checker struct{ err error }

func (c checker) PingContext(context.Context) error { return c.err }

func decode[T any](t *testing.T, w *httptest.ResponseRecorder) T {
	t.Helper()
	var value T
	if err := json.NewDecoder(w.Body).Decode(&value); err != nil {
		t.Fatalf("decode response: %v", err)
	}
	return value
}

func TestPingDoesNotRequireDatabase(t *testing.T) {
	r := httptest.NewRequest(http.MethodGet, "/ping", nil)
	w := httptest.NewRecorder()
	New(nil, "test", Options{}).ServeHTTP(w, r)
	if w.Code != http.StatusOK {
		t.Fatalf("status = %d, want %d", w.Code, http.StatusOK)
	}
	if got := decode[Status](t, w); got != (Status{Status: "ok"}) {
		t.Fatalf("response = %#v, want status ok", got)
	}
}

func TestReady(t *testing.T) {
	tests := []struct {
		name  string
		check ReadinessChecker
		want  int
		code  string
	}{
		{"database ready", checker{}, http.StatusOK, ""},
		{"database unavailable", checker{errors.New("down")}, http.StatusServiceUnavailable, "database_unavailable"},
		{"database unconfigured", nil, http.StatusServiceUnavailable, "database_unconfigured"},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			r := httptest.NewRequest(http.MethodGet, "/ready", nil)
			w := httptest.NewRecorder()
			New(tt.check, "test", Options{}).ServeHTTP(w, r)
			if w.Code != tt.want {
				t.Fatalf("status = %d, want %d", w.Code, tt.want)
			}
			if tt.code == "" {
				if got := decode[Status](t, w); got.Status != "ready" {
					t.Fatalf("response = %#v, want status ready", got)
				}
			} else if got := decode[Error](t, w); got.Code != tt.code || got.Message == "" {
				t.Fatalf("response = %#v, want code %q and a message", got, tt.code)
			}
		})
	}
}

func TestVersion(t *testing.T) {
	r := httptest.NewRequest(http.MethodGet, "/version", nil)
	w := httptest.NewRecorder()
	New(checker{}, "1.2.3", Options{}).ServeHTTP(w, r)
	if w.Code != http.StatusOK {
		t.Fatalf("status = %d, want %d", w.Code, http.StatusOK)
	}
	want := Version{Service: "grid", Version: "1.2.3", APIVersion: APIVersion}
	if got := decode[Version](t, w); got != want {
		t.Fatalf("response = %#v, want %#v", got, want)
	}
}

func TestMethodNotAllowedUsesErrorModel(t *testing.T) {
	r := httptest.NewRequest(http.MethodPost, "/ping", nil)
	w := httptest.NewRecorder()
	New(nil, "test", Options{}).ServeHTTP(w, r)
	if w.Code != http.StatusMethodNotAllowed || w.Header().Get("Allow") != http.MethodGet {
		t.Fatalf("unexpected response status or Allow header: %d %q", w.Code, w.Header().Get("Allow"))
	}
	if got := decode[Error](t, w); got != (Error{Code: "method_not_allowed", Message: "only GET is supported"}) {
		t.Fatalf("unexpected error response: %#v", got)
	}
}
