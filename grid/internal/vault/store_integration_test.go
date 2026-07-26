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
	t.Cleanup(func() { _, _ = db.Exec("DELETE FROM vault_blobs WHERE sha256 = $1", digest) })

	ingested, err := store.Ingest(ctx, digest, int64(len(content)), bytes.NewReader(content))
	if err != nil || ingested.SHA256 != digest || ingested.Size != int64(len(content)) {
		t.Fatalf("ingested = %#v, error = %v", ingested, err)
	}
	if ingested.IngestedAt.IsZero() {
		t.Fatalf("ingested timestamp = %v", ingested.IngestedAt)
	}

	// Ingest is idempotent and keeps the original ingest time: the same bytes
	// arriving again from another region is the normal case, not a conflict.
	repeated, err := store.Ingest(ctx, digest, int64(len(content)), bytes.NewReader(content))
	if err != nil || !repeated.IngestedAt.Equal(ingested.IngestedAt) {
		t.Fatalf("repeated = %#v, error = %v", repeated, err)
	}

	stated, err := store.Stat(ctx, digest)
	if err != nil || stated.Size != int64(len(content)) {
		t.Fatalf("stat = %#v, error = %v", stated, err)
	}

	reader, opened, err := store.Open(ctx, digest)
	if err != nil {
		t.Fatalf("open error = %v", err)
	}
	served, err := io.ReadAll(reader)
	reader.Close()
	if err != nil || !bytes.Equal(served, content) || opened.SHA256 != digest {
		t.Fatalf("served = %q, blob = %#v, error = %v", served, opened, err)
	}

	if _, err := store.Stat(ctx, digestOf([]byte("never ingested"))); !errors.Is(err, ErrNotFound) {
		t.Fatalf("stat of unknown blob = %v, want ErrNotFound", err)
	}

	// An index row whose bytes have gone is not a held blob. Reporting it as held
	// would let an inventory commit believe content is durable when it is not.
	if err := os.Remove(store.files.path(digest)); err != nil {
		t.Fatal(err)
	}
	if _, err := store.Stat(ctx, digest); !errors.Is(err, ErrNotFound) {
		t.Fatalf("stat after byte loss = %v, want ErrNotFound", err)
	}
	if _, _, err := store.Open(ctx, digest); !errors.Is(err, ErrNotFound) {
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

	content := []byte("bytes whose digest is not what the caller claimed")
	claimed := digestOf([]byte("a different blob"))
	t.Cleanup(func() { _, _ = db.Exec("DELETE FROM vault_blobs WHERE sha256 = $1", claimed) })
	if _, err := store.Ingest(ctx, claimed, int64(len(content)), bytes.NewReader(content)); !errors.Is(err, ErrMismatch) {
		t.Fatalf("mismatched ingest = %v, want ErrMismatch", err)
	}
	// A rejected ingest must not leave an index row claiming the blob is held.
	if _, err := store.Stat(ctx, claimed); !errors.Is(err, ErrNotFound) {
		t.Fatalf("stat after rejected ingest = %v, want ErrNotFound", err)
	}

	if _, err := store.Ingest(ctx, "not-a-digest", 4, bytes.NewReader([]byte("data"))); !errors.Is(err, ErrInvalid) {
		t.Fatalf("invalid digest ingest = %v, want ErrInvalid", err)
	}
	if _, err := store.Ingest(ctx, digestOf(content), 0, bytes.NewReader(content)); !errors.Is(err, ErrInvalid) {
		t.Fatalf("zero-length ingest = %v, want ErrInvalid", err)
	}
	if _, err := store.Ingest(ctx, digestOf(content), MaxBlobSize+1, bytes.NewReader(content)); !errors.Is(err, ErrInvalid) {
		t.Fatalf("oversized ingest = %v, want ErrInvalid", err)
	}
}
