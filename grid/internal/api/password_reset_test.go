package api

import (
	"bytes"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
)

// The request endpoint must answer identically whether or not an account
// matched, or it becomes a way to discover which accounts are registered
// (ADR 0034). Asserted on the whole response — status, body and headers — because
// checking only the status would miss a difference leaking through either of the
// other two.
func TestPasswordResetRequestCannotEnumerateAccounts(t *testing.T) {
	post := func(t *testing.T, store *memoryAccountStore, identifier string) *httptest.ResponseRecorder {
		t.Helper()
		handler := newTestAPI(t, func(o *Options) { o.Accounts = store })
		body, err := json.Marshal(passwordResetRequest{Identifier: identifier})
		if err != nil {
			t.Fatalf("marshal request: %v", err)
		}
		request := httptest.NewRequest(http.MethodPost, "/v1/password-resets", bytes.NewReader(body))
		request.Header.Set("Content-Type", "application/json")
		recorder := httptest.NewRecorder()
		handler.ServeHTTP(recorder, request)
		return recorder
	}

	known := post(t, &memoryAccountStore{resetToken: "a-real-token"}, "someone.known")
	unknown := post(t, &memoryAccountStore{}, "nobody.here")

	if known.Code != http.StatusAccepted {
		t.Fatalf("known account: status = %d, want %d", known.Code, http.StatusAccepted)
	}
	if unknown.Code != known.Code {
		t.Errorf("unknown account: status = %d, want %d — the two are distinguishable",
			unknown.Code, known.Code)
	}
	if unknown.Body.String() != known.Body.String() {
		t.Errorf("bodies differ: known %q, unknown %q — the two are distinguishable",
			known.Body.String(), unknown.Body.String())
	}
	if got, want := unknown.Header().Get("Content-Type"), known.Header().Get("Content-Type"); got != want {
		t.Errorf("content types differ: known %q, unknown %q", want, got)
	}
}

// Note on what is not tested here: that an email address cannot select an
// account. The handler passes the identifier through unchanged — correctly, since
// resolution is the store's job — so any assertion at this layer would be about
// the mock rather than about the rule. The rule lives in
// webaccount.PostgresStore.RequestPasswordReset, which resolves through
// DeriveUserid against username and display_name_key exactly as Authenticate
// does, and there is no test harness for that package. It is verified against the
// live grid instead: an address must produce no reset row, and the userid must.
// A mock test here would have passed either way.

// A malformed body is the one thing the endpoint does report, since it says
// nothing about any account and hiding it would conceal a client bug.
func TestPasswordResetRequestRejectsEmptyIdentifier(t *testing.T) {
	handler := newTestAPI(t, func(o *Options) { o.Accounts = &memoryAccountStore{resetToken: "a-real-token"} })
	request := httptest.NewRequest(http.MethodPost, "/v1/password-resets",
		strings.NewReader(`{"identifier":"  "}`))
	request.Header.Set("Content-Type", "application/json")
	recorder := httptest.NewRecorder()
	handler.ServeHTTP(recorder, request)
	if recorder.Code != http.StatusBadRequest {
		t.Fatalf("status = %d, want %d", recorder.Code, http.StatusBadRequest)
	}
}

// Unknown, already-used and expired tokens share one reply, so the response
// cannot say whether a token ever existed. The store collapses all three into
// ErrInvalidCode; this asserts the handler does not re-expand them.
func TestPasswordResetConsumeHidesWhyTheTokenFailed(t *testing.T) {
	store := &memoryAccountStore{resetToken: "a-real-token"}
	handler := newTestAPI(t, func(o *Options) { o.Accounts = store })

	consume := func(t *testing.T, token, password string) *httptest.ResponseRecorder {
		t.Helper()
		request := httptest.NewRequest(http.MethodPost, "/v1/password-resets/"+token,
			strings.NewReader(`{"password":"`+password+`"}`))
		request.Header.Set("Content-Type", "application/json")
		recorder := httptest.NewRecorder()
		handler.ServeHTTP(recorder, request)
		return recorder
	}

	good := consume(t, "a-real-token", "a-long-enough-password")
	if good.Code != http.StatusNoContent {
		t.Fatalf("valid token: status = %d, want %d (body %s)",
			good.Code, http.StatusNoContent, good.Body.String())
	}
	if store.resetConsumed != "a-long-enough-password" {
		t.Errorf("the new password was not written: got %q", store.resetConsumed)
	}

	bad := consume(t, "not-a-token", "a-long-enough-password")
	if bad.Code != http.StatusBadRequest {
		t.Errorf("unknown token: status = %d, want %d", bad.Code, http.StatusBadRequest)
	}
	if !strings.Contains(bad.Body.String(), "invalid_token") {
		t.Errorf("unknown token: body = %s, want an invalid_token code", bad.Body.String())
	}
	// The reply must not say which of the three reasons applied.
	for _, leak := range []string{"expired", "used", "unknown", "consumed"} {
		if strings.Contains(strings.ToLower(bad.Body.String()), leak) &&
			!strings.Contains(strings.ToLower(bad.Body.String()), "invalid or has expired") {
			t.Errorf("reply names why the token failed (%q): %s", leak, bad.Body.String())
		}
	}
}
