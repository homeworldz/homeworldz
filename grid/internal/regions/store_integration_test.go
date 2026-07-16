package regions

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
	provisionedID, err := identifier.NewUUID()
	if err != nil {
		t.Fatal(err)
	}
	provisioned, err := store.RegisterProvisioned(ctx, provisionedID, Registration{
		Name: "Provisioned Integration Region", GridX: coordinate + 1, GridY: coordinate + 1,
		PublicEndpoint: "http://localhost:42101", LeaseDuration: time.Minute,
	})
	if err != nil {
		t.Fatal(err)
	}
	userID, err := identifier.NewUUID()
	if err != nil {
		t.Fatal(err)
	}
	if _, err := db.ExecContext(ctx, `INSERT INTO users(id, username, password_hash, last_region_id)
		VALUES($1, $2, 'not-a-login-password', $3)`, userID,
		fmt.Sprintf("region-reference.%d", time.Now().UnixNano()), provisioned.ID); err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() {
		_, _ = db.Exec("DELETE FROM users WHERE id = $1", userID)
		_, _ = db.Exec("DELETE FROM regions WHERE id = $1", provisioned.ID)
	})
	if _, err := db.ExecContext(ctx,
		"UPDATE regions SET lease_expires_at = now() - interval '1 second' WHERE id = $1",
		provisioned.ID); err != nil {
		t.Fatal(err)
	}
	if _, err := store.RegisterProvisioned(ctx, provisioned.ID, Registration{
		Name: provisioned.Name, GridX: provisioned.GridX, GridY: provisioned.GridY,
		PublicEndpoint: provisioned.PublicEndpoint, ViewerPort: provisioned.ViewerPort,
		LeaseDuration: time.Minute,
	}); err != nil {
		t.Fatal(err)
	}
	var retainedRegionID string
	if err := db.QueryRowContext(ctx, "SELECT last_region_id FROM users WHERE id = $1", userID).
		Scan(&retainedRegionID); err != nil || retainedRegionID != provisioned.ID {
		t.Fatalf("retained last region = %q, error = %v", retainedRegionID, err)
	}
	if _, err := db.ExecContext(ctx, "UPDATE regions SET provisioned = false WHERE id = $1", provisioned.ID); err != nil {
		t.Fatal(err)
	}
	if _, err := store.RenewProvisioned(ctx, provisioned.ID, time.Minute); err != nil {
		t.Fatal(err)
	}
	var markedProvisioned bool
	if err := db.QueryRowContext(ctx, "SELECT provisioned FROM regions WHERE id = $1", provisioned.ID).
		Scan(&markedProvisioned); err != nil || !markedProvisioned {
		t.Fatalf("provisioned renewal marker = %t, error = %v", markedProvisioned, err)
	}
	if err := store.DeregisterProvisioned(ctx, provisioned.ID); err != nil {
		t.Fatal(err)
	}
	if _, err := store.Get(ctx, provisioned.ID); !errors.Is(err, ErrNotFound) {
		t.Fatalf("deactivated provisioned region error = %v, want ErrNotFound", err)
	}
	if err := db.QueryRowContext(ctx,
		"SELECT last_region_id FROM users WHERE id = $1", userID).Scan(&retainedRegionID); err != nil || retainedRegionID != provisioned.ID {
		t.Fatalf("last region after deactivation = %q, error = %v", retainedRegionID, err)
	}

	created, err := store.Register(ctx, Registration{
		Name: "Integration Region", GridX: coordinate, GridY: coordinate,
		PublicEndpoint: "http://localhost:42001", LeaseDuration: 60 * time.Second,
	})
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { _, _ = db.Exec("DELETE FROM regions WHERE id = $1", created.ID) })

	reconnected, err := store.Register(ctx, Registration{
		Name: "Integration Region", GridX: coordinate, GridY: coordinate,
		PublicEndpoint: "http://localhost:42001", LeaseDuration: 90 * time.Second,
	})
	if err != nil || reconnected.ID != created.ID || !reconnected.LeaseExpiresAt.After(created.LeaseExpiresAt) {
		t.Fatalf("reconnected region = %#v, error = %v", reconnected, err)
	}

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
