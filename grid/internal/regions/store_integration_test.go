package regions

import (
	"context"
	"database/sql"
	"errors"
	"os"
	"testing"
	"time"

	_ "github.com/jackc/pgx/v5/stdlib"
)

func TestPostgresRegionLifecycle(t *testing.T) {
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

	store := NewPostgresStore(db)
	coordinate := int(time.Now().UnixNano() % 1_000_000_000)
	created, err := store.Register(ctx, Registration{
		Name: "Integration Region", GridX: coordinate, GridY: coordinate,
		PublicEndpoint: "http://localhost:42001", LeaseDuration: 60 * time.Second,
	})
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { _, _ = db.Exec("DELETE FROM regions WHERE id = $1", created.ID) })

	if _, err := store.Register(ctx, Registration{
		Name: "Conflict", GridX: coordinate, GridY: coordinate,
		PublicEndpoint: "http://localhost:42002", LeaseDuration: 60 * time.Second,
	}); !errors.Is(err, ErrConflict) {
		t.Fatalf("coordinate conflict error = %v, want ErrConflict", err)
	}
	renewed, err := store.Renew(ctx, created.ID, 120*time.Second)
	if err != nil || !renewed.LeaseExpiresAt.After(created.LeaseExpiresAt) {
		t.Fatalf("renewed region = %#v, error = %v", renewed, err)
	}
	if _, err := store.Get(ctx, created.ID); err != nil {
		t.Fatal(err)
	}
	items, err := store.List(ctx)
	if err != nil {
		t.Fatal(err)
	}
	found := false
	for _, item := range items {
		found = found || item.ID == created.ID
	}
	if !found {
		t.Fatal("registered region was not discoverable")
	}
	if _, err := db.ExecContext(ctx, "UPDATE regions SET lease_expires_at = now() - interval '1 second' WHERE id = $1", created.ID); err != nil {
		t.Fatal(err)
	}
	if _, err := store.Get(ctx, created.ID); !errors.Is(err, ErrNotFound) {
		t.Fatalf("expired region error = %v, want ErrNotFound", err)
	}
	replacement, err := store.Register(ctx, Registration{
		Name: "Replacement", GridX: coordinate, GridY: coordinate,
		PublicEndpoint: "http://localhost:42003", LeaseDuration: 60 * time.Second,
	})
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { _, _ = db.Exec("DELETE FROM regions WHERE id = $1", replacement.ID) })
	if err := store.Deregister(ctx, replacement.ID); err != nil {
		t.Fatal(err)
	}
}
