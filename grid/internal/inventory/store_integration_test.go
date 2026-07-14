package inventory

import (
	"context"
	"database/sql"
	"fmt"
	"os"
	"testing"
	"time"

	"github.com/homeworldz/homeworldz/grid/internal/identifier"
	_ "github.com/jackc/pgx/v5/stdlib"
)

func TestPostgresSystemFolderLifecycle(t *testing.T) {
	databaseURL := os.Getenv("HOMEWORLDZ_TEST_DATABASE_URL")
	if databaseURL == "" {
		t.Skip("HOMEWORLDZ_TEST_DATABASE_URL is not configured")
	}
	db, err := sql.Open("pgx", databaseURL)
	if err != nil {
		t.Fatal(err)
	}
	defer db.Close()
	ctx, cancel := context.WithTimeout(context.Background(), 15*time.Second)
	defer cancel()
	userID, err := identifier.NewUUID()
	if err != nil {
		t.Fatal(err)
	}
	if _, err := db.ExecContext(ctx, `INSERT INTO users (id, username, password_hash)
		VALUES ($1, $2, 'integration-only')`, userID, fmt.Sprintf("inventory.%d", time.Now().UnixNano())); err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { _, _ = db.Exec("DELETE FROM users WHERE id = $1", userID) })
	store := NewPostgresStore(db)
	first, err := store.EnsureSystemFolders(ctx, userID)
	if err != nil {
		t.Fatal(err)
	}
	second, err := store.EnsureSystemFolders(ctx, userID)
	if err != nil {
		t.Fatal(err)
	}
	if len(first) != 21 || len(second) != len(first) {
		t.Fatalf("folder counts = %d, %d", len(first), len(second))
	}
	itemID, _ := identifier.NewUUID()
	assetID, _ := identifier.NewUUID()
	item := Item{ID: itemID, OwnerUserID: userID, CreatorUserID: userID,
		FolderID: first[8].ID, AssetID: assetID, AssetType: 13, InventoryType: 18,
		Name: "Integration Shape", BasePermissions: 0x7fffffff, CurrentPermissions: 0x7fffffff}
	if inserted, err := store.EnsureItem(ctx, item); err != nil || !inserted {
		t.Fatalf("first ensure item inserted = %v, error = %v", inserted, err)
	}
	if inserted, err := store.EnsureItem(ctx, item); err != nil || inserted {
		t.Fatalf("second ensure item inserted = %v, error = %v", inserted, err)
	}
	replacementAssetID, _ := identifier.NewUUID()
	item.AssetID = replacementAssetID
	if updated, err := store.EnsureItem(ctx, item); err != nil || !updated {
		t.Fatalf("changed ensure item updated = %v, error = %v", updated, err)
	}
	items, err := store.ListItems(ctx, userID)
	if err != nil || len(items) != 1 || items[0].AssetID != replacementAssetID {
		t.Fatalf("inventory items = %#v, error = %v", items, err)
	}
}
