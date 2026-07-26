package httpapi

import (
	"errors"
	"io"
	"net/http"
	"strconv"
	"strings"

	"github.com/homeworldz/server/grid/internal/vault"
)

// vaultBlob serves the asset vault of ADR 0026 at
// /api/v1/vault/blobs/{sha256}. It sits under the internal service-token
// boundary with the rest of /api/, which is what keeps the vault out of the
// viewer data path: only regions reach it, and viewers keep fetching asset bytes
// from the region they are connected to.
//
//	PUT   ingests bytes, verified against the digest in the path and the
//	      Content-Length, and is idempotent.
//	HEAD  reports whether the vault holds a blob, which is the question the
//	      inventory-commit invariant asks.
//	GET   returns the bytes, so a region that cannot reach a peer can always fall
//	      back to the vault for inventory-referenced content.
func (a *API) vaultBlob(w http.ResponseWriter, r *http.Request) {
	if a.vault == nil {
		writeJSON(w, http.StatusServiceUnavailable, Error{Code: "vault_unavailable", Message: "asset vault is unavailable"})
		return
	}
	digest := strings.TrimPrefix(r.URL.Path, "/api/v1/vault/blobs/")
	if strings.Contains(digest, "/") || !vault.ValidDigest(digest) {
		a.notFound(w, r)
		return
	}
	switch r.Method {
	case http.MethodPut:
		a.ingestVaultBlob(w, r, digest)
	case http.MethodHead:
		blob, err := a.vault.Stat(r.Context(), digest)
		if writeVaultError(w, err) {
			return
		}
		w.Header().Set("Content-Type", "application/octet-stream")
		w.Header().Set("Content-Length", strconv.FormatInt(blob.Size, 10))
		w.WriteHeader(http.StatusOK)
	case http.MethodGet:
		content, blob, err := a.vault.Open(r.Context(), digest)
		if writeVaultError(w, err) {
			return
		}
		defer content.Close()
		w.Header().Set("Content-Type", "application/octet-stream")
		w.Header().Set("Content-Length", strconv.FormatInt(blob.Size, 10))
		w.WriteHeader(http.StatusOK)
		// A copy failure here is a broken connection, not a vault fault, and the
		// status line has already gone out.
		_, _ = io.Copy(w, content)
	default:
		w.Header().Set("Allow", "GET, HEAD, PUT")
		writeJSON(w, http.StatusMethodNotAllowed, Error{Code: "method_not_allowed", Message: "only GET, HEAD, and PUT are supported"})
	}
}

// ingestVaultBlob writes bytes through to the vault. The declared length comes
// from Content-Length: verification needs a length up front, so a chunked body
// with no declared length is rejected rather than trusted.
//
// The response is 200 rather than 201 because ingest is idempotent and the same
// bytes commonly arrive more than once — from a re-upload, a second region, or a
// backfill — and the useful answer is "the vault holds this", not whether this
// particular request is what put it there.
func (a *API) ingestVaultBlob(w http.ResponseWriter, r *http.Request, digest string) {
	if r.ContentLength <= 0 {
		writeJSON(w, http.StatusBadRequest, Error{Code: "invalid_vault_blob", Message: "a positive Content-Length is required"})
		return
	}
	if r.ContentLength > vault.MaxBlobSize {
		writeJSON(w, http.StatusRequestEntityTooLarge, Error{Code: "vault_blob_too_large", Message: "blob exceeds the maximum vault blob size"})
		return
	}
	blob, err := a.vault.Ingest(r.Context(), digest, r.ContentLength,
		http.MaxBytesReader(w, r.Body, vault.MaxBlobSize+1))
	if writeVaultError(w, err) {
		return
	}
	writeJSON(w, http.StatusOK, blob)
}

func writeVaultError(w http.ResponseWriter, err error) bool {
	if err == nil {
		return false
	}
	switch {
	case errors.Is(err, vault.ErrNotFound):
		writeJSON(w, http.StatusNotFound, Error{Code: "vault_blob_not_found", Message: "the vault does not hold the blob"})
	case errors.Is(err, vault.ErrMismatch):
		writeJSON(w, http.StatusBadRequest, Error{Code: "vault_blob_mismatch", Message: "blob bytes do not match the declared digest or length"})
	case errors.Is(err, vault.ErrInvalid):
		writeJSON(w, http.StatusBadRequest, Error{Code: "invalid_vault_blob", Message: "blob digest or length is invalid"})
	default:
		writeJSON(w, http.StatusInternalServerError, Error{Code: "vault_error", Message: "vault operation failed"})
	}
	return true
}
