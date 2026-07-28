package durability

import (
	"bytes"
	"context"
	"crypto/sha256"
	"encoding/hex"
	"errors"
	"io"
	"net/http"
	"net/http/httptest"
	"testing"
	"time"

	"github.com/homeworldz/server/grid/internal/assetmeta"
	"github.com/homeworldz/server/grid/internal/vault"
)

const (
	assetID = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa"
	blobID  = "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb"
)

type stubRegistry struct {
	blob      assetmeta.Blob
	locations []assetmeta.Location
}

func (r *stubRegistry) Get(_ context.Context, id string) (assetmeta.Asset, error) {
	if id != assetID {
		return assetmeta.Asset{}, assetmeta.ErrNotFound
	}
	return assetmeta.Asset{ID: id, SHA256: r.blob.Checksum, Size: r.blob.ByteLength,
		Locations: r.locations}, nil
}

func (r *stubRegistry) Blob(_ context.Context, id string) (assetmeta.Blob, error) {
	if id != assetID {
		return assetmeta.Blob{}, assetmeta.ErrNotFound
	}
	return r.blob, nil
}

type stubVault struct {
	held  map[string][]byte
	blob  assetmeta.Blob
	fails bool
}

func (v *stubVault) Ingest(_ context.Context, id string, content io.Reader) (vault.Blob, error) {
	body, err := io.ReadAll(content)
	if err != nil {
		return vault.Blob{}, err
	}
	sum := sha256.Sum256(body)
	if int64(len(body)) != v.blob.ByteLength || hex.EncodeToString(sum[:]) != v.blob.Checksum {
		return vault.Blob{}, vault.ErrMismatch
	}
	v.held[id] = body
	return vault.Blob{BlobID: id, ByteLength: int64(len(body)), Checksum: v.blob.Checksum}, nil
}

func (v *stubVault) Held(_ context.Context, id string) (vault.Blob, error) {
	if v.fails {
		return vault.Blob{}, errors.New("vault index unreachable")
	}
	body, found := v.held[id]
	if !found {
		return vault.Blob{}, vault.ErrNotFound
	}
	return vault.Blob{BlobID: id, ByteLength: int64(len(body)), Checksum: v.blob.Checksum}, nil
}

func (v *stubVault) Open(ctx context.Context, id string) (io.ReadCloser, vault.Blob, error) {
	blob, err := v.Held(ctx, id)
	if err != nil {
		return nil, vault.Blob{}, err
	}
	return io.NopCloser(bytes.NewReader(v.held[id])), blob, nil
}

func newFixture(content []byte, locations ...assetmeta.Location) (*Keeper, *stubVault) {
	sum := sha256.Sum256(content)
	blob := assetmeta.Blob{BlobID: blobID, ByteLength: int64(len(content)),
		Checksum: hex.EncodeToString(sum[:]), ChecksumAlgorithm: "sha256"}
	store := &stubVault{held: make(map[string][]byte), blob: blob}
	registry := &stubRegistry{blob: blob, locations: locations}
	return New(registry, store, "secret", &http.Client{Timeout: 5 * time.Second}), store
}

func TestEnsureDurableFetchesFromARegisteredLocation(t *testing.T) {
	content := []byte("bytes a region still holds")
	var served, authorized int
	region := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path != "/api/v1/assets/"+assetID {
			w.WriteHeader(http.StatusNotFound)
			return
		}
		if r.Header.Get("Authorization") == "Bearer secret" {
			authorized++
		}
		served++
		w.Write(content)
	}))
	defer region.Close()

	keeper, store := newFixture(content, assetmeta.Location{Endpoint: region.URL, Origin: true})
	if err := keeper.EnsureDurable(context.Background(), assetID); err != nil {
		t.Fatalf("EnsureDurable = %v", err)
	}
	if !bytes.Equal(store.held[blobID], content) {
		t.Fatalf("vault holds %q", store.held[blobID])
	}
	if authorized != 1 {
		t.Fatalf("authorized fetches = %d", authorized)
	}
	// Already durable: no second fetch. This is what makes the check safe to run
	// on every inventory write.
	if err := keeper.EnsureDurable(context.Background(), assetID); err != nil {
		t.Fatalf("second EnsureDurable = %v", err)
	}
	if served != 1 {
		t.Fatalf("fetches = %d, want 1", served)
	}
}

func TestEnsureDurablePrefersOriginsAndFallsBack(t *testing.T) {
	content := []byte("bytes only the replica still serves")
	dead := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, _ *http.Request) {
		w.WriteHeader(http.StatusNotFound)
	}))
	defer dead.Close()
	var order []string
	replica := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, _ *http.Request) {
		order = append(order, "replica")
		w.Write(content)
	}))
	defer replica.Close()

	// The origin is listed second but must be tried first: a location claiming to
	// hold the only copy is the one worth reading before it disappears.
	keeper, store := newFixture(content,
		assetmeta.Location{Endpoint: replica.URL},
		assetmeta.Location{Endpoint: dead.URL, Origin: true})
	if err := keeper.EnsureDurable(context.Background(), assetID); err != nil {
		t.Fatalf("EnsureDurable = %v", err)
	}
	if len(order) != 1 || !bytes.Equal(store.held[blobID], content) {
		t.Fatalf("fetch order = %v, held = %q", order, store.held[blobID])
	}
}

func TestEnsureDurableReportsUnfetchableBytes(t *testing.T) {
	content := []byte("bytes no location still has")
	gone := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, _ *http.Request) {
		w.WriteHeader(http.StatusNotFound)
	}))
	defer gone.Close()

	// The state the grid was already in: origins recorded, bytes gone. The item
	// must be refused rather than committed against content nothing can serve.
	keeper, _ := newFixture(content, assetmeta.Location{Endpoint: gone.URL, Origin: true})
	if err := keeper.EnsureDurable(context.Background(), assetID); !errors.Is(err, ErrUnfetchable) {
		t.Fatalf("EnsureDurable = %v, want ErrUnfetchable", err)
	}

	// No locations at all is the same refusal, not a pass.
	empty, _ := newFixture(content)
	if err := empty.EnsureDurable(context.Background(), assetID); !errors.Is(err, ErrUnfetchable) {
		t.Fatalf("EnsureDurable with no locations = %v, want ErrUnfetchable", err)
	}
}

func TestEnsureDurableRefusesBytesThatDoNotMatchTheRegistry(t *testing.T) {
	content := []byte("what the registry recorded")
	liar := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, _ *http.Request) {
		w.Write([]byte("something else entirely"))
	}))
	defer liar.Close()

	// A region serving the wrong bytes fails the ingest instead of corrupting the
	// durable copy — the untrusted-region boundary of ADR 0028 holding.
	keeper, store := newFixture(content, assetmeta.Location{Endpoint: liar.URL, Origin: true})
	if err := keeper.EnsureDurable(context.Background(), assetID); !errors.Is(err, ErrUnfetchable) {
		t.Fatalf("EnsureDurable = %v, want ErrUnfetchable", err)
	}
	if _, held := store.held[blobID]; held {
		t.Fatal("vault kept bytes that did not match the registry")
	}
}

func TestEnsureDurableUnknownAssetAndUnavailableVault(t *testing.T) {
	keeper, store := newFixture([]byte("registered"))
	if err := keeper.EnsureDurable(context.Background(),
		"dddddddd-dddd-4ddd-8ddd-dddddddddddd"); !errors.Is(err, ErrUnregistered) {
		t.Fatalf("unknown asset = %v, want ErrUnregistered", err)
	}
	// A vault that cannot answer must not be read as "durable": failing closed is
	// the whole point of the invariant.
	store.fails = true
	if err := keeper.EnsureDurable(context.Background(), assetID); !errors.Is(err, ErrVaultUnavailable) {
		t.Fatalf("unavailable vault = %v, want ErrVaultUnavailable", err)
	}
	var absent *Keeper
	if err := absent.EnsureDurable(context.Background(), assetID); !errors.Is(err, ErrVaultUnavailable) {
		t.Fatalf("nil keeper = %v, want ErrVaultUnavailable", err)
	}
}
