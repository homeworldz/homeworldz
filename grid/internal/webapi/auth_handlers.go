package webapi

import (
	"context"
	"errors"
	"net/http"
	"net/url"
	"strings"
	"time"

	"github.com/homeworldz/homeworldz/grid/internal/mailer"
	"github.com/homeworldz/homeworldz/grid/internal/webaccount"
)

// registrations handles POST /v1/registrations (public, rate-limited).
func (a *API) registrations(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		methodNotAllowed(w, http.MethodPost)
		return
	}
	if a.rateLimit(w, r) {
		return
	}
	if a.accounts == nil {
		writeError(w, http.StatusServiceUnavailable, Error{Code: "account_store_unavailable", Message: "account storage is unavailable"})
		return
	}
	var request registerAvatarRequest
	if !decodeJSON(w, r, &request) {
		return
	}
	account, code, err := a.accounts.Register(r.Context(), request.DisplayName, request.Email)
	switch {
	case errors.Is(err, webaccount.ErrInvalidDisplayName):
		writeError(w, http.StatusBadRequest, Error{Code: "invalid_display_name", Message: "display name must be two words that form a 3-32 character userid", Field: "displayName"})
		return
	case errors.Is(err, webaccount.ErrInvalidEmail):
		writeError(w, http.StatusBadRequest, Error{Code: "invalid_email", Message: "a valid email address is required", Field: "email"})
		return
	case errors.Is(err, webaccount.ErrConflict):
		writeError(w, http.StatusConflict, Error{Code: "userid_taken", Message: "that display name is already registered"})
		return
	case err != nil:
		a.internalError(w, r, "register account", err)
		return
	}
	a.sendVerificationEmail(r.Context(), account.Userid, request.Email, code)
	w.Header().Set("Location", "/v1/account")
	writeJSON(w, http.StatusCreated, RegistrationPending{Userid: account.Userid, DisplayName: account.DisplayName})
}

// verifications handles POST /v1/verifications (public, rate-limited).
func (a *API) verifications(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		methodNotAllowed(w, http.MethodPost)
		return
	}
	if a.rateLimit(w, r) {
		return
	}
	if a.accounts == nil {
		writeError(w, http.StatusServiceUnavailable, Error{Code: "account_store_unavailable", Message: "account storage is unavailable"})
		return
	}
	var request verifyRegistrationRequest
	if !decodeJSON(w, r, &request) {
		return
	}
	if strings.TrimSpace(request.Code) == "" || len(request.Code) > 128 {
		writeError(w, http.StatusBadRequest, Error{Code: "invalid_code", Message: "a confirmation code is required", Field: "code"})
		return
	}
	if !validPassword(w, request.Password) {
		return
	}
	account, err := a.accounts.Verify(r.Context(), request.Code, request.Password)
	switch {
	case errors.Is(err, webaccount.ErrInvalidCode):
		writeError(w, http.StatusConflict, Error{Code: "invalid_code", Message: "the confirmation code is invalid or expired"})
		return
	case err != nil:
		a.internalError(w, r, "verify registration", err)
		return
	}
	a.issueToken(w, r, account)
}

// resendVerification handles POST /v1/verifications/resend (public,
// rate-limited). It always responds 202 to avoid disclosing account state.
func (a *API) resendVerification(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		methodNotAllowed(w, http.MethodPost)
		return
	}
	if a.rateLimit(w, r) {
		return
	}
	var request resendVerificationRequest
	if !decodeJSON(w, r, &request) {
		return
	}
	if strings.TrimSpace(request.Userid) == "" {
		writeError(w, http.StatusBadRequest, Error{Code: "invalid_userid", Message: "a userid is required", Field: "userid"})
		return
	}
	if a.accounts != nil {
		if code, email, err := a.accounts.ResendVerification(r.Context(), request.Userid); err == nil {
			a.sendVerificationEmail(r.Context(), request.Userid, email, code)
		}
	}
	w.WriteHeader(http.StatusAccepted)
}

// tokens handles POST /v1/tokens (public, rate-limited).
func (a *API) tokens(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		methodNotAllowed(w, http.MethodPost)
		return
	}
	if a.rateLimit(w, r) {
		return
	}
	if a.accounts == nil {
		writeError(w, http.StatusServiceUnavailable, Error{Code: "account_store_unavailable", Message: "account storage is unavailable"})
		return
	}
	var request createTokenRequest
	if !decodeJSON(w, r, &request) {
		return
	}
	if strings.TrimSpace(request.Userid) == "" || request.Password == "" || len(request.Password) > 128 {
		writeError(w, http.StatusUnauthorized, Error{Code: "invalid_credentials", Message: "the userid or password is incorrect"})
		return
	}
	account, err := a.accounts.Authenticate(r.Context(), request.Userid, request.Password)
	if errors.Is(err, webaccount.ErrInvalidCredentials) {
		writeError(w, http.StatusUnauthorized, Error{Code: "invalid_credentials", Message: "the userid or password is incorrect"})
		return
	}
	if err != nil {
		a.internalError(w, r, "authenticate", err)
		return
	}
	a.issueToken(w, r, account)
}

// issueToken signs a website token for the account and writes a TokenResponse
// with no-store caching.
func (a *API) issueToken(w http.ResponseWriter, r *http.Request, account webaccount.Account) {
	token, expiresAt, err := a.signer.Sign(time.Now(), account.ID, account.Userid,
		account.DisplayName, account.RezDate, account.Privileges, account.AuthVersion)
	if err != nil {
		a.internalError(w, r, "issue token", err)
		return
	}
	w.Header().Set("Cache-Control", "no-store")
	writeJSON(w, http.StatusOK, TokenResponse{
		AccessToken: token,
		TokenType:   "Bearer",
		ExpiresAt:   expiresAt,
		Identity:    identityOf(account),
	})
}

// validPassword enforces the 8-128 character password bounds, writing a 400 on
// failure.
func validPassword(w http.ResponseWriter, password string) bool {
	if len(password) < 8 || len(password) > 128 {
		writeError(w, http.StatusBadRequest, Error{Code: "invalid_password", Message: "password must be 8-128 characters", Field: "password"})
		return false
	}
	return true
}

// sendVerificationEmail delivers the confirmation code. Delivery failures are
// logged (without the recipient address) and swallowed: the account exists and
// the code can be re-requested via resend.
func (a *API) sendVerificationEmail(ctx context.Context, userid, email, code string) {
	var body strings.Builder
	body.WriteString("Welcome to HomeWorldz.\n\n")
	body.WriteString("Your confirmation code for avatar \"" + userid + "\" is:\n\n")
	body.WriteString("    " + code + "\n\n")
	if a.verificationURL != "" {
		body.WriteString("Or open this link to finish setting up your account:\n")
		body.WriteString(a.verificationURL + "?code=" + url.QueryEscape(code) + "\n\n")
	}
	body.WriteString("This code expires in 24 hours. If you did not request it, ignore this email.\n")
	if err := a.mailer.Send(ctx, mailer.Message{
		To:      email,
		Subject: "Confirm your HomeWorldz avatar",
		Body:    body.String(),
	}); err != nil && a.logger != nil {
		a.logger.Error("send verification email", "userid", userid, "error", err)
	}
}

// internalError logs the underlying error and writes a generic 500.
func (a *API) internalError(w http.ResponseWriter, r *http.Request, operation string, err error) {
	if a.logger != nil {
		a.logger.Error("website api error",
			"requestId", requestIDFromContext(r.Context()), "operation", operation, "error", err)
	}
	writeError(w, http.StatusInternalServerError, Error{Code: "internal_error", Message: "an internal error occurred"})
}
