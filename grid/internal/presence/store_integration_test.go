package presence

import (
	"context"
	"database/sql"
	"errors"
	"fmt"
	"os"
	"testing"
	"time"

	"github.com/homeworldz/homeworldz/grid/internal/identity"
	"github.com/homeworldz/homeworldz/grid/internal/regions"
	_ "github.com/jackc/pgx/v5/stdlib"
)

func TestPostgresPresenceLifecycle(t *testing.T) {
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
	nonce := time.Now().UnixNano()
	user, err := identity.NewPostgresStore(db).CreateUser(ctx, fmt.Sprintf("presence.%d", nonce), "integration-password")
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { _, _ = db.Exec("DELETE FROM users WHERE id = $1", user.ID) })
	region, err := regions.NewPostgresStore(db).Register(ctx, regions.Registration{
		Name: "Presence Region", GridX: int(nonce % 1_000_000_000), GridY: int(nonce % 1_000_000_000),
		PublicEndpoint: "http://localhost:42001", LeaseDuration: time.Minute,
	})
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { _, _ = db.Exec("DELETE FROM regions WHERE id = $1", region.ID) })
	store := NewPostgresStore(db)
	if _, err := store.Update(ctx, user.ID, region.ID); err != nil {
		t.Fatal(err)
	}
	if _, err := store.Get(ctx, user.ID); err != nil {
		t.Fatal(err)
	}
	values, err := store.List(ctx)
	if err != nil {
		t.Fatal(err)
	}
	found := false
	for _, value := range values {
		found = found || value.UserID == user.ID
	}
	if !found {
		t.Fatal("updated presence was not discoverable")
	}
	if _, err := db.ExecContext(ctx, "UPDATE presence SET last_seen_at = now() - interval '2 minutes' WHERE user_id = $1", user.ID); err != nil {
		t.Fatal(err)
	}
	if _, err := store.Get(ctx, user.ID); !errors.Is(err, ErrNotFound) {
		t.Fatalf("stale presence error = %v", err)
	}
	if _, err := store.List(ctx); err != nil {
		t.Fatal(err)
	}
	var count int
	if err := db.QueryRowContext(ctx, "SELECT count(*) FROM presence WHERE user_id = $1", user.ID).Scan(&count); err != nil {
		t.Fatal(err)
	}
	if count != 0 {
		t.Fatal("stale presence was not deleted")
	}
}
