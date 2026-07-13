package httpapi

import (
	"context"
	"crypto/rand"
	"crypto/subtle"
	"encoding/hex"
	"fmt"
	"log/slog"
	"net/http"
	"strings"
	"sync/atomic"
	"time"
)

const RequestIDHeader = "X-Request-ID"

type requestIDKey struct{}

var fallbackRequestID atomic.Uint64

func withRequestID(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		requestID := r.Header.Get(RequestIDHeader)
		if !validRequestID(requestID) {
			requestID = newRequestID()
		}
		w.Header().Set(RequestIDHeader, requestID)
		ctx := context.WithValue(r.Context(), requestIDKey{}, requestID)
		next.ServeHTTP(w, r.WithContext(ctx))
	})
}

func withRequestLogging(next http.Handler, logger *slog.Logger) http.Handler {
	if logger == nil {
		return next
	}
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		started := time.Now()
		capture := &responseCapture{ResponseWriter: w}
		next.ServeHTTP(capture, r)
		if !capture.wrote {
			capture.status = http.StatusOK
		}
		logger.Info("http request",
			"requestId", requestIDFromContext(r.Context()),
			"method", r.Method,
			"path", r.URL.Path,
			"status", capture.status,
			"durationMs", time.Since(started).Milliseconds(),
		)
	})
}

func authenticateInternal(next http.Handler, serviceToken string) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if !strings.HasPrefix(r.URL.Path, "/api/") {
			next.ServeHTTP(w, r)
			return
		}
		if serviceToken == "" {
			writeJSON(w, http.StatusServiceUnavailable, Error{
				Code: "service_auth_unconfigured", Message: "service authentication is not configured",
			})
			return
		}
		if !validBearerToken(r.Header.Get("Authorization"), serviceToken) {
			w.Header().Set("WWW-Authenticate", "Bearer")
			writeJSON(w, http.StatusUnauthorized, Error{
				Code: "unauthorized", Message: "a valid service token is required",
			})
			return
		}
		next.ServeHTTP(w, r)
	})
}

func validBearerToken(authorization, serviceToken string) bool {
	scheme, token, found := strings.Cut(authorization, " ")
	if !found || !strings.EqualFold(scheme, "Bearer") || token == "" {
		return false
	}
	return subtle.ConstantTimeCompare([]byte(token), []byte(serviceToken)) == 1
}

func validRequestID(value string) bool {
	if value == "" || len(value) > 128 {
		return false
	}
	for _, character := range value {
		if (character >= 'a' && character <= 'z') ||
			(character >= 'A' && character <= 'Z') ||
			(character >= '0' && character <= '9') ||
			character == '-' || character == '_' || character == '.' {
			continue
		}
		return false
	}
	return true
}

func newRequestID() string {
	bytes := make([]byte, 16)
	if _, err := rand.Read(bytes); err != nil {
		return fmt.Sprintf("%016x%016x", time.Now().UnixNano(), fallbackRequestID.Add(1))
	}
	return hex.EncodeToString(bytes)
}

func requestIDFromContext(ctx context.Context) string {
	value, _ := ctx.Value(requestIDKey{}).(string)
	return value
}

type responseCapture struct {
	http.ResponseWriter
	status int
	wrote  bool
}

func (w *responseCapture) WriteHeader(status int) {
	if w.wrote {
		return
	}
	w.wrote = true
	w.status = status
	w.ResponseWriter.WriteHeader(status)
}

func (w *responseCapture) Write(value []byte) (int, error) {
	if !w.wrote {
		w.WriteHeader(http.StatusOK)
	}
	return w.ResponseWriter.Write(value)
}

func (w *responseCapture) Unwrap() http.ResponseWriter {
	return w.ResponseWriter
}
