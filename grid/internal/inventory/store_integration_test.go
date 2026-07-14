package inventory

import (
	"context"
	"database/sql"
	"errors"
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
	customFolderID, _ := identifier.NewUUID()
	custom, err := store.CreateFolder(ctx, Folder{ID: customFolderID, OwnerUserID: userID,
		ParentID: first[0].ID, Name: "Integration Projects", TypeDefault: -1})
	if err != nil || custom.Version != 1 {
		t.Fatalf("create custom folder = %#v, error = %v", custom, err)
	}
	if _, err := store.CreateFolder(ctx, custom); !errors.Is(err, ErrFolderConflict) {
		t.Fatalf("duplicate custom folder error = %v", err)
	}
	custom.Name = "Renamed Integration Projects"
	custom, err = store.UpdateFolder(ctx, custom)
	if err != nil || custom.Version != 2 || custom.Name != "Renamed Integration Projects" {
		t.Fatalf("update custom folder = %#v, error = %v", custom, err)
	}
	itemID, _ := identifier.NewUUID()
	assetID, _ := identifier.NewUUID()
	item := Item{ID: itemID, OwnerUserID: userID, CreatorUserID: userID,
		FolderID: custom.ID, AssetID: assetID, AssetType: 13, InventoryType: 18,
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
	uploadedItemID, _ := identifier.NewUUID()
	uploadedAssetID, _ := identifier.NewUUID()
	uploaded := Item{ID: uploadedItemID, OwnerUserID: userID, CreatorUserID: userID,
		FolderID: SystemFolderID(userID, 0), AssetID: uploadedAssetID,
		AssetType: 0, InventoryType: 0, Name: "Uploaded Texture",
		BasePermissions: 0x7fffffff, CurrentPermissions: 0x7fffffff,
		NextPermissions: 0x7fffffff}
	if created, err := store.CreateItem(ctx, uploaded); err != nil || created.CreatedAt.IsZero() {
		t.Fatalf("create uploaded item = %#v, error = %v", created, err)
	}
	if _, err := store.CreateItem(ctx, uploaded); !errors.Is(err, ErrItemConflict) {
		t.Fatalf("duplicate uploaded item error = %v", err)
	}
	items, err := store.ListItems(ctx, userID)
	if err != nil || len(items) != 2 {
		t.Fatalf("inventory items = %#v, error = %v", items, err)
	}
	found := map[string]bool{}
	for _, listed := range items {
		found[listed.AssetID] = true
	}
	if !found[uploadedAssetID] || !found[replacementAssetID] {
		t.Fatalf("inventory assets = %#v", found)
	}
}
