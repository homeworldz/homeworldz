package httpapi

import (
	"context"
	"encoding/json"
	"log/slog"
	"net/http"
	"time"
)

type ReadinessChecker interface {
	PingContext(context.Context) error
}

type API struct {
	ready   ReadinessChecker
	version string
}

type Options struct {
	ServiceToken string
	Logger       *slog.Logger
}

func New(ready ReadinessChecker, version string, options Options) http.Handler {
	a := &API{ready: ready, version: version}
	mux := http.NewServeMux()
	mux.HandleFunc("/ping", getOnly(a.ping))
	mux.HandleFunc("/ready", getOnly(a.readiness))
	mux.HandleFunc("/version", getOnly(a.buildVersion))
	mux.HandleFunc("/", a.notFound)
	return withRequestID(withRequestLogging(
		authenticateInternal(mux, options.ServiceToken), options.Logger,
	))
}

func getOnly(next http.HandlerFunc) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		if r.Method != http.MethodGet {
			w.Header().Set("Allow", http.MethodGet)
			writeJSON(w, http.StatusMethodNotAllowed, Error{
				Code: "method_not_allowed", Message: "only GET is supported",
			})
			return
		}
		next(w, r)
	}
}

func (a *API) ping(w http.ResponseWriter, _ *http.Request) {
	writeJSON(w, http.StatusOK, Status{Status: "ok"})
}

func (a *API) readiness(w http.ResponseWriter, r *http.Request) {
	if a.ready == nil {
		writeJSON(w, http.StatusServiceUnavailable, Error{
			Code: "database_unconfigured", Message: "database is not configured",
		})
		return
	}

	ctx, cancel := context.WithTimeout(r.Context(), 2*time.Second)
	defer cancel()
	if err := a.ready.PingContext(ctx); err != nil {
		writeJSON(w, http.StatusServiceUnavailable, Error{
			Code: "database_unavailable", Message: "database is unavailable",
		})
		return
	}
	writeJSON(w, http.StatusOK, Status{Status: "ready"})
}

func (a *API) buildVersion(w http.ResponseWriter, _ *http.Request) {
	writeJSON(w, http.StatusOK, Version{
		Service: "grid", Version: a.version, APIVersion: APIVersion,
	})
}

func (a *API) notFound(w http.ResponseWriter, _ *http.Request) {
	writeJSON(w, http.StatusNotFound, Error{Code: "not_found", Message: "route not found"})
}

func writeJSON(w http.ResponseWriter, status int, value any) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(status)
	_ = json.NewEncoder(w).Encode(value)
}
