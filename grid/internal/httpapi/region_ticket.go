package httpapi

import (
	"net/http"
	"time"

	"github.com/homeworldz/server/grid/internal/identity"
)

// validateRegionTicket serves POST /api/v1/region-runtime/{id}/validate-ticket.
//
// A region receiving a client's region ticket cannot verify it locally: the
// ticket is signed with the grid's secret, and handing that secret to
// software the operator does not run would let any region mint account
// tokens (ADR 0028 admits exactly such regions). So the region presents the
// ticket here, authenticated by its own access key, and the grid — which
// already holds the secret — answers with the identity the ticket resolves
// to. The path parameter binds the check: a ticket minted for another region
// is refused even though its signature is genuine, which is the region-ID
// claim doing its job.
//
// Every failure is the same 401 invalid_ticket: which claim failed is
// diagnostic detail a caller must not need and an attacker must not get.
func (a *API) validateRegionTicket(w http.ResponseWriter, r *http.Request, regionID string) {
	if a.ticketVerifier == nil || a.identity == nil {
		writeJSON(w, http.StatusServiceUnavailable, Error{
			Code: "ticket_validation_unavailable", Message: "ticket validation is not configured"})
		return
	}
	var request ValidateRegionTicketRequest
	if !decodeJSON(w, r, &request) {
		return
	}
	refuse := func() {
		writeJSON(w, http.StatusUnauthorized, Error{
			Code: "invalid_ticket", Message: "the ticket is invalid, expired, or for another region"})
	}
	claims, err := a.ticketVerifier.Verify(request.Token, time.Now())
	if err != nil || claims.RegionID != regionID || claims.SessionID == "" {
		refuse()
		return
	}
	session, err := a.identity.ValidateSession(r.Context(), claims.SessionID)
	if err == identity.ErrSessionNotFound {
		refuse()
		return
	}
	if err != nil {
		writeJSON(w, http.StatusInternalServerError, Error{
			Code: "session_store_error", Message: "session validation failed"})
		return
	}
	if session.UserID != claims.Subject {
		refuse()
		return
	}
	writeJSON(w, http.StatusOK, ValidateRegionTicketResult{
		UserID:      claims.Subject,
		Userid:      claims.Userid,
		DisplayName: claims.DisplayName,
		SessionID:   claims.SessionID,
		ExpiresAt:   claims.ExpiresAt,
	})
}
