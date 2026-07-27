package api

import (
	"context"
	"encoding/json"
	"errors"
	"net/http"
	"strings"

	"github.com/homeworldz/server/grid/internal/messages"
	"github.com/homeworldz/server/grid/internal/webaccount"
)

// Instant messages (docs/CLIENT2.md, "What the grid channel carries today"):
// the first store-and-forward notification kind. A message is stored before
// any delivery is attempted — durability is what distinguishes it from a
// system notice — then delivered live to the recipient's open channels, and
// otherwise replayed from the backlog on their next connection.

// clientMessages serves POST /v1/client/messages. The sender is the bearer
// token's account, never a body field.
func (a *API) clientMessages(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		methodNotAllowed(w, http.MethodPost)
		return
	}
	account, ok := a.requireAuth(w, r)
	if !ok {
		return
	}
	if a.messages == nil {
		writeError(w, http.StatusServiceUnavailable, Error{Code: "messages_unavailable", Message: "instant messages are not available"})
		return
	}
	var request sendMessageRequest
	if !decodeJSON(w, r, &request) {
		return
	}
	text := strings.TrimSpace(request.Message)
	if text == "" || len(text) > 2048 {
		writeError(w, http.StatusBadRequest, Error{Code: "invalid_message", Message: "message must be 1-2048 characters", Field: "message"})
		return
	}
	if !validUUID(request.To) {
		writeError(w, http.StatusBadRequest, Error{Code: "invalid_recipient", Message: "to must be a user id", Field: "to"})
		return
	}
	if _, err := a.accounts.Get(r.Context(), request.To); errors.Is(err, webaccount.ErrNotFound) {
		a.writeNotFound(w)
		return
	} else if err != nil {
		a.internalError(w, r, "load recipient", err)
		return
	}

	stored, err := a.messages.Create(r.Context(), account.ID, request.To, text)
	if err != nil {
		a.internalError(w, r, "store instant message", err)
		return
	}
	delivered := 0
	if payload, err := a.messagePayload(r.Context(), stored); err == nil {
		delivered = a.channels.deliver(request.To, channelEnvelope{
			Type: "notification", Version: channelEnvelopeVersion, Payload: payload})
		if delivered > 0 {
			// Best-effort marking, as documented on the store: handed to a
			// connection counts as delivered.
			if err := a.messages.MarkDelivered(r.Context(), []string{stored.ID}); err != nil && a.logger != nil {
				a.logger.Error("mark message delivered", "error", err)
			}
		}
	}
	writeJSON(w, http.StatusOK, MessageResult{ID: stored.ID, SentAt: stored.SentAt.UTC(), Delivered: delivered})
}

// messagePayload renders one instant message as a notification payload,
// resolving the sender's identity for display.
func (a *API) messagePayload(ctx context.Context, stored messages.Message) (json.RawMessage, error) {
	sender, err := a.accounts.Get(ctx, stored.FromUserID)
	if err != nil {
		return nil, err
	}
	return json.Marshal(channelMessagePayload{
		Kind:    "instant_message",
		ID:      stored.ID,
		From:    MessageSender{ID: sender.ID, Userid: sender.Userid, DisplayName: sender.DisplayName},
		Message: stored.Message,
		SentAt:  stored.SentAt.UTC(),
	})
}

// deliverMessageBacklog replays a user's undelivered messages onto a newly
// authenticated channel connection, in sent order, marking what was handed
// over. Called from the channel's serve loop after hello.
func (a *API) deliverMessageBacklog(ctx context.Context, client *channelClient, userID string) {
	if a.messages == nil {
		return
	}
	backlog, err := a.messages.Undelivered(ctx, userID, 100)
	if err != nil || len(backlog) == 0 {
		return
	}
	handed := make([]string, 0, len(backlog))
	for _, stored := range backlog {
		payload, err := a.messagePayload(ctx, stored)
		if err != nil {
			continue
		}
		if !client.enqueue(ctx, channelEnvelope{
			Type: "notification", Version: channelEnvelopeVersion, Payload: payload}) {
			break
		}
		handed = append(handed, stored.ID)
	}
	if len(handed) > 0 {
		if err := a.messages.MarkDelivered(ctx, handed); err != nil && a.logger != nil {
			a.logger.Error("mark message backlog delivered", "error", err)
		}
	}
}
