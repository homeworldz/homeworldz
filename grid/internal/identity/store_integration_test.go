package identity

import (
	"context"
	"crypto/md5"
	"database/sql"
	"encoding/hex"
	"errors"
	"fmt"
	"os"
	"strings"
	"testing"
	"time"

	"github.com/homeworldz/server/grid/internal/identifier"
	_ "github.com/jackc/pgx/v5/stdlib"
)

// Values this test reserves for itself. Grid coordinates and viewer circuit
// codes are both unique-constrained, so the test owns a fixed, recognizable pair
// and clears it before use rather than picking fresh values each run. The
// coordinate sits far from real allocations — Welcome is at 1000,1000 — so a test
// region can never shadow or displace a live one.
const (
	testRegionName        = "Circuit Test"
	testGridCoordinate    = 3456
	testViewerCircuitCode = 123456
)

// clearCircuitTestRows removes whatever an earlier run of this test left behind.
// Cleanups cannot run when a run is killed, and a single surviving row then wedges
// every later run against a unique constraint, so the test starts by reclaiming
// the values it is about to insert rather than trusting the database to be clean.
func clearCircuitTestRows(ctx context.Context, t *testing.T, db *sql.DB) {
	t.Helper()
	// Releasing the circuit code rather than deleting the session keeps this
	// narrow: the code is what is constrained, and any session still holding it
	// belongs to a test user whose own row is cleaned up separately.
	if _, err := db.ExecContext(ctx,
		"UPDATE sessions SET viewer_circuit_code = NULL WHERE viewer_circuit_code = $1",
		testViewerCircuitCode); err != nil {
		t.Fatalf("release reserved viewer circuit code: %v", err)
	}
	// Sessions reference a region with ON DELETE SET NULL, so removing a stale
	// test region cannot orphan a session row.
	if _, err := db.ExecContext(ctx,
		"DELETE FROM regions WHERE name = $1 OR (grid_x = $2 AND grid_y = $2)",
		testRegionName, testGridCoordinate); err != nil {
		t.Fatalf("remove stale test region: %v", err)
	}
}

func TestPostgresIdentityLifecycle(t *testing.T) {
	databaseURL := os.Getenv("HOMEWORLDZ_TEST_DATABASE_URL")
	if databaseURL == "" {
		t.Skip("HOMEWORLDZ_TEST_DATABASE_URL is not configured")
	}
	db, err := sql.Open("pgx", databaseURL)
	if err != nil {
		t.Fatal(err)
	}
	// Registered before any row cleanup so it runs last: t.Cleanup is
	// last-in-first-out, and a deferred close would instead run before every
	// cleanup below, leaving them to fail silently against a closed pool.
	t.Cleanup(func() { _ = db.Close() })
	ctx, cancel := context.WithTimeout(context.Background(), 15*time.Second)
	defer cancel()
	store := NewPostgresStore(db)
	username := fmt.Sprintf("integration.%d", time.Now().UnixNano())
	user, err := store.CreateUser(ctx, username, "integration-password")
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { _, _ = db.Exec("DELETE FROM users WHERE id = $1", user.ID) })
	if _, err := store.CreateUser(ctx, username, "integration-password"); !errors.Is(err, ErrConflict) {
		t.Fatalf("duplicate user error = %v, want ErrConflict", err)
	}
	if _, err := store.CreateSession(ctx, username, "wrong-password", time.Hour); !errors.Is(err, ErrInvalidCredentials) {
		t.Fatalf("invalid login error = %v, want ErrInvalidCredentials", err)
	}
	session, err := store.CreateSession(ctx, username, "integration-password", time.Hour)
	if err != nil {
		t.Fatal(err)
	}
	if validated, err := store.ValidateSession(ctx, session.ID); err != nil || validated.UserID != user.ID || validated.SecureID == "" {
		t.Fatalf("validated session = %#v, error = %v", validated, err)
	}
	viewerDigest := md5.Sum([]byte("integration-password"))
	viewerSession, err := store.CreateViewerSession(ctx, username, hex.EncodeToString(viewerDigest[:]), time.Hour)
	if err != nil || viewerSession.UserID != user.ID || viewerSession.SecureID == "" {
		t.Fatalf("viewer session = %#v, error = %v", viewerSession, err)
	}
	if _, err := store.CreateViewerSession(ctx, username, strings.Repeat("0", 32), time.Hour); !errors.Is(err, ErrInvalidCredentials) {
		t.Fatalf("invalid viewer login error = %v, want ErrInvalidCredentials", err)
	}
	regionID, err := identifier.NewUUID()
	if err != nil {
		t.Fatalf("create destination region ID: %v", err)
	}
	clearCircuitTestRows(ctx, t, db)
	if _, err := db.ExecContext(ctx, `INSERT INTO regions
		(id, name, grid_x, grid_y, public_endpoint, lease_expires_at)
		VALUES ($1, $2, $3, $3, 'http://127.0.0.1:42001', now() + interval '1 minute')`,
		regionID, testRegionName, testGridCoordinate); err != nil {
		t.Fatalf("insert destination region: %v", err)
	}
	t.Cleanup(func() { _, _ = db.Exec("DELETE FROM regions WHERE id = $1", regionID) })
	if err := store.AssignViewerDestination(ctx, viewerSession.ID, testViewerCircuitCode, regionID); err != nil {
		t.Fatalf("assign viewer destination: %v", err)
	}
	validatedViewer, err := store.ValidateSession(ctx, viewerSession.ID)
	if err != nil || validatedViewer.ViewerCircuitCode != testViewerCircuitCode ||
		validatedViewer.DestinationRegionID != regionID {
		t.Fatalf("validated viewer destination = %#v, error = %v", validatedViewer, err)
	}
	if _, err := db.ExecContext(ctx, "UPDATE sessions SET expires_at = now() - interval '1 second' WHERE id = $1", session.ID); err != nil {
		t.Fatal(err)
	}
	if _, err := store.ValidateSession(ctx, session.ID); !errors.Is(err, ErrSessionNotFound) {
		t.Fatalf("expired session error = %v, want ErrSessionNotFound", err)
	}
	second, err := store.CreateSession(ctx, username, "integration-password", time.Hour)
	if err != nil {
		t.Fatal(err)
	}
	if err := store.RevokeSession(ctx, second.ID); err != nil {
		t.Fatal(err)
	}
	if _, err := store.ValidateSession(ctx, second.ID); !errors.Is(err, ErrSessionNotFound) {
		t.Fatalf("revoked session error = %v, want ErrSessionNotFound", err)
	}
}

func TestPostgresUpsertUser(t *testing.T) {
	databaseURL := os.Getenv("HOMEWORLDZ_TEST_DATABASE_URL")
	if databaseURL == "" {
		t.Skip("HOMEWORLDZ_TEST_DATABASE_URL is not configured")
	}
	db, err := sql.Open("pgx", databaseURL)
	if err != nil {
		t.Fatal(err)
	}
	// Registered before any row cleanup so it runs last: t.Cleanup is
	// last-in-first-out, and a deferred close would instead run before every
	// cleanup below, leaving them to fail silently against a closed pool.
	t.Cleanup(func() { _ = db.Close() })
	ctx, cancel := context.WithTimeout(context.Background(), 15*time.Second)
	defer cancel()
	store := NewPostgresStore(db)
	username := fmt.Sprintf("upsert.%d", time.Now().UnixNano())

	created, wasCreated, err := store.UpsertUser(ctx, username, "first-password")
	if err != nil || !wasCreated {
		t.Fatalf("first upsert created=%v error=%v", wasCreated, err)
	}
	t.Cleanup(func() { _, _ = db.Exec("DELETE FROM users WHERE id = $1", created.ID) })
	// Both hashes work: web (bcrypt) session and viewer (MD5) session.
	if _, err := store.CreateSession(ctx, username, "first-password", time.Hour); err != nil {
		t.Fatalf("web login after create: %v", err)
	}
	firstDigest := md5.Sum([]byte("first-password"))
	if _, err := store.CreateViewerSession(ctx, username, hex.EncodeToString(firstDigest[:]), time.Hour); err != nil {
		t.Fatalf("viewer login after create: %v", err)
	}

	// Upserting the same username replaces the hashes and keeps the same id.
	updated, wasCreated, err := store.UpsertUser(ctx, username, "second-password")
	if err != nil || wasCreated {
		t.Fatalf("second upsert created=%v error=%v", wasCreated, err)
	}
	if updated.ID != created.ID {
		t.Fatalf("upsert changed id: %s -> %s", created.ID, updated.ID)
	}
	if _, err := store.CreateSession(ctx, username, "first-password", time.Hour); !errors.Is(err, ErrInvalidCredentials) {
		t.Fatalf("old web password should be rejected, got %v", err)
	}
	if _, err := store.CreateSession(ctx, username, "second-password", time.Hour); err != nil {
		t.Fatalf("new web password should work: %v", err)
	}
	secondDigest := md5.Sum([]byte("second-password"))
	if _, err := store.CreateViewerSession(ctx, username, hex.EncodeToString(secondDigest[:]), time.Hour); err != nil {
		t.Fatalf("new viewer password should work: %v", err)
	}
}
