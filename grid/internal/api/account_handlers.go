package api

import (
	"errors"
	"net/http"

	"github.com/homeworldz/homeworldz/grid/internal/webaccount"
)

// account handles GET /v1/account.
func (a *API) account(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		methodNotAllowed(w, http.MethodGet)
		return
	}
	account, ok := a.requireAuth(w, r)
	if !ok {
		return
	}
	writeJSON(w, http.StatusOK, identityOf(account))
}

// accountProfile handles PATCH /v1/account/profile.
func (a *API) accountProfile(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPatch {
		methodNotAllowed(w, http.MethodPatch)
		return
	}
	account, ok := a.requireAuth(w, r)
	if !ok {
		return
	}
	var request updateProfileRequest
	if !decodeJSON(w, r, &request) {
		return
	}
	if request.DisplayName == nil {
		writeError(w, http.StatusBadRequest, Error{Code: "empty_update", Message: "at least one field must be provided"})
		return
	}
	updated, err := a.accounts.UpdateProfile(r.Context(), account.ID, *request.DisplayName)
	switch {
	case errors.Is(err, webaccount.ErrInvalidDisplayName):
		writeError(w, http.StatusBadRequest, Error{Code: "invalid_display_name", Message: "display name must be two words that form a 3-32 character userid", Field: "displayName"})
		return
	case errors.Is(err, webaccount.ErrDisplayNameTaken):
		writeError(w, http.StatusConflict, Error{Code: "display_name_taken", Message: "that display name is already in use", Field: "displayName"})
		return
	case errors.Is(err, webaccount.ErrNotFound):
		a.writeUnauthorized(w)
		return
	case err != nil:
		a.internalError(w, r, "update profile", err)
		return
	}
	writeJSON(w, http.StatusOK, identityOf(updated))
}

// accountPassword handles PUT /v1/account/password.
func (a *API) accountPassword(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPut {
		methodNotAllowed(w, http.MethodPut)
		return
	}
	account, ok := a.requireAuth(w, r)
	if !ok {
		return
	}
	var request changePasswordRequest
	if !decodeJSON(w, r, &request) {
		return
	}
	if request.CurrentPassword == "" || len(request.CurrentPassword) > 128 {
		writeError(w, http.StatusBadRequest, Error{Code: "invalid_password", Message: "the current password is required", Field: "currentPassword"})
		return
	}
	if !validPassword(w, request.NewPassword) {
		return
	}
	err := a.accounts.ChangePassword(r.Context(), account.ID, request.CurrentPassword, request.NewPassword)
	switch {
	case errors.Is(err, webaccount.ErrWrongPassword):
		writeError(w, http.StatusBadRequest, Error{Code: "wrong_password", Message: "the current password is incorrect", Field: "currentPassword"})
		return
	case errors.Is(err, webaccount.ErrNotFound):
		a.writeUnauthorized(w)
		return
	case err != nil:
		a.internalError(w, r, "change password", err)
		return
	}
	w.WriteHeader(http.StatusNoContent)
}
