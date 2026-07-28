// Command vaultbackfill is the one-time adoption pass ADR 0026 requires: it
// walks every asset an inventory item references, makes each one durable in the
// vault, and reports the ones that are already unfetchable.
//
// It exists because durability was added to a grid that had been running
// without it. Assets created before the vault live only wherever the region
// that made them put them, and the registry cheerfully names origins that no
// longer serve the bytes. This pass turns that unknown into a list: what is now
// safe, and what was already lost and must be recreated.
//
// It is safe to run repeatedly. Assets the vault already holds cost one indexed
// lookup and no transfer, so a second run is a fast audit of the first.
package main

import (
	"context"
	"database/sql"
	"errors"
	"flag"
	"fmt"
	"net/http"
	"os"
	"sort"
	"time"

	"github.com/homeworldz/server/grid/internal/assetmeta"
	"github.com/homeworldz/server/grid/internal/config"
	"github.com/homeworldz/server/grid/internal/durability"
	"github.com/homeworldz/server/grid/internal/vault"
	_ "github.com/jackc/pgx/v5/stdlib"
)

// referencedAssets lists the assets inventory depends on. Links are excluded
// because a link's asset_id names the item it points at rather than any bytes.
const referencedAssets = `
	SELECT item.asset_id,
	       min(item.name) AS name,
	       count(*) AS items,
	       count(DISTINCT item.owner_user_id) AS owners
	FROM inventory_items AS item
	WHERE item.asset_type NOT IN (24, 25)
	  AND item.asset_id <> '00000000-0000-0000-0000-000000000000'
	GROUP BY item.asset_id
	ORDER BY min(item.name)`

type reference struct {
	assetID string
	name    string
	items   int
	owners  int
}

func main() {
	configDirectory := flag.String("config", "config", "directory containing db.ini and grid.ini")
	timeout := flag.Duration("timeout", 30*time.Minute, "overall time budget")
	dryRun := flag.Bool("dry-run", false, "report what is durable without fetching anything")
	flag.Parse()

	settings, err := config.LoadGrid(*configDirectory)
	if err != nil {
		fail("load configuration: %v", err)
	}
	if settings.DatabaseURL == "" {
		fail("no database url configured in %s/db.ini", *configDirectory)
	}
	db, err := sql.Open("pgx", settings.DatabaseURL)
	if err != nil {
		fail("open database: %v", err)
	}
	defer db.Close()

	assetVault, err := vault.NewPostgresStore(db, settings.VaultPath)
	if err != nil {
		fail("open vault: %v", err)
	}
	fmt.Printf("vault: %s\n", assetVault.Directory())

	ctx, cancel := context.WithTimeout(context.Background(), *timeout)
	defer cancel()

	references, err := listReferences(ctx, db)
	if err != nil {
		fail("list inventory-referenced assets: %v", err)
	}
	fmt.Printf("inventory references %d distinct assets\n\n", len(references))

	registry := assetmeta.NewPostgresStore(db)
	keeper := durability.New(registry, assetVault, settings.ServiceToken,
		&http.Client{Timeout: 60 * time.Second})

	var durable, ingested, lost, unregistered, failed int
	var lostItems []reference
	for _, item := range references {
		if *dryRun {
			blob, err := registry.Blob(ctx, item.assetID)
			if errors.Is(err, assetmeta.ErrNotFound) {
				unregistered++
				lostItems = append(lostItems, item)
				continue
			} else if err != nil {
				failed++
				continue
			}
			if _, err := assetVault.Held(ctx, blob.BlobID); err == nil {
				durable++
			} else {
				lost++
				lostItems = append(lostItems, item)
			}
			continue
		}
		// Held-before tells apart "was already safe" from "this run saved it",
		// which is the number that says whether the pass did anything.
		before := heldAlready(ctx, registry, assetVault, item.assetID)
		switch err := keeper.EnsureDurable(ctx, item.assetID); {
		case err == nil && before:
			durable++
		case err == nil:
			ingested++
			fmt.Printf("ingested  %s  %s\n", item.assetID, item.name)
		case errors.Is(err, durability.ErrUnregistered):
			unregistered++
			lostItems = append(lostItems, item)
		case errors.Is(err, durability.ErrUnfetchable):
			lost++
			lostItems = append(lostItems, item)
		default:
			failed++
			fmt.Printf("error     %s  %s: %v\n", item.assetID, item.name, err)
		}
	}

	fmt.Printf("\nalready durable: %d\ningested now:    %d\nunfetchable:     %d\nunregistered:    %d\nerrors:          %d\n",
		durable, ingested, lost, unregistered, failed)
	if len(lostItems) > 0 {
		// Named, not just counted: every one of these is content a user will
		// find broken, and the only remedy is recreating it.
		fmt.Printf("\nassets no location will serve (%d) — these are lost and must be recreated:\n", len(lostItems))
		sort.Slice(lostItems, func(i, j int) bool { return lostItems[i].name < lostItems[j].name })
		for _, item := range lostItems {
			fmt.Printf("  %s  %-40s  %d item(s), %d owner(s)\n",
				item.assetID, item.name, item.items, item.owners)
		}
	}
	if failed > 0 {
		os.Exit(1)
	}
}

func heldAlready(ctx context.Context, registry assetmeta.Store, store vault.Store, assetID string) bool {
	blob, err := registry.Blob(ctx, assetID)
	if err != nil {
		return false
	}
	_, err = store.Held(ctx, blob.BlobID)
	return err == nil
}

func listReferences(ctx context.Context, db *sql.DB) ([]reference, error) {
	rows, err := db.QueryContext(ctx, referencedAssets)
	if err != nil {
		return nil, err
	}
	defer rows.Close()
	var references []reference
	for rows.Next() {
		var item reference
		if err := rows.Scan(&item.assetID, &item.name, &item.items, &item.owners); err != nil {
			return nil, err
		}
		references = append(references, item)
	}
	return references, rows.Err()
}

func fail(format string, arguments ...any) {
	fmt.Fprintf(os.Stderr, format+"\n", arguments...)
	os.Exit(1)
}
