// Package vault implements the grid-side asset vault of ADR 0026: a durable,
// replica-only blob store holding the bytes behind every inventory-referenced
// asset, so a user's inventory survives the permanent loss of any region.
//
// Three properties the rest of the grid depends on:
//
//   - It is replica-only. The vault never originates an asset, never assigns a
//     viewer-facing UUID, and never hosts an agent. Bytes only ever arrive here
//     as a copy of content a region already holds.
//   - It is never in the viewer data path. Viewers fetch asset bytes from the
//     region they are connected to; the vault serves regions, over the internal
//     service-token boundary, and nothing else.
//   - It fails closed. Bytes are verified against their declared digest and
//     length before they become reachable, so the vault never serves content
//     that does not match its address.
//
// Blobs are keyed by lowercase SHA-256, matching the region blob stores of
// ADR 0014. ADR 0027 moves blob identity to a grid-assigned blob_id with the
// digest retained as the integrity checksum; that re-keys this store rather than
// changing what it does.
package vault

import (
	"context"
	"crypto/sha256"
	"database/sql"
	"encoding/hex"
	"errors"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"time"
)

// MaxBlobSize bounds a single ingest so a malformed or hostile declared length
// cannot fill the vault filesystem. It is far above any expected texture, mesh,
// or animation asset.
const MaxBlobSize = 64 << 20

var (
	ErrNotFound = errors.New("vault does not hold the blob")
	ErrMismatch = errors.New("blob bytes do not match the declared digest or length")
	ErrInvalid  = errors.New("blob digest or length is invalid")
)

// Blob is a blob the vault durably holds.
type Blob struct {
	SHA256     string    `json:"sha256"`
	Size       int64     `json:"size"`
	IngestedAt time.Time `json:"ingestedAt"`
}

type Store interface {
	// Ingest durably stores size bytes read from content under digest, verifying
	// both before the blob becomes reachable. It is idempotent: ingesting a blob
	// the vault already holds succeeds and reports the existing record.
	Ingest(ctx context.Context, digest string, size int64, content io.Reader) (Blob, error)
	// Stat reports a blob the vault holds, or ErrNotFound.
	Stat(ctx context.Context, digest string) (Blob, error)
	// Open returns the bytes of a blob the vault holds. The caller closes them.
	Open(ctx context.Context, digest string) (io.ReadCloser, Blob, error)
}

// PostgresStore keeps blob bytes on a local filesystem tree and indexes them in
// PostgreSQL. The index is what the inventory-commit invariant of ADR 0026 will
// consult, so it must never claim a blob the filesystem cannot produce; Stat
// therefore confirms both.
type PostgresStore struct {
	files blobFiles
	db    *sql.DB
}

// NewPostgresStore prepares the vault rooted at directory. A grid that cannot
// open its vault must not start: once the ADR 0026 invariant is enforced, an
// unavailable vault has to fail inventory writes rather than silently skip
// durability.
func NewPostgresStore(db *sql.DB, directory string) (*PostgresStore, error) {
	if db == nil {
		return nil, errors.New("vault requires a database connection")
	}
	root, err := filepath.Abs(directory)
	if err != nil {
		return nil, fmt.Errorf("resolve vault directory: %w", err)
	}
	if err := os.MkdirAll(root, 0o755); err != nil {
		return nil, fmt.Errorf("create vault directory: %w", err)
	}
	return &PostgresStore{files: blobFiles{root: root}, db: db}, nil
}

// Directory reports the resolved filesystem root, for startup logging.
func (s *PostgresStore) Directory() string { return s.files.root }

func (s *PostgresStore) Ingest(ctx context.Context, digest string, size int64,
	content io.Reader) (Blob, error) {
	if !ValidDigest(digest) || size <= 0 || size > MaxBlobSize {
		return Blob{}, ErrInvalid
	}
	// Bytes are published before the index row is written. A crash between the
	// two leaves an unindexed file, which a later ingest simply replaces; the
	// reverse order would leave the index claiming bytes the vault cannot serve,
	// and that claim is exactly what inventory commits will trust.
	if err := s.files.write(digest, size, content); err != nil {
		return Blob{}, err
	}
	var blob Blob
	// A matching digest means identical bytes, so a re-ingest has nothing to
	// reconcile. The no-op update exists only so RETURNING still yields the
	// existing row, which a bare DO NOTHING would not.
	if err := s.db.QueryRowContext(ctx, `
		INSERT INTO vault_blobs (sha256, size) VALUES ($1, $2)
		ON CONFLICT (sha256) DO UPDATE SET size = vault_blobs.size
		RETURNING sha256, size, ingested_at`, digest, size).
		Scan(&blob.SHA256, &blob.Size, &blob.IngestedAt); err != nil {
		return Blob{}, fmt.Errorf("index vault blob: %w", err)
	}
	return blob, nil
}

func (s *PostgresStore) Stat(ctx context.Context, digest string) (Blob, error) {
	if !ValidDigest(digest) {
		return Blob{}, ErrInvalid
	}
	var blob Blob
	if err := s.db.QueryRowContext(ctx,
		"SELECT sha256, size, ingested_at FROM vault_blobs WHERE sha256 = $1", digest).
		Scan(&blob.SHA256, &blob.Size, &blob.IngestedAt); errors.Is(err, sql.ErrNoRows) {
		return Blob{}, ErrNotFound
	} else if err != nil {
		return Blob{}, fmt.Errorf("read vault blob index: %w", err)
	}
	// An index row whose bytes are missing or truncated is not a held blob. This
	// costs one stat call and keeps the durability answer honest.
	stored, err := s.files.size(digest)
	if errors.Is(err, ErrNotFound) {
		return Blob{}, ErrNotFound
	} else if err != nil {
		return Blob{}, err
	}
	if stored != blob.Size {
		return Blob{}, ErrNotFound
	}
	return blob, nil
}

func (s *PostgresStore) Open(ctx context.Context, digest string) (io.ReadCloser, Blob, error) {
	blob, err := s.Stat(ctx, digest)
	if err != nil {
		return nil, Blob{}, err
	}
	// No digest recompute on read. The vault trusts its own storage layer as any
	// storage layer is trusted (ADR 0026); verification concentrates at the
	// untrusted boundary, where a fetching region checks these bytes against the
	// grid-recorded checksum (ADR 0027, ADR 0028). This is deliberately unlike
	// the region blob store, which re-hashes because it serves viewers directly.
	file, err := os.Open(s.files.path(digest))
	if errors.Is(err, os.ErrNotExist) {
		return nil, Blob{}, ErrNotFound
	} else if err != nil {
		return nil, Blob{}, fmt.Errorf("open vault blob: %w", err)
	}
	return file, blob, nil
}

// blobFiles stores blob bytes sharded by the first digest byte, the same layout
// the region blob store uses.
type blobFiles struct{ root string }

func (f blobFiles) path(digest string) string {
	return filepath.Join(f.root, digest[:2], digest[2:])
}

// write streams content into the shard for digest, verifying the bytes against
// the declared digest and length before publishing them. Verification happens on
// a temporary file in the destination directory, so bytes that fail it are never
// reachable under the digest and the publish itself is an atomic rename.
func (f blobFiles) write(digest string, declared int64, content io.Reader) error {
	shard := filepath.Join(f.root, digest[:2])
	if err := os.MkdirAll(shard, 0o755); err != nil {
		return fmt.Errorf("create vault shard: %w", err)
	}
	temporary, err := os.CreateTemp(shard, digest[2:]+".*.partial")
	if err != nil {
		return fmt.Errorf("create vault temporary file: %w", err)
	}
	name := temporary.Name()
	published := false
	defer func() {
		temporary.Close()
		if !published {
			os.Remove(name)
		}
	}()
	hasher := sha256.New()
	// Reading one byte past the declared length is enough to detect an over-long
	// body without writing an unbounded amount of it to disk.
	written, err := io.Copy(io.MultiWriter(temporary, hasher),
		io.LimitReader(content, declared+1))
	if err != nil {
		return fmt.Errorf("write vault blob: %w", err)
	}
	if written != declared || hex.EncodeToString(hasher.Sum(nil)) != digest {
		return ErrMismatch
	}
	if err := temporary.Sync(); err != nil {
		return fmt.Errorf("flush vault blob: %w", err)
	}
	if err := temporary.Close(); err != nil {
		return fmt.Errorf("close vault blob: %w", err)
	}
	if err := os.Rename(name, f.path(digest)); err != nil {
		return fmt.Errorf("publish vault blob: %w", err)
	}
	published = true
	return nil
}

// size reports the stored length of a blob, or ErrNotFound.
func (f blobFiles) size(digest string) (int64, error) {
	info, err := os.Stat(f.path(digest))
	if errors.Is(err, os.ErrNotExist) {
		return 0, ErrNotFound
	} else if err != nil {
		return 0, fmt.Errorf("stat vault blob: %w", err)
	}
	return info.Size(), nil
}

// ValidDigest reports whether value is a lowercase hexadecimal SHA-256.
func ValidDigest(value string) bool {
	if len(value) != 64 {
		return false
	}
	for _, character := range value {
		if !((character >= '0' && character <= '9') || (character >= 'a' && character <= 'f')) {
			return false
		}
	}
	return true
}
