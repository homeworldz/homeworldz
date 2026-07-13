package httpapi

import (
	"context"
	"errors"
	"net/http"
	"net/http/httptest"
	"testing"
)

type checker struct{ err error }

func (c checker) PingContext(context.Context) error { return c.err }

func TestPingDoesNotRequireDatabase(t *testing.T) {
	r := httptest.NewRequest(http.MethodGet, "/ping", nil)
	w := httptest.NewRecorder()
	New(nil, "test").ServeHTTP(w, r)
	if w.Code != http.StatusOK {
		t.Fatalf("status = %d, want %d", w.Code, http.StatusOK)
	}
}

func TestReady(t *testing.T) {
	tests := []struct {
		name  string
		check ReadinessChecker
		want  int
	}{
		{"database ready", checker{}, http.StatusOK},
		{"database unavailable", checker{errors.New("down")}, http.StatusServiceUnavailable},
		{"database unconfigured", nil, http.StatusServiceUnavailable},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			r := httptest.NewRequest(http.MethodGet, "/ready", nil)
			w := httptest.NewRecorder()
			New(tt.check, "test").ServeHTTP(w, r)
			if w.Code != tt.want {
				t.Fatalf("status = %d, want %d", w.Code, tt.want)
			}
		})
	}
}

func TestVersion(t *testing.T) {
	r := httptest.NewRequest(http.MethodGet, "/version", nil)
	w := httptest.NewRecorder()
	New(checker{}, "1.2.3").ServeHTTP(w, r)
	if w.Code != http.StatusOK || w.Body.String() != "{\"apiVersion\":\"v1\",\"service\":\"grid\",\"version\":\"1.2.3\"}\n" {
		t.Fatalf("unexpected response: %d %s", w.Code, w.Body.String())
	}
}
