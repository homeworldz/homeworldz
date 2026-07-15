package assetmeta

import (
	"context"
	"database/sql"
	"errors"
	"os"
	"testing"
	"time"

	"github.com/homeworldz/homeworldz/grid/internal/identifier"
	_ "github.com/jackc/pgx/v5/stdlib"
)

func TestPostgresAssetMetadataLifecycle(t *testing.T) {
	databaseURL := os.Getenv("HOMEWORLDZ_TEST_DATABASE_URL")
	if databaseURL == "" {
		t.Skip("HOMEWORLDZ_TEST_DATABASE_URL is not configured")
	}
	db, err := sql.Open("pgx", databaseURL)
	if err != nil {
		t.Fatal(err)
	}
	defer db.Close()
	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()
	id, _ := identifier.NewUUID()
	creator, _ := identifier.NewUUID()
	t.Cleanup(func() { _, _ = db.Exec("DELETE FROM asset_metadata WHERE asset_id = $1", id) })
	store := NewPostgresStore(db)
	input := Registration{
		ID: id, CreatorUserID: creator,
		SHA256: "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
		Size:   332, Endpoint: "http://region-one.example:42001", Origin: true,
	}
	created, err := store.Register(ctx, input)
	if err != nil || len(created.Locations) != 1 || !created.Locations[0].Origin {
		t.Fatalf("created asset = %#v, error = %v", created, err)
	}
	input.Endpoint = "http://region-two.example:42001"
	input.Origin = false
	replicated, err := store.Register(ctx, input)
	if err != nil || len(replicated.Locations) != 2 {
		t.Fatalf("replicated asset = %#v, error = %v", replicated, err)
	}
	input.Size++
	if _, err := store.Register(ctx, input); !errors.Is(err, ErrConflict) {
		t.Fatalf("conflicting asset error = %v", err)
	}
}
