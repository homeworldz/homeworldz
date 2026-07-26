package vault

import (
	"bytes"
	"crypto/sha256"
	"encoding/hex"
	"errors"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func digestOf(content []byte) string {
	sum := sha256.Sum256(content)
	return hex.EncodeToString(sum[:])
}

func TestBlobFilesRoundTrip(t *testing.T) {
	files := blobFiles{root: t.TempDir()}
	content := []byte("homeworldz vault blob")
	digest := digestOf(content)
	if err := files.write(digest, int64(len(content)), bytes.NewReader(content)); err != nil {
		t.Fatalf("write = %v", err)
	}
	size, err := files.size(digest)
	if err != nil || size != int64(len(content)) {
		t.Fatalf("size = %d, %v", size, err)
	}
	stored, err := os.ReadFile(files.path(digest))
	if err != nil || !bytes.Equal(stored, content) {
		t.Fatalf("stored bytes = %q, %v", stored, err)
	}
	// Sharded on the first digest byte, like the region blob store.
	if filepath.Base(filepath.Dir(files.path(digest))) != digest[:2] {
		t.Fatalf("blob path = %s", files.path(digest))
	}
}

func TestBlobFilesRejectsWrongDigest(t *testing.T) {
	files := blobFiles{root: t.TempDir()}
	content := []byte("bytes that do not match")
	claimed := digestOf([]byte("something else entirely"))
	if err := files.write(claimed, int64(len(content)), bytes.NewReader(content)); !errors.Is(err, ErrMismatch) {
		t.Fatalf("write = %v, want ErrMismatch", err)
	}
	// Fail closed: the rejected bytes must not be reachable under the digest.
	if _, err := files.size(claimed); !errors.Is(err, ErrNotFound) {
		t.Fatalf("size after mismatch = %v, want ErrNotFound", err)
	}
	assertNoPartialFiles(t, files.root)
}

func TestBlobFilesRejectsShortBody(t *testing.T) {
	files := blobFiles{root: t.TempDir()}
	content := []byte("twelve bytes")
	digest := digestOf(content)
	if err := files.write(digest, int64(len(content))+8, bytes.NewReader(content)); !errors.Is(err, ErrMismatch) {
		t.Fatalf("write = %v, want ErrMismatch", err)
	}
	if _, err := files.size(digest); !errors.Is(err, ErrNotFound) {
		t.Fatalf("size after short body = %v, want ErrNotFound", err)
	}
	assertNoPartialFiles(t, files.root)
}

func TestBlobFilesRejectsLongBody(t *testing.T) {
	files := blobFiles{root: t.TempDir()}
	content := []byte("a body longer than its declared length")
	digest := digestOf(content)
	if err := files.write(digest, 4, bytes.NewReader(content)); !errors.Is(err, ErrMismatch) {
		t.Fatalf("write = %v, want ErrMismatch", err)
	}
	if _, err := files.size(digest); !errors.Is(err, ErrNotFound) {
		t.Fatalf("size after long body = %v, want ErrNotFound", err)
	}
	assertNoPartialFiles(t, files.root)
}

func TestBlobFilesWriteIsIdempotent(t *testing.T) {
	files := blobFiles{root: t.TempDir()}
	content := []byte("identical bytes ingested twice")
	digest := digestOf(content)
	for attempt := 0; attempt < 2; attempt++ {
		if err := files.write(digest, int64(len(content)), bytes.NewReader(content)); err != nil {
			t.Fatalf("write attempt %d = %v", attempt, err)
		}
	}
	size, err := files.size(digest)
	if err != nil || size != int64(len(content)) {
		t.Fatalf("size = %d, %v", size, err)
	}
	assertNoPartialFiles(t, files.root)
}

func TestValidDigest(t *testing.T) {
	valid := strings.Repeat("ab12", 16)
	cases := map[string]bool{
		valid:                      true,
		strings.ToUpper(valid):     false, // lowercase only
		strings.Repeat("ab12", 15): false, // too short
		strings.Repeat("ab12", 17): false, // too long
		strings.Repeat("gg12", 16): false, // not hexadecimal
		"":                         false,
	}
	for value, want := range cases {
		if got := ValidDigest(value); got != want {
			t.Errorf("ValidDigest(%q) = %v, want %v", value, got, want)
		}
	}
}

// assertNoPartialFiles confirms that no temporary ingest file survived, so a
// rejected or repeated write leaves nothing behind to accumulate.
func assertNoPartialFiles(t *testing.T, root string) {
	t.Helper()
	err := filepath.Walk(root, func(path string, info os.FileInfo, err error) error {
		if err != nil {
			return err
		}
		if !info.IsDir() && strings.HasSuffix(path, ".partial") {
			t.Errorf("leftover temporary file %s", path)
		}
		return nil
	})
	if err != nil {
		t.Fatalf("walk vault root: %v", err)
	}
}
