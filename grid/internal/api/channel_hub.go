package api

import "sync"

// channelHub tracks the open grid-channel connections of each signed-in user
// so server-initiated envelopes can reach them (docs/CLIENT2.md, "The
// communication mechanisms"). Delivery is best-effort to currently connected
// channels: nothing is persisted for users who are offline, because every
// notification the grid can produce today describes durable state a client
// re-reads on its next connection. Store-and-forward arrives with the
// notification kinds that need it (instant messages, inventory offers).

// channelSendBuffer bounds one connection's outbound queue. Grid-channel
// traffic is small and a healthy client drains continuously; a full queue
// marks a consumer too slow to be worth blocking anyone else for.
const channelSendBuffer = 16

type channelHub struct {
	mu      sync.Mutex
	clients map[string]map[*channelClient]struct{}
}

// channelClient is one authenticated connection's handle: the send queue its
// writer goroutine drains, keyed back to the user for deregistration.
type channelClient struct {
	userID string
	send   chan channelEnvelope
}

func newChannelHub() *channelHub {
	return &channelHub{clients: map[string]map[*channelClient]struct{}{}}
}

// register adds a connection for a user. A user may hold several connections
// (two browser tabs, a client and a browser); each registers separately and
// each receives every delivery.
func (h *channelHub) register(userID string) *channelClient {
	client := &channelClient{userID: userID, send: make(chan channelEnvelope, channelSendBuffer)}
	h.mu.Lock()
	defer h.mu.Unlock()
	set := h.clients[userID]
	if set == nil {
		set = map[*channelClient]struct{}{}
		h.clients[userID] = set
	}
	set[client] = struct{}{}
	return client
}

// deregister removes a connection. The send channel is never closed — a
// deliver holding the lock may still be enqueueing — it is simply abandoned
// to the collector once the writer goroutine stops draining it.
func (h *channelHub) deregister(client *channelClient) {
	h.mu.Lock()
	defer h.mu.Unlock()
	set := h.clients[client.userID]
	delete(set, client)
	if len(set) == 0 {
		delete(h.clients, client.userID)
	}
}

// deliver enqueues an envelope for every connection the user has open and
// reports how many accepted it. A connection whose queue is full is skipped
// rather than waited for: the message describes state the client will
// re-read anyway, and a slow consumer must not stall delivery to the rest.
func (h *channelHub) deliver(userID string, message channelEnvelope) int {
	h.mu.Lock()
	defer h.mu.Unlock()
	delivered := 0
	for client := range h.clients[userID] {
		select {
		case client.send <- message:
			delivered++
		default:
		}
	}
	return delivered
}
