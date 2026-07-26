package api

import (
	"context"
	"encoding/json"
	"log/slog"
	"net/http"
	"net/http/httptest"
	"testing"
	"time"

	"github.com/homeworldz/homeworldz/grid/internal/arrival"
	"github.com/homeworldz/homeworldz/grid/internal/mailer"
	"github.com/homeworldz/homeworldz/grid/internal/provisioning"
	"github.com/homeworldz/homeworldz/grid/internal/webtoken"
)

// memoryRegionStore satisfies RegionStore with a fixed provisioned-region set.
type memoryRegionStore struct{ items []provisioning.Region }

func (s *memoryRegionStore) List(context.Context) ([]provisioning.Region, error) {
	return s.items, nil
}

func (s *memoryRegionStore) Get(_ context.Context, id string) (provisioning.Region, error) {
	for _, item := range s.items {
		if item.ID == id {
			return item, nil
		}
	}
	return provisioning.Region{}, provisioning.ErrNotFound
}

func (s *memoryRegionStore) Create(_ context.Context, region provisioning.Region) (provisioning.Region, error) {
	return region, nil
}

func (s *memoryRegionStore) Update(_ context.Context, _ string, _ provisioning.Update) (provisioning.Region, error) {
	return provisioning.Region{}, provisioning.ErrNotFound
}

func (s *memoryRegionStore) RotateAccessKey(_ context.Context, _, _ string) (provisioning.Region, error) {
	return provisioning.Region{}, provisioning.ErrNotFound
}

func (s *memoryRegionStore) Delete(context.Context, string) error { return nil }

// newTestAPI builds the handler with in-memory collaborators. Options mutators
// let a test add stores or arrival points without repeating the boilerplate.
func newTestAPI(t *testing.T, mutate ...func(*Options)) http.Handler {
	t.Helper()
	signer, err := webtoken.NewSigner(
		[]byte("0123456789abcdef0123456789abcdef"), "https://issuer.test", "https://audience.test", time.Hour)
	if err != nil {
		t.Fatalf("signer: %v", err)
	}
	logger := slog.New(slog.NewTextHandler(testWriter{t}, &slog.HandlerOptions{Level: slog.LevelError}))
	options := Options{
		Signer:   signer,
		Mailer:   mailer.NewLogMailer(logger, "test@homeworldz.test"),
		Logger:   logger,
		Version:  "1.2.3",
		GridName: "Homeworldz Test",
	}
	for _, m := range mutate {
		m(&options)
	}
	handler, err := New(options)
	if err != nil {
		t.Fatalf("new api: %v", err)
	}
	return handler
}

type testWriter struct{ t *testing.T }

func (w testWriter) Write(p []byte) (int, error) {
	w.t.Log(string(p))
	return len(p), nil
}

func TestClientVersionProbe(t *testing.T) {
	handler := newTestAPI(t)
	response := httptest.NewRecorder()
	handler.ServeHTTP(response, httptest.NewRequest(http.MethodGet, "/v1/version", nil))
	if response.Code != http.StatusOK {
		t.Fatalf("status %d: %s", response.Code, response.Body.String())
	}

	var document Version
	if err := json.Unmarshal(response.Body.Bytes(), &document); err != nil {
		t.Fatalf("decode: %v", err)
	}
	if document.Service != "homeworldz-api" || document.Version != "1.2.3" || document.APIVersion != "v1" {
		t.Fatalf("unexpected document header: %+v", document)
	}
	if document.Client.Protocol != 1 || document.Client.MinimumProtocol != 1 {
		t.Fatalf("unexpected protocol gate: %+v", document.Client)
	}
	if document.Client.Grid.Name != "Homeworldz Test" {
		t.Fatalf("unexpected grid name: %+v", document.Client.Grid)
	}
	// Honesty: what is built is advertised, and nothing more. The grid channel
	// ships in this binary; its absolute URL appears only when the deployment
	// knows its public URL.
	if document.Client.Grid.Channel != "websocket" || document.Client.Grid.ChannelURL != "" {
		t.Fatalf("unexpected grid channel advertisement: %+v", document.Client.Grid)
	}
	if len(document.Client.Regions.Transports) != 0 || len(document.Client.Regions.AssetFormats) != 0 ||
		document.Client.Regions.MeshedPrims {
		t.Fatalf("region capabilities advertised before they exist: %+v", document.Client.Regions)
	}
	if document.Client.Welcome != nil {
		t.Fatalf("welcome invented with no arrival list: %+v", document.Client.Welcome)
	}
	// The empty lists must serialize as [], not null: a client indexes them.
	body := response.Body.String()
	if !json.Valid([]byte(body)) {
		t.Fatal("invalid JSON body")
	}
	var raw map[string]any
	_ = json.Unmarshal([]byte(body), &raw)
	client := raw["client"].(map[string]any)
	regions := client["regions"].(map[string]any)
	if _, ok := regions["transports"].([]any); !ok {
		t.Fatalf("transports must be an array, got %T", regions["transports"])
	}
}

func TestClientVersionWelcomeDerivesFromArrivalList(t *testing.T) {
	handler := newTestAPI(t, func(options *Options) {
		options.Welcome = []arrival.Point{{Region: "Welcome", X: 127, Y: 127, Z: 23}}
		options.Regions = &memoryRegionStore{items: []provisioning.Region{
			{ID: "r1", Name: "Elsewhere", MapX: 1004, MapY: 1004},
			{ID: "r2", Name: "Welcome", MapX: 1000, MapY: 1000},
		}}
	})
	response := httptest.NewRecorder()
	handler.ServeHTTP(response, httptest.NewRequest(http.MethodGet, "/v1/version", nil))
	if response.Code != http.StatusOK {
		t.Fatalf("status %d: %s", response.Code, response.Body.String())
	}
	var document Version
	if err := json.Unmarshal(response.Body.Bytes(), &document); err != nil {
		t.Fatalf("decode: %v", err)
	}
	welcome := document.Client.Welcome
	if welcome == nil || welcome.Name != "Welcome" {
		t.Fatalf("unexpected welcome: %+v", welcome)
	}
	if welcome.GridX == nil || welcome.GridY == nil || *welcome.GridX != 1000 || *welcome.GridY != 1000 {
		t.Fatalf("welcome coordinates not resolved: %+v", welcome)
	}
}

func TestClientVersionWelcomeWithoutProvisionedRecordKeepsName(t *testing.T) {
	handler := newTestAPI(t, func(options *Options) {
		options.Welcome = []arrival.Point{{Region: "Welcome", X: 127, Y: 127, Z: 23}}
		options.Regions = &memoryRegionStore{}
	})
	response := httptest.NewRecorder()
	handler.ServeHTTP(response, httptest.NewRequest(http.MethodGet, "/v1/version", nil))
	var document Version
	if err := json.Unmarshal(response.Body.Bytes(), &document); err != nil {
		t.Fatalf("decode: %v", err)
	}
	welcome := document.Client.Welcome
	if welcome == nil || welcome.Name != "Welcome" || welcome.GridX != nil || welcome.GridY != nil {
		t.Fatalf("unexpected welcome: %+v", welcome)
	}
}

func TestClientVersionRejectsOtherMethods(t *testing.T) {
	handler := newTestAPI(t)
	response := httptest.NewRecorder()
	handler.ServeHTTP(response, httptest.NewRequest(http.MethodPost, "/v1/version", nil))
	if response.Code != http.StatusMethodNotAllowed {
		t.Fatalf("expected 405, got %d", response.Code)
	}
}
