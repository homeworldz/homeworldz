package vault

import (
	"bytes"
	"context"
	"database/sql"
	"errors"
	"io"
	"os"
	"testing"
	"time"

	_ "github.com/jackc/pgx/v5/stdlib"
)

// registerBlob inserts a blob registration, which is what the vault verifies
// ingested bytes against. The vault holds bytes for blobs the grid has
// registered; there is no such thing as ingesting an unregistered one.
func registerBlob(t *testing.T, db *sql.DB, digest string, length int64) string {
	t.Helper()
	var blobID string
	if err := db.QueryRow(`
		INSERT INTO blobs (byte_length, checksum, checksum_algorithm)
		VALUES ($1, $2, 'sha256') RETURNING blob_id`, length, digest).Scan(&blobID); err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() {
		_, _ = db.Exec("DELETE FROM vault_blobs WHERE blob_id = $1", blobID)
		_, _ = db.Exec("DELETE FROM blobs WHERE blob_id = $1", blobID)
	})
	return blobID
}

func TestPostgresVaultLifecycle(t *testing.T) {
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
	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()

	store, err := NewPostgresStore(db, t.TempDir())
	if err != nil {
		t.Fatal(err)
	}
	content := []byte("vault-authoritative inventory bytes")
	digest := digestOf(content)
	blobID := registerBlob(t, db, digest, int64(len(content)))

	ingested, err := store.Ingest(ctx, blobID, bytes.NewReader(content))
	if err != nil || ingested.BlobID != blobID || ingested.Checksum != digest ||
		ingested.ByteLength != int64(len(content)) {
		t.Fatalf("ingested = %#v, error = %v", ingested, err)
	}
	if ingested.IngestedAt.IsZero() {
		t.Fatalf("ingested timestamp = %v", ingested.IngestedAt)
	}

	// Ingest is idempotent and keeps the original ingest time: the same bytes
	// arriving again from another region is the normal case, not a conflict.
	repeated, err := store.Ingest(ctx, blobID, bytes.NewReader(content))
	if err != nil || !repeated.IngestedAt.Equal(ingested.IngestedAt) {
		t.Fatalf("repeated = %#v, error = %v", repeated, err)
	}

	held, err := store.Held(ctx, blobID)
	if err != nil || held.ByteLength != int64(len(content)) || held.Checksum != digest {
		t.Fatalf("held = %#v, error = %v", held, err)
	}

	reader, opened, err := store.Open(ctx, blobID)
	if err != nil {
		t.Fatalf("open error = %v", err)
	}
	served, err := io.ReadAll(reader)
	reader.Close()
	if err != nil || !bytes.Equal(served, content) || opened.Checksum != digest {
		t.Fatalf("served = %q, blob = %#v, error = %v", served, opened, err)
	}

	// Registered but never ingested is the state every asset starts in, and it
	// is exactly what the inventory-commit invariant must refuse.
	absent := registerBlob(t, db, digestOf([]byte("never ingested")), 14)
	if _, err := store.Held(ctx, absent); !errors.Is(err, ErrNotFound) {
		t.Fatalf("held for un-ingested blob = %v, want ErrNotFound", err)
	}

	// An index row whose bytes have gone is not a held blob. Reporting it as held
	// would let an inventory commit believe content is durable when it is not.
	if err := os.Remove(store.files.path(digest)); err != nil {
		t.Fatal(err)
	}
	if _, err := store.Held(ctx, blobID); !errors.Is(err, ErrNotFound) {
		t.Fatalf("held after byte loss = %v, want ErrNotFound", err)
	}
	if _, _, err := store.Open(ctx, blobID); !errors.Is(err, ErrNotFound) {
		t.Fatalf("open after byte loss = %v, want ErrNotFound", err)
	}
}

func TestPostgresVaultRejectsInvalidIngest(t *testing.T) {
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
	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()
	store, err := NewPostgresStore(db, t.TempDir())
	if err != nil {
		t.Fatal(err)
	}

	// Bytes that are not what the registry says the blob is. This is the check
	// that keeps a region from talking the vault into vouching for content the
	// grid never verified.
	content := []byte("bytes whose digest is not what the registry recorded")
	wrong := registerBlob(t, db, digestOf([]byte("a different blob")), int64(len(content)))
	if _, err := store.Ingest(ctx, wrong, bytes.NewReader(content)); !errors.Is(err, ErrMismatch) {
		t.Fatalf("mismatched ingest = %v, want ErrMismatch", err)
	}
	// A rejected ingest must not leave an index row claiming the blob is held.
	if _, err := store.Held(ctx, wrong); !errors.Is(err, ErrNotFound) {
		t.Fatalf("held after rejected ingest = %v, want ErrNotFound", err)
	}

	// A body that disagrees with the registered length is refused even though
	// the caller declares nothing: the length comes from the registration.
	short := registerBlob(t, db, digestOf(content), int64(len(content))+8)
	if _, err := store.Ingest(ctx, short, bytes.NewReader(content)); !errors.Is(err, ErrMismatch) {
		t.Fatalf("short ingest = %v, want ErrMismatch", err)
	}

	if _, err := store.Ingest(ctx, "not-a-uuid", bytes.NewReader([]byte("data"))); !errors.Is(err, ErrInvalid) {
		t.Fatalf("invalid blob id ingest = %v, want ErrInvalid", err)
	}
	if _, err := store.Ingest(ctx, "11111111-1111-4111-8111-111111111111",
		bytes.NewReader(content)); !errors.Is(err, ErrInvalid) {
		t.Fatalf("unregistered blob ingest = %v, want ErrInvalid", err)
	}
}
