package httpapi

import (
	"bytes"
	"context"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"io"
	"net/http"
	"net/http/httptest"
	"strconv"
	"testing"
	"time"

	"github.com/homeworldz/homeworldz/grid/internal/vault"
)

// memoryVault is an in-memory stand-in that keeps the real store's verification
// and idempotency behavior, so handler tests exercise the same outcomes.
type memoryVault struct{ blobs map[string][]byte }

func (v *memoryVault) Ingest(_ context.Context, digest string, size int64,
	content io.Reader) (vault.Blob, error) {
	if !vault.ValidDigest(digest) || size <= 0 || size > vault.MaxBlobSize {
		return vault.Blob{}, vault.ErrInvalid
	}
	body, err := io.ReadAll(content)
	if err != nil {
		return vault.Blob{}, err
	}
	sum := sha256.Sum256(body)
	if int64(len(body)) != size || hex.EncodeToString(sum[:]) != digest {
		return vault.Blob{}, vault.ErrMismatch
	}
	v.blobs[digest] = body
	return vault.Blob{SHA256: digest, Size: size, IngestedAt: time.Unix(1, 0).UTC()}, nil
}

func (v *memoryVault) Stat(_ context.Context, digest string) (vault.Blob, error) {
	body, found := v.blobs[digest]
	if !found {
		return vault.Blob{}, vault.ErrNotFound
	}
	return vault.Blob{SHA256: digest, Size: int64(len(body)), IngestedAt: time.Unix(1, 0).UTC()}, nil
}

func (v *memoryVault) Open(ctx context.Context, digest string) (io.ReadCloser, vault.Blob, error) {
	blob, err := v.Stat(ctx, digest)
	if err != nil {
		return nil, vault.Blob{}, err
	}
	return io.NopCloser(bytes.NewReader(v.blobs[digest])), blob, nil
}

// requestVault issues a service-authenticated request with a raw (non-JSON) body,
// which the vault endpoints take, and returns the recorder for header and byte
// assertions.
func requestVault(t *testing.T, handler http.Handler, method, path string,
	body []byte, wantStatus int) *httptest.ResponseRecorder {
	t.Helper()
	r := httptest.NewRequest(method, path, bytes.NewReader(body))
	r.Header.Set("Authorization", "Bearer secret")
	if len(body) > 0 {
		r.Header.Set("Content-Type", "application/octet-stream")
	}
	w := httptest.NewRecorder()
	handler.ServeHTTP(w, r)
	if w.Code != wantStatus {
		t.Fatalf("%s %s status = %d, want %d: %s", method, path, w.Code, wantStatus, w.Body.String())
	}
	return w
}

func TestVaultBlobLifecycle(t *testing.T) {
	store := &memoryVault{blobs: make(map[string][]byte)}
	handler := New(checker{}, "test", Options{ServiceToken: "secret", Vault: store})
	content := []byte("durable inventory-referenced bytes")
	sum := sha256.Sum256(content)
	digest := hex.EncodeToString(sum[:])
	path := "/api/v1/vault/blobs/" + digest

	recorder := requestVault(t, handler, http.MethodPut, path, content, http.StatusOK)
	var ingested vault.Blob
	if err := json.NewDecoder(recorder.Body).Decode(&ingested); err != nil {
		t.Fatalf("decode ingest response: %v", err)
	}
	if ingested.SHA256 != digest || ingested.Size != int64(len(content)) {
		t.Fatalf("ingested blob = %#v", ingested)
	}

	// Re-ingesting the same bytes succeeds; the vault reports what it holds
	// rather than treating a repeat as a conflict.
	requestVault(t, handler, http.MethodPut, path, content, http.StatusOK)

	head := requestVault(t, handler, http.MethodHead, path, nil, http.StatusOK)
	declared := strconv.Itoa(len(content))
	if got := head.Header().Get("Content-Length"); got != declared {
		t.Fatalf("HEAD Content-Length = %q, want %s", got, declared)
	}

	get := requestVault(t, handler, http.MethodGet, path, nil, http.StatusOK)
	if !bytes.Equal(get.Body.Bytes(), content) {
		t.Fatalf("GET body = %q", get.Body.Bytes())
	}
	if got := get.Header().Get("Content-Type"); got != "application/octet-stream" {
		t.Fatalf("GET Content-Type = %q", got)
	}
}

func TestVaultBlobRejectsMismatchedBytes(t *testing.T) {
	store := &memoryVault{blobs: make(map[string][]byte)}
	handler := New(checker{}, "test", Options{ServiceToken: "secret", Vault: store})
	claimed := sha256.Sum256([]byte("what the caller says it is sending"))
	path := "/api/v1/vault/blobs/" + hex.EncodeToString(claimed[:])

	recorder := requestVault(t, handler, http.MethodPut, path,
		[]byte("what the caller actually sent"), http.StatusBadRequest)
	var failure Error
	if err := json.NewDecoder(recorder.Body).Decode(&failure); err != nil {
		t.Fatalf("decode mismatch response: %v", err)
	}
	if failure.Code != "vault_blob_mismatch" {
		t.Fatalf("mismatch error = %#v", failure)
	}
	// Fail closed: the rejected blob must not become readable.
	requestVault(t, handler, http.MethodHead, path, nil, http.StatusNotFound)
}

func TestVaultBlobMissingAndInvalid(t *testing.T) {
	store := &memoryVault{blobs: make(map[string][]byte)}
	handler := New(checker{}, "test", Options{ServiceToken: "secret", Vault: store})
	unknown := sha256.Sum256([]byte("never ingested"))
	unknownPath := "/api/v1/vault/blobs/" + hex.EncodeToString(unknown[:])

	recorder := requestVault(t, handler, http.MethodGet, unknownPath, nil, http.StatusNotFound)
	var failure Error
	if err := json.NewDecoder(recorder.Body).Decode(&failure); err != nil {
		t.Fatalf("decode missing response: %v", err)
	}
	if failure.Code != "vault_blob_not_found" {
		t.Fatalf("missing error = %#v", failure)
	}

	// A path that is not a digest is not a vault route at all.
	requestVault(t, handler, http.MethodGet, "/api/v1/vault/blobs/not-a-digest", nil, http.StatusNotFound)

	// An empty body has no length to verify against.
	recorder = requestVault(t, handler, http.MethodPut, unknownPath, nil, http.StatusBadRequest)
	if err := json.NewDecoder(recorder.Body).Decode(&failure); err != nil {
		t.Fatalf("decode empty-body response: %v", err)
	}
	if failure.Code != "invalid_vault_blob" {
		t.Fatalf("empty body error = %#v", failure)
	}

	recorder = requestVault(t, handler, http.MethodDelete, unknownPath, nil, http.StatusMethodNotAllowed)
	if got := recorder.Header().Get("Allow"); got != "GET, HEAD, PUT" {
		t.Fatalf("Allow = %q", got)
	}
}

func TestVaultBlobUnavailableWithoutStore(t *testing.T) {
	handler := New(checker{}, "test", Options{ServiceToken: "secret"})
	unknown := sha256.Sum256([]byte("no vault configured"))
	recorder := requestVault(t, handler, http.MethodGet,
		"/api/v1/vault/blobs/"+hex.EncodeToString(unknown[:]), nil, http.StatusServiceUnavailable)
	var failure Error
	if err := json.NewDecoder(recorder.Body).Decode(&failure); err != nil {
		t.Fatalf("decode unavailable response: %v", err)
	}
	if failure.Code != "vault_unavailable" {
		t.Fatalf("unavailable error = %#v", failure)
	}
}

func TestVaultBlobRequiresServiceToken(t *testing.T) {
	store := &memoryVault{blobs: make(map[string][]byte)}
	handler := New(checker{}, "test", Options{ServiceToken: "secret", Vault: store})
	unknown := sha256.Sum256([]byte("unauthenticated"))
	r := httptest.NewRequest(http.MethodGet,
		"/api/v1/vault/blobs/"+hex.EncodeToString(unknown[:]), nil)
	w := httptest.NewRecorder()
	handler.ServeHTTP(w, r)
	// The vault stays behind the internal boundary, which is what keeps it out of
	// the viewer data path.
	if w.Code != http.StatusUnauthorized {
		t.Fatalf("unauthenticated status = %d, want %d", w.Code, http.StatusUnauthorized)
	}
}
