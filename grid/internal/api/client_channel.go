package api

import (
	"context"
	"encoding/json"
	"net/http"
	"net/url"
	"strings"
	"time"

	"github.com/coder/websocket"

	"github.com/homeworldz/server/grid/internal/webaccount"
)

// The grid channel (docs/CLIENT2.md, "The communication mechanisms"): the
// persistent connection carrying what must reach a user regardless of which
// region they occupy. It is anchored to the grid precisely so it survives
// region crossings, and it is the client's request path after bootstrap —
// requests and server-initiated messages share it, paired by correlation
// identifiers.
//
// Every message is a JSON envelope, and the encoding is self-describing by
// its first byte: '{' is the envelope, any other leading byte is an encoding
// this server does not (yet) speak and is refused by name rather than fed to
// the JSON parser.

// channelEnvelopeVersion versions the envelope shape itself.
const channelEnvelopeVersion = 1

// channelAuthTimeout bounds how long an unauthenticated connection may hold a
// socket: browsers cannot set an Authorization header on a WebSocket, so the
// token arrives as the first message instead, and it must arrive promptly.
const channelAuthTimeout = 10 * time.Second

// channelReadLimit bounds one inbound message. Grid-channel traffic is small;
// anything larger is a misbehaving client.
const channelReadLimit = 64 * 1024

type channelEnvelope struct {
	Type          string          `json:"type"`
	Version       int             `json:"version"`
	CorrelationID string          `json:"correlationId,omitempty"`
	Payload       json.RawMessage `json:"payload,omitempty"`
}

type channelAuthPayload struct {
	Token string `json:"token"`
}

type channelHelloPayload struct {
	Grid     string   `json:"grid"`
	Identity Identity `json:"identity"`
	// Greeting is the grid-wide welcome, delivered here — the connection
	// that survives region crossings — precisely so entering a region never
	// re-welcomes anyone to the grid. Empty when the operator disabled it.
	Greeting string `json:"greeting,omitempty"`
}

type channelErrorPayload struct {
	Code    string `json:"code"`
	Message string `json:"message"`
}

// channelNotificationPayload is a server-initiated notification. Kind names
// the notification family; message is its operator- or sender-authored text.
type channelNotificationPayload struct {
	Kind    string    `json:"kind"`
	Message string    `json:"message"`
	SentAt  time.Time `json:"sentAt"`
}

// channelMessagePayload is the instant_message notification kind: a stored,
// sender-attributed message with its stable identifier, so a client can
// de-duplicate a live delivery against a later backlog replay.
type channelMessagePayload struct {
	Kind    string        `json:"kind"`
	ID      string        `json:"id"`
	From    MessageSender `json:"from"`
	Message string        `json:"message"`
	SentAt  time.Time     `json:"sentAt"`
}

// clientChannel upgrades GET /v1/client/channel.
func (a *API) clientChannel(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		w.Header().Set("Allow", "GET")
		writeError(w, http.StatusMethodNotAllowed, Error{Code: "method_not_allowed", Message: "only GET is supported"})
		return
	}
	if a.rateLimit(w, r) {
		return
	}
	conn, err := websocket.Accept(w, r, &websocket.AcceptOptions{
		OriginPatterns: a.channelOriginPatterns(),
	})
	if err != nil {
		// Accept has already written the HTTP error.
		return
	}
	conn.SetReadLimit(channelReadLimit)
	a.serveChannel(r.Context(), conn)
}

// channelOriginPatterns converts the CORS origin allowlist to the host
// patterns websocket.Accept matches Origin headers against. Same-origin
// requests pass without appearing here.
func (a *API) channelOriginPatterns() []string {
	patterns := make([]string, 0, len(a.allowedOrigins))
	for origin := range a.allowedOrigins {
		if parsed, err := url.Parse(origin); err == nil && parsed.Host != "" {
			patterns = append(patterns, parsed.Host)
		}
	}
	return patterns
}

func (a *API) serveChannel(ctx context.Context, conn *websocket.Conn) {
	defer conn.CloseNow()

	account, ok := a.channelAuthenticate(ctx, conn)
	if !ok {
		return
	}

	greeting := strings.ReplaceAll(a.welcomeText, "{grid}", a.gridName)
	greeting = strings.ReplaceAll(greeting, "{user}", account.DisplayName)
	hello, err := json.Marshal(channelHelloPayload{
		Grid: a.gridName, Identity: identityOf(account), Greeting: greeting})
	if err != nil {
		return
	}
	if !a.channelWrite(ctx, conn, channelEnvelope{
		Type: "hello", Version: channelEnvelopeVersion, Payload: hello,
	}) {
		return
	}

	// From here the connection has two message sources — replies to what the
	// read loop receives, and hub deliveries initiated elsewhere — so every
	// write funnels through one queue drained by a single writer goroutine.
	client := a.channels.register(account.ID)
	defer a.channels.deregister(client)

	ctx, cancel := context.WithCancel(ctx)
	defer cancel()
	go func() {
		defer cancel()
		for {
			select {
			case <-ctx.Done():
				return
			case message := <-client.send:
				if !a.channelWrite(ctx, conn, message) {
					return
				}
			}
		}
	}()

	// The store-and-forward backlog replays before this connection is told
	// anything else: it is already in sent order, the writer above preserves
	// queue order, and live deliveries land behind it.
	a.deliverMessageBacklog(ctx, client, account.ID)

	for {
		message, ok := a.channelRead(ctx, conn, 0)
		if !ok {
			return
		}
		switch message.Type {
		case "ping":
			if !client.enqueue(ctx, channelEnvelope{
				Type: "pong", Version: channelEnvelopeVersion, CorrelationID: message.CorrelationID,
			}) {
				return
			}
		default:
			payload, err := json.Marshal(channelErrorPayload{Code: "unsupported_type",
				Message: "this message type is not supported"})
			if err != nil {
				return
			}
			if !client.enqueue(ctx, channelEnvelope{
				Type: "error", Version: channelEnvelopeVersion, CorrelationID: message.CorrelationID, Payload: payload,
			}) {
				return
			}
		}
	}
}

// enqueue queues a reply from the read loop, waiting for space rather than
// dropping: unlike a hub delivery, a reply's ordering with respect to the
// request matters, and the writer goroutine is draining continuously.
func (c *channelClient) enqueue(ctx context.Context, message channelEnvelope) bool {
	select {
	case c.send <- message:
		return true
	case <-ctx.Done():
		return false
	}
}

// channelAuthenticate reads the mandatory first message, an auth envelope
// carrying the account bearer token, and resolves it exactly as requireAuth
// does for REST: signature, live account, authorization version. A region
// ticket fails here on its audience, which is the point of the audience.
func (a *API) channelAuthenticate(ctx context.Context, conn *websocket.Conn) (webaccount.Account, bool) {
	var account webaccount.Account
	message, ok := a.channelRead(ctx, conn, channelAuthTimeout)
	if !ok {
		return account, false
	}
	if message.Type != "auth" {
		conn.Close(websocket.StatusPolicyViolation, "the first message must be auth")
		return account, false
	}
	var payload channelAuthPayload
	if err := json.Unmarshal(message.Payload, &payload); err != nil || payload.Token == "" {
		conn.Close(websocket.StatusPolicyViolation, "auth requires a token")
		return account, false
	}
	claims, err := a.signer.Verify(payload.Token, time.Now())
	if err != nil {
		conn.Close(websocket.StatusPolicyViolation, "the token is invalid or expired")
		return account, false
	}
	resolved, err := a.accounts.Get(ctx, claims.Subject)
	if err != nil || resolved.AuthVersion != claims.Version {
		conn.Close(websocket.StatusPolicyViolation, "the token is invalid or expired")
		return account, false
	}
	return resolved, true
}

// channelRead reads one envelope, enforcing the text frame type, the leading
// byte rule, and an optional deadline.
func (a *API) channelRead(ctx context.Context, conn *websocket.Conn, timeout time.Duration) (channelEnvelope, bool) {
	if timeout > 0 {
		deadline, cancel := context.WithTimeout(ctx, timeout)
		defer cancel()
		ctx = deadline
	}
	kind, data, err := conn.Read(ctx)
	if err != nil {
		return channelEnvelope{}, false
	}
	if kind != websocket.MessageText || len(data) == 0 || data[0] != '{' {
		// A leading byte other than '{' is a different encoding, refused by
		// name (docs/CLIENT2.md, "Encoding: JSON on both channels").
		conn.Close(websocket.StatusUnsupportedData, "unsupported message encoding")
		return channelEnvelope{}, false
	}
	var message channelEnvelope
	if err := json.Unmarshal(data, &message); err != nil {
		conn.Close(websocket.StatusInvalidFramePayloadData, "the message is not a valid envelope")
		return channelEnvelope{}, false
	}
	return message, true
}

func (a *API) channelWrite(ctx context.Context, conn *websocket.Conn, message channelEnvelope) bool {
	encoded, err := json.Marshal(message)
	if err != nil {
		return false
	}
	return conn.Write(ctx, websocket.MessageText, encoded) == nil
}
