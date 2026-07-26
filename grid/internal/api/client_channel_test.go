package api

import (
	"context"
	"encoding/json"
	"net/http/httptest"
	"testing"
	"time"

	"github.com/coder/websocket"
)

func dialChannel(t *testing.T, handler *worldEntryHarness) (*websocket.Conn, func()) {
	t.Helper()
	server := httptest.NewServer(handler.handler)
	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	conn, _, err := websocket.Dial(ctx, server.URL+"/v1/client/channel", nil)
	if err != nil {
		cancel()
		server.Close()
		t.Fatalf("dial: %v", err)
	}
	return conn, func() {
		_ = conn.CloseNow()
		cancel()
		server.Close()
	}
}

func sendEnvelope(t *testing.T, conn *websocket.Conn, message channelEnvelope) {
	t.Helper()
	encoded, err := json.Marshal(message)
	if err != nil {
		t.Fatal(err)
	}
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	if err := conn.Write(ctx, websocket.MessageText, encoded); err != nil {
		t.Fatalf("write: %v", err)
	}
}

func readEnvelope(t *testing.T, conn *websocket.Conn) channelEnvelope {
	t.Helper()
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	kind, data, err := conn.Read(ctx)
	if err != nil {
		t.Fatalf("read: %v", err)
	}
	if kind != websocket.MessageText {
		t.Fatalf("frame type %v", kind)
	}
	var message channelEnvelope
	if err := json.Unmarshal(data, &message); err != nil {
		t.Fatalf("decode: %v", err)
	}
	return message
}

func authenticate(t *testing.T, conn *websocket.Conn, token string) channelEnvelope {
	t.Helper()
	payload, err := json.Marshal(channelAuthPayload{Token: token})
	if err != nil {
		t.Fatal(err)
	}
	sendEnvelope(t, conn, channelEnvelope{Type: "auth", Version: 1, Payload: payload})
	return readEnvelope(t, conn)
}

func TestChannelAuthenticatesAndGreets(t *testing.T) {
	harness := newWorldEntryHarness(t)
	conn, done := dialChannel(t, harness)
	defer done()

	hello := authenticate(t, conn, harness.token)
	if hello.Type != "hello" || hello.Version != 1 {
		t.Fatalf("unexpected greeting: %+v", hello)
	}
	var greeting channelHelloPayload
	if err := json.Unmarshal(hello.Payload, &greeting); err != nil {
		t.Fatal(err)
	}
	if greeting.Grid != "Homeworldz Test" || greeting.Identity.ID != testUserID {
		t.Fatalf("unexpected hello payload: %+v", greeting)
	}
}

func TestChannelPingPongCarriesCorrelation(t *testing.T) {
	harness := newWorldEntryHarness(t)
	conn, done := dialChannel(t, harness)
	defer done()
	authenticate(t, conn, harness.token)

	sendEnvelope(t, conn, channelEnvelope{Type: "ping", Version: 1, CorrelationID: "req-42"})
	pong := readEnvelope(t, conn)
	if pong.Type != "pong" || pong.CorrelationID != "req-42" {
		t.Fatalf("unexpected pong: %+v", pong)
	}
}

func TestChannelUnknownTypeGetsErrorEnvelope(t *testing.T) {
	harness := newWorldEntryHarness(t)
	conn, done := dialChannel(t, harness)
	defer done()
	authenticate(t, conn, harness.token)

	sendEnvelope(t, conn, channelEnvelope{Type: "mystery", Version: 1, CorrelationID: "req-7"})
	failure := readEnvelope(t, conn)
	if failure.Type != "error" || failure.CorrelationID != "req-7" {
		t.Fatalf("unexpected error envelope: %+v", failure)
	}
	var payload channelErrorPayload
	if err := json.Unmarshal(failure.Payload, &payload); err != nil {
		t.Fatal(err)
	}
	if payload.Code != "unsupported_type" {
		t.Fatalf("unexpected error code: %+v", payload)
	}
}

func TestChannelRefusesBadToken(t *testing.T) {
	harness := newWorldEntryHarness(t)
	conn, done := dialChannel(t, harness)
	defer done()

	payload, _ := json.Marshal(channelAuthPayload{Token: "not-a-token"})
	sendEnvelope(t, conn, channelEnvelope{Type: "auth", Version: 1, Payload: payload})
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	if _, _, err := conn.Read(ctx); err == nil {
		t.Fatal("expected close after bad token")
	} else if websocket.CloseStatus(err) != websocket.StatusPolicyViolation {
		t.Fatalf("close status: %v", err)
	}
}

func TestChannelRefusesRegionTicket(t *testing.T) {
	harness := newWorldEntryHarness(t)

	// Mint a real region ticket via world entry, then present it on the grid
	// channel: the audience check must refuse it exactly as account routes do.
	response := harness.open(t, `{}`, harness.token)
	var opened ClientSession
	if err := json.Unmarshal(response.Body.Bytes(), &opened); err != nil {
		t.Fatal(err)
	}

	conn, done := dialChannel(t, harness)
	defer done()
	payload, _ := json.Marshal(channelAuthPayload{Token: opened.Ticket.Token})
	sendEnvelope(t, conn, channelEnvelope{Type: "auth", Version: 1, Payload: payload})
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	if _, _, err := conn.Read(ctx); err == nil {
		t.Fatal("expected close after region ticket")
	} else if websocket.CloseStatus(err) != websocket.StatusPolicyViolation {
		t.Fatalf("close status: %v", err)
	}
}

func TestChannelRefusesNonJSONEncodingByFirstByte(t *testing.T) {
	harness := newWorldEntryHarness(t)
	conn, done := dialChannel(t, harness)
	defer done()

	// 'P' could be the first byte of some framed binary format; the channel
	// refuses it by name instead of feeding it to the JSON parser.
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	if err := conn.Write(ctx, websocket.MessageText, []byte("P\x02binary")); err != nil {
		t.Fatalf("write: %v", err)
	}
	if _, _, err := conn.Read(ctx); err == nil {
		t.Fatal("expected close after unsupported encoding")
	} else if websocket.CloseStatus(err) != websocket.StatusUnsupportedData {
		t.Fatalf("close status: %v", err)
	}
}
