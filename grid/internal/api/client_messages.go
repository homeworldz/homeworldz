package api

import (
	"context"
	"encoding/json"
	"errors"
	"net/http"
	"strings"
	"unicode/utf8"

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
	// Validation order (message, then recipient) is observed behavior clients
	// mirror; change it only deliberately. The limit counts characters, as
	// documented — not bytes, which would shrink it up to 4x for non-ASCII.
	text := strings.TrimSpace(request.Message)
	if text == "" || utf8.RuneCountInString(text) > 2048 {
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
// over. Called from the channel's serve loop after hello. 100 is a batch
// size, not a total: batches repeat until one comes back short, so a backlog
// of any depth drains on one connection — the client core caught the earlier
// version stranding everything past the first batch until the next connect.
func (a *API) deliverMessageBacklog(ctx context.Context, client *channelClient, userID string) {
	if a.messages == nil {
		return
	}
	const batchSize = 100
	for {
		backlog, err := a.messages.Undelivered(ctx, userID, batchSize)
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
				return
			}
			handed = append(handed, stored.ID)
		}
		if len(handed) == 0 {
			// Nothing marked means the same batch would return forever.
			return
		}
		if err := a.messages.MarkDelivered(ctx, handed); err != nil {
			if a.logger != nil {
				a.logger.Error("mark message backlog delivered", "error", err)
			}
			// Unmarked messages would return in the next batch forever.
			return
		}
		if len(backlog) < batchSize {
			return
		}
	}
}
