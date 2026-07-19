package api

import (
	"net/http"
	"strings"
	"time"

	"github.com/homeworldz/homeworldz/grid/internal/webaccount"
)

// bearerToken extracts a Bearer token from the Authorization header.
func bearerToken(r *http.Request) (string, bool) {
	scheme, token, found := strings.Cut(r.Header.Get("Authorization"), " ")
	if !found || !strings.EqualFold(scheme, "Bearer") || strings.TrimSpace(token) == "" {
		return "", false
	}
	return token, true
}

// requireAuth verifies the website token and loads current account state. On
// any failure it writes a 401 and returns false. The authorization-version
// claim is compared against the stored version so password, privilege, and ban
// changes (which bump the version) immediately invalidate prior tokens.
func (a *API) requireAuth(w http.ResponseWriter, r *http.Request) (webaccount.Account, bool) {
	token, ok := bearerToken(r)
	if !ok {
		a.writeUnauthorized(w)
		return webaccount.Account{}, false
	}
	claims, err := a.signer.Verify(token, time.Now())
	if err != nil {
		a.writeUnauthorized(w)
		return webaccount.Account{}, false
	}
	if a.accounts == nil {
		writeError(w, http.StatusServiceUnavailable, Error{Code: "account_store_unavailable", Message: "account storage is unavailable"})
		return webaccount.Account{}, false
	}
	account, err := a.accounts.Get(r.Context(), claims.Subject)
	if err != nil || account.AuthVersion != claims.Version {
		a.writeUnauthorized(w)
		return webaccount.Account{}, false
	}
	return account, true
}

// requirePrivilege writes a 403 and returns false when the account lacks the
// required capability.
func (a *API) requirePrivilege(w http.ResponseWriter, account webaccount.Account, required string) bool {
	if webaccount.HasPrivilege(account.Privileges, required) {
		return true
	}
	writeError(w, http.StatusForbidden, Error{Code: "forbidden", Message: "the required privilege is missing"})
	return false
}

func (a *API) writeUnauthorized(w http.ResponseWriter) {
	w.Header().Set("WWW-Authenticate", "Bearer")
	writeError(w, http.StatusUnauthorized, Error{Code: "unauthorized", Message: "a valid, unexpired website token is required"})
}
