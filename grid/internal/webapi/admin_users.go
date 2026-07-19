package webapi

import (
	"errors"
	"net/http"
	"strconv"
	"strings"

	"github.com/homeworldz/homeworldz/grid/internal/webaccount"
)

// adminUsersRoot handles GET /v1/admin/users (list/search).
func (a *API) adminUsersRoot(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		methodNotAllowed(w, http.MethodGet)
		return
	}
	account, ok := a.requireAuth(w, r)
	if !ok || !a.requirePrivilege(w, account, webaccount.PrivUsers) {
		return
	}
	query := r.URL.Query()
	search := query.Get("search")
	cursor := query.Get("cursor")
	if len(search) > 128 || len(cursor) > 512 {
		writeError(w, http.StatusBadRequest, Error{Code: "invalid_query", Message: "search or cursor is too long"})
		return
	}
	limit := 50
	if raw := query.Get("limit"); raw != "" {
		parsed, err := strconv.Atoi(raw)
		if err != nil || parsed < 1 || parsed > 100 {
			writeError(w, http.StatusBadRequest, Error{Code: "invalid_limit", Message: "limit must be between 1 and 100", Field: "limit"})
			return
		}
		limit = parsed
	}
	items, nextCursor, err := a.accounts.List(r.Context(), search, cursor, limit)
	if err != nil {
		a.internalError(w, r, "list users", err)
		return
	}
	users := make([]ManagedUser, 0, len(items))
	for _, item := range items {
		users = append(users, managedUserOf(item))
	}
	writeJSON(w, http.StatusOK, UserPage{Users: users, NextCursor: nextCursor})
}

// adminUserByID dispatches /v1/admin/users/{id} and its sub-resources.
func (a *API) adminUserByID(w http.ResponseWriter, r *http.Request) {
	account, ok := a.requireAuth(w, r)
	if !ok {
		return
	}
	parts := strings.Split(strings.Trim(strings.TrimPrefix(r.URL.Path, "/v1/admin/users/"), "/"), "/")
	if len(parts) == 0 || !validUUID(parts[0]) {
		a.notFound(w, r)
		return
	}
	id := parts[0]

	switch {
	case len(parts) == 1 && r.Method == http.MethodGet:
		if !a.requirePrivilege(w, account, webaccount.PrivUsers) {
			return
		}
		a.writeManaged(w, r, id, "get user")
	case len(parts) == 1 && r.Method == http.MethodPatch:
		if !a.requirePrivilege(w, account, webaccount.PrivUsers) {
			return
		}
		a.adminUpdateUser(w, r, id)
	case len(parts) == 1:
		methodNotAllowed(w, http.MethodGet, http.MethodPatch)
	case len(parts) == 2 && parts[1] == "privileges" && r.Method == http.MethodPut:
		a.adminReplacePrivileges(w, r, account, id)
	case len(parts) == 2 && parts[1] == "ban" && r.Method == http.MethodPut:
		a.adminBanUser(w, r, account, id)
	case len(parts) == 2 && parts[1] == "ban" && r.Method == http.MethodDelete:
		a.adminUnbanUser(w, r, account, id)
	case len(parts) == 2 && parts[1] == "privileges":
		methodNotAllowed(w, http.MethodPut)
	case len(parts) == 2 && parts[1] == "ban":
		methodNotAllowed(w, http.MethodPut, http.MethodDelete)
	default:
		a.notFound(w, r)
	}
}

func (a *API) adminUpdateUser(w http.ResponseWriter, r *http.Request, id string) {
	var request updateProfileRequest
	if !decodeJSON(w, r, &request) {
		return
	}
	if request.DisplayName == nil {
		writeError(w, http.StatusBadRequest, Error{Code: "empty_update", Message: "at least one field must be provided"})
		return
	}
	_, err := a.accounts.UpdateProfile(r.Context(), id, *request.DisplayName)
	switch {
	case errors.Is(err, webaccount.ErrInvalidDisplayName):
		writeError(w, http.StatusBadRequest, Error{Code: "invalid_display_name", Message: "display name must be 1-64 characters", Field: "displayName"})
		return
	case errors.Is(err, webaccount.ErrNotFound):
		a.writeNotFound(w)
		return
	case err != nil:
		a.internalError(w, r, "update user", err)
		return
	}
	a.writeManaged(w, r, id, "get user")
}

func (a *API) adminReplacePrivileges(w http.ResponseWriter, r *http.Request, actor webaccount.Account, id string) {
	if !a.requirePrivilege(w, actor, webaccount.PrivAdmin) {
		return
	}
	var request replacePrivilegesRequest
	if !decodeJSON(w, r, &request) {
		return
	}
	normalized, err := webaccount.NormalizePrivileges(request.Privs)
	if err != nil {
		writeError(w, http.StatusBadRequest, Error{Code: "invalid_privileges", Message: "privileges must be a comma-separated list of names", Field: "privs"})
		return
	}
	target, err := a.accounts.GetManaged(r.Context(), id)
	if errors.Is(err, webaccount.ErrNotFound) {
		a.writeNotFound(w)
		return
	}
	if err != nil {
		a.internalError(w, r, "load user", err)
		return
	}
	// Only super may grant or revoke the super privilege.
	if webaccount.IsSuper(normalized) != webaccount.IsSuper(target.Privileges) && !webaccount.IsSuper(actor.Privileges) {
		writeError(w, http.StatusForbidden, Error{Code: "forbidden", Message: "only a super account may grant or revoke super"})
		return
	}
	managed, err := a.accounts.ReplacePrivileges(r.Context(), id, normalized)
	switch {
	case errors.Is(err, webaccount.ErrNotFound):
		a.writeNotFound(w)
		return
	case errors.Is(err, webaccount.ErrLastSuper):
		writeError(w, http.StatusConflict, Error{Code: "last_super", Message: "the final super account cannot be demoted"})
		return
	case err != nil:
		a.internalError(w, r, "replace privileges", err)
		return
	}
	writeJSON(w, http.StatusOK, managedUserOf(managed))
}

func (a *API) adminBanUser(w http.ResponseWriter, r *http.Request, actor webaccount.Account, id string) {
	if !a.requirePrivilege(w, actor, webaccount.PrivBans) {
		return
	}
	var request banUserRequest
	if !decodeJSON(w, r, &request) {
		return
	}
	reason := strings.TrimSpace(request.Reason)
	if reason == "" || len(reason) > 1024 {
		writeError(w, http.StatusBadRequest, Error{Code: "invalid_reason", Message: "reason must be 1-1024 characters", Field: "reason"})
		return
	}
	managed, err := a.accounts.Ban(r.Context(), id, reason, request.ExpiresAt, actor.ID)
	switch {
	case errors.Is(err, webaccount.ErrNotFound):
		a.writeNotFound(w)
		return
	case err != nil:
		a.internalError(w, r, "ban user", err)
		return
	}
	writeJSON(w, http.StatusOK, managedUserOf(managed))
}

func (a *API) adminUnbanUser(w http.ResponseWriter, r *http.Request, actor webaccount.Account, id string) {
	if !a.requirePrivilege(w, actor, webaccount.PrivBans) {
		return
	}
	managed, err := a.accounts.Unban(r.Context(), id)
	switch {
	case errors.Is(err, webaccount.ErrNotFound):
		a.writeNotFound(w)
		return
	case err != nil:
		a.internalError(w, r, "unban user", err)
		return
	}
	writeJSON(w, http.StatusOK, managedUserOf(managed))
}

func (a *API) writeManaged(w http.ResponseWriter, r *http.Request, id, operation string) {
	managed, err := a.accounts.GetManaged(r.Context(), id)
	if errors.Is(err, webaccount.ErrNotFound) {
		a.writeNotFound(w)
		return
	}
	if err != nil {
		a.internalError(w, r, operation, err)
		return
	}
	writeJSON(w, http.StatusOK, managedUserOf(managed))
}

func (a *API) writeNotFound(w http.ResponseWriter) {
	writeError(w, http.StatusNotFound, Error{Code: "not_found", Message: "resource not found"})
}
