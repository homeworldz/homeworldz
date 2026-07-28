// Package durability enforces the inventory-commit invariant of ADR 0026: an
// inventory item may only be committed when the vault already holds the
// verified blob for the asset it references.
//
// The Keeper answers "is this asset's blob durable, and if not, make it so".
// Making it so means fetching the bytes from a location the registry already
// records and ingesting them, which is deliberately the same act as the
// adoption backfill — one mechanism, so a grid that has been running without
// the vault repairs itself as inventory is touched rather than needing a
// separate reconciliation path.
//
// Fetching rather than waiting for a region to push is what makes the
// invariant independent of region cooperation, which ADR 0026 requires:
// nothing about durability may rest on a region choosing to write through,
// shutting down in an orderly way, or being upgraded. A region write-through
// (PUT to the vault) remains worthwhile as an optimization, since the region
// has the bytes in hand and saves the grid a round trip, but it is never the
// thing durability depends on.
package durability

import (
	"context"
	"errors"
	"fmt"
	"io"
	"net/http"
	"net/url"
	"strings"
	"time"

	"github.com/homeworldz/server/grid/internal/assetmeta"
	"github.com/homeworldz/server/grid/internal/vault"
)

var (
	// ErrUnregistered is an asset the registry has never heard of. Inventory may
	// not reference bytes the grid cannot describe.
	ErrUnregistered = errors.New("asset is not registered")
	// ErrUnfetchable is a registered asset whose bytes no recorded location will
	// serve. This is the state the grid was in before the vault: the registry
	// names origins that no longer hold the content, and nothing can recover it.
	ErrUnfetchable = errors.New("asset bytes are not available from any registered location")
	// ErrVaultUnavailable keeps an inventory write from committing when the
	// durability question cannot be answered at all. Failing closed is the
	// point: a commit that skipped the check is exactly the silent data loss
	// the vault exists to prevent.
	ErrVaultUnavailable = errors.New("asset vault is unavailable")
)

// Registry is the part of the asset registry the keeper reads.
type Registry interface {
	Get(ctx context.Context, assetID string) (assetmeta.Asset, error)
	Blob(ctx context.Context, assetID string) (assetmeta.Blob, error)
}

type Keeper struct {
	registry Registry
	vault    vault.Store
	client   *http.Client
	token    string
}

// New builds a keeper. The service token is the internal-tier credential the
// grid already shares with its regions, which is what lets the grid read an
// asset back out of the region that registered it.
func New(registry Registry, store vault.Store, serviceToken string, client *http.Client) *Keeper {
	if client == nil {
		client = &http.Client{Timeout: 30 * time.Second}
	}
	return &Keeper{registry: registry, vault: store, client: client, token: serviceToken}
}

// EnsureDurable returns nil once the vault holds the blob behind assetID.
//
// It is safe to call on every inventory write: the common case is one indexed
// lookup, and the fetch only happens the first time an asset becomes
// inventory-referenced (or the first time an old one is touched after the
// vault arrived).
func (k *Keeper) EnsureDurable(ctx context.Context, assetID string) error {
	if k == nil || k.vault == nil || k.registry == nil {
		return ErrVaultUnavailable
	}
	blob, err := k.registry.Blob(ctx, assetID)
	if errors.Is(err, assetmeta.ErrNotFound) {
		return ErrUnregistered
	} else if err != nil {
		return fmt.Errorf("read asset blob: %w", err)
	}
	switch _, err := k.vault.Held(ctx, blob.BlobID); {
	case err == nil:
		return nil
	case errors.Is(err, vault.ErrNotFound):
		// Fall through to the fetch below.
	case errors.Is(err, vault.ErrInvalid):
		return ErrUnregistered
	default:
		return fmt.Errorf("%w: %s", ErrVaultUnavailable, err)
	}
	asset, err := k.registry.Get(ctx, assetID)
	if errors.Is(err, assetmeta.ErrNotFound) {
		return ErrUnregistered
	} else if err != nil {
		return fmt.Errorf("read asset: %w", err)
	}
	// Origins first: a location that claims to hold the only copy is the one
	// most worth reading before it disappears, which is the whole failure this
	// prevents.
	locations := make([]assetmeta.Location, 0, len(asset.Locations))
	for _, location := range asset.Locations {
		if location.Origin {
			locations = append(locations, location)
		}
	}
	for _, location := range asset.Locations {
		if !location.Origin {
			locations = append(locations, location)
		}
	}
	var lastErr error
	for _, location := range locations {
		if err := k.ingestFrom(ctx, location.Endpoint, assetID, blob.BlobID); err != nil {
			lastErr = err
			continue
		}
		return nil
	}
	if lastErr != nil {
		return fmt.Errorf("%w: %s", ErrUnfetchable, lastErr)
	}
	return ErrUnfetchable
}

// ingestFrom reads an asset back out of one registered location and writes it
// through to the vault. The bytes are never trusted: the vault verifies them
// against the registry's checksum and length, so a region serving the wrong
// content fails the ingest instead of corrupting the durable copy.
func (k *Keeper) ingestFrom(ctx context.Context, endpoint, assetID, blobID string) error {
	address, err := url.Parse(strings.TrimRight(endpoint, "/") + "/api/v1/assets/" + assetID)
	if err != nil {
		return fmt.Errorf("location endpoint is unusable: %w", err)
	}
	if address.Scheme != "http" && address.Scheme != "https" {
		return fmt.Errorf("location endpoint is not http: %s", endpoint)
	}
	request, err := http.NewRequestWithContext(ctx, http.MethodGet, address.String(), nil)
	if err != nil {
		return err
	}
	if k.token != "" {
		request.Header.Set("Authorization", "Bearer "+k.token)
	}
	response, err := k.client.Do(request)
	if err != nil {
		return err
	}
	defer func() {
		// Drain briefly so the connection can be reused; a body we are
		// abandoning is not worth reading in full.
		_, _ = io.Copy(io.Discard, io.LimitReader(response.Body, 4096))
		response.Body.Close()
	}()
	if response.StatusCode != http.StatusOK {
		return fmt.Errorf("location %s answered %d", endpoint, response.StatusCode)
	}
	if _, err := k.vault.Ingest(ctx, blobID, response.Body); err != nil {
		return fmt.Errorf("ingest from %s: %w", endpoint, err)
	}
	return nil
}
