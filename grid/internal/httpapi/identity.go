package httpapi

import (
	"errors"
	"net/http"
	"strings"
	"time"

	"github.com/homeworldz/homeworldz/grid/internal/identity"
)

func (a *API) usersRoot(w http.ResponseWriter, r *http.Request) {
	if a.identity == nil {
		identityUnavailable(w)
		return
	}
	if r.Method != http.MethodPost {
		w.Header().Set("Allow", http.MethodPost)
		writeJSON(w, http.StatusMethodNotAllowed, Error{Code: "method_not_allowed", Message: "only POST is supported"})
		return
	}
	var request CreateUserRequest
	if !decodeJSON(w, r, &request) {
		return
	}
	request.Username = strings.ToLower(strings.TrimSpace(request.Username))
	if !validUsername(request.Username) || len(request.Password) < 8 || len(request.Password) > 128 {
		writeJSON(w, http.StatusBadRequest, Error{Code: "invalid_user", Message: "username or password does not meet development account requirements"})
		return
	}
	user, err := a.identity.CreateUser(r.Context(), request.Username, request.Password)
	if errors.Is(err, identity.ErrConflict) {
		writeJSON(w, http.StatusConflict, Error{Code: "username_in_use", Message: "username is already registered"})
		return
	}
	if err != nil {
		writeJSON(w, http.StatusInternalServerError, Error{Code: "identity_store_error", Message: "user creation failed"})
		return
	}
	w.Header().Set("Location", "/api/v1/users/"+user.ID)
	writeJSON(w, http.StatusCreated, user)
}

func (a *API) userByID(w http.ResponseWriter, r *http.Request) {
	if a.identity == nil {
		identityUnavailable(w)
		return
	}
	if r.Method != http.MethodGet {
		w.Header().Set("Allow", http.MethodGet)
		writeJSON(w, http.StatusMethodNotAllowed, Error{Code: "method_not_allowed", Message: "only GET is supported"})
		return
	}
	id := strings.TrimPrefix(r.URL.Path, "/api/v1/users/")
	if !validUUID(id) {
		a.notFound(w, r)
		return
	}
	user, err := a.identity.FindUser(r.Context(), id)
	if errors.Is(err, identity.ErrUserNotFound) {
		writeJSON(w, http.StatusNotFound, Error{Code: "user_not_found", Message: "user was not found"})
	} else if err != nil {
		writeJSON(w, http.StatusInternalServerError, Error{Code: "identity_store_error", Message: "user lookup failed"})
	} else {
		writeJSON(w, http.StatusOK, user)
	}
}

func (a *API) sessionsRoot(w http.ResponseWriter, r *http.Request) {
	if a.identity == nil {
		identityUnavailable(w)
		return
	}
	if r.Method != http.MethodPost {
		w.Header().Set("Allow", http.MethodPost)
		writeJSON(w, http.StatusMethodNotAllowed, Error{Code: "method_not_allowed", Message: "only POST is supported"})
		return
	}
	var request CreateSessionRequest
	if !decodeJSON(w, r, &request) {
		return
	}
	request.Username = strings.ToLower(strings.TrimSpace(request.Username))
	duration, ok := validateSessionDuration(w, request.SessionSeconds)
	if !ok || request.Username == "" || request.Password == "" {
		if ok {
			writeJSON(w, http.StatusBadRequest, Error{Code: "invalid_login", Message: "username and password are required"})
		}
		return
	}
	session, err := a.identity.CreateSession(r.Context(), request.Username, request.Password, duration)
	if errors.Is(err, identity.ErrInvalidCredentials) {
		writeJSON(w, http.StatusUnauthorized, Error{Code: "invalid_credentials", Message: "username or password is incorrect"})
		return
	}
	if err != nil {
		writeJSON(w, http.StatusInternalServerError, Error{Code: "identity_store_error", Message: "session creation failed"})
		return
	}
	w.Header().Set("Location", "/api/v1/sessions/"+session.ID)
	writeJSON(w, http.StatusCreated, session)
}

func (a *API) sessionByID(w http.ResponseWriter, r *http.Request) {
	if a.identity == nil {
		identityUnavailable(w)
		return
	}
	id := strings.TrimPrefix(r.URL.Path, "/api/v1/sessions/")
	if !validUUID(id) {
		a.notFound(w, r)
		return
	}
	switch r.Method {
	case http.MethodGet:
		session, err := a.identity.ValidateSession(r.Context(), id)
		if errors.Is(err, identity.ErrSessionNotFound) {
			writeJSON(w, http.StatusNotFound, Error{Code: "session_not_found", Message: "session was not found or has expired"})
		} else if err != nil {
			writeJSON(w, http.StatusInternalServerError, Error{Code: "identity_store_error", Message: "session validation failed"})
		} else {
			writeJSON(w, http.StatusOK, session)
		}
	case http.MethodDelete:
		if err := a.identity.RevokeSession(r.Context(), id); errors.Is(err, identity.ErrSessionNotFound) {
			writeJSON(w, http.StatusNotFound, Error{Code: "session_not_found", Message: "session was not found"})
		} else if err != nil {
			writeJSON(w, http.StatusInternalServerError, Error{Code: "identity_store_error", Message: "session revocation failed"})
		} else {
			w.WriteHeader(http.StatusNoContent)
		}
	default:
		w.Header().Set("Allow", "GET, DELETE")
		writeJSON(w, http.StatusMethodNotAllowed, Error{Code: "method_not_allowed", Message: "only GET and DELETE are supported"})
	}
}

func identityUnavailable(w http.ResponseWriter) {
	writeJSON(w, http.StatusServiceUnavailable, Error{Code: "identity_store_unavailable", Message: "identity storage is unavailable"})
}

func validUsername(value string) bool {
	if len(value) < 3 || len(value) > 32 {
		return false
	}
	for _, character := range value {
		if (character >= 'a' && character <= 'z') || (character >= '0' && character <= '9') ||
			character == '.' || character == '_' || character == '-' {
			continue
		}
		return false
	}
	return true
}

func validateSessionDuration(w http.ResponseWriter, seconds int) (time.Duration, bool) {
	if seconds == 0 {
		seconds = 12 * 60 * 60
	}
	if seconds < 5*60 || seconds > 24*60*60 {
		writeJSON(w, http.StatusBadRequest, Error{Code: "invalid_session_duration", Message: "sessionSeconds must be between 300 and 86400"})
		return 0, false
	}
	return time.Duration(seconds) * time.Second, true
}
