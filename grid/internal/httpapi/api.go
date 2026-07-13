package httpapi

import (
	"context"
	"encoding/json"
	"net/http"
	"time"
)

const APIVersion = "v1"

type ReadinessChecker interface {
	PingContext(context.Context) error
}

type API struct {
	ready   ReadinessChecker
	version string
}

func New(ready ReadinessChecker, version string) http.Handler {
	a := &API{ready: ready, version: version}
	mux := http.NewServeMux()
	mux.HandleFunc("/ping", getOnly(a.ping))
	mux.HandleFunc("/ready", getOnly(a.readiness))
	mux.HandleFunc("/version", getOnly(a.buildVersion))
	return mux
}

func getOnly(next http.HandlerFunc) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		if r.Method != http.MethodGet {
			w.Header().Set("Allow", http.MethodGet)
			writeJSON(w, http.StatusMethodNotAllowed, map[string]string{
				"code": "method_not_allowed", "message": "only GET is supported",
			})
			return
		}
		next(w, r)
	}
}

func (a *API) ping(w http.ResponseWriter, _ *http.Request) {
	writeJSON(w, http.StatusOK, map[string]string{"status": "ok"})
}

func (a *API) readiness(w http.ResponseWriter, r *http.Request) {
	if a.ready == nil {
		writeJSON(w, http.StatusServiceUnavailable, map[string]string{
			"code": "database_unconfigured", "message": "database is not configured",
		})
		return
	}

	ctx, cancel := context.WithTimeout(r.Context(), 2*time.Second)
	defer cancel()
	if err := a.ready.PingContext(ctx); err != nil {
		writeJSON(w, http.StatusServiceUnavailable, map[string]string{
			"code": "database_unavailable", "message": "database is unavailable",
		})
		return
	}
	writeJSON(w, http.StatusOK, map[string]string{"status": "ready"})
}

func (a *API) buildVersion(w http.ResponseWriter, _ *http.Request) {
	writeJSON(w, http.StatusOK, map[string]string{
		"service": "grid", "version": a.version, "apiVersion": APIVersion,
	})
}

func writeJSON(w http.ResponseWriter, status int, value any) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(status)
	_ = json.NewEncoder(w).Encode(value)
}
