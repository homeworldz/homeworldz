package api

import (
	"net/http"
	"strings"

	"github.com/homeworldz/server/grid/internal/inventory"
)

// The client's read-only view of its own inventory.
//
// A viewer reaches inventory through the AIS capability, whose URL carries a
// session id and whose payload is LLSD. Neither suits a session client: the
// capability URL is the LLUDP-era pattern, and this tier's rule is that a route
// derives the acting user from the bearer token and never from the path. So the
// rows are the same rows and the composition is shared
// ([inventory.FolderByID] and friends); only the credential and the encoding
// differ.
//
// Read-only on purpose. Listing is what the client asked for, and every mutation
// AIS supports — links, slam, purge, asset updates — carries ordering and
// permission rules that deserve their own design rather than a JSON transcription
// of the LLSD ones.

// clientInventoryRoot serves GET /v1/client/inventory: every folder the user
// owns, which is the whole navigable skeleton in one call, plus the id of the
// root so a client need not infer it from a parent that is empty.
func (a *API) clientInventoryRoot(w http.ResponseWriter, r *http.Request) {
	account, ok := a.requireAuth(w, r)
	if !ok {
		return
	}
	if r.Method != http.MethodGet {
		writeError(w, http.StatusMethodNotAllowed, Error{
			Code: "method_not_allowed", Message: "inventory listing requires GET"})
		return
	}
	if a.inventory == nil {
		writeError(w, http.StatusServiceUnavailable, Error{
			Code: "inventory_unavailable", Message: "inventory storage is unavailable"})
		return
	}
	folders, err := a.inventory.ListFolders(r.Context(), account.ID)
	if err != nil {
		writeError(w, http.StatusServiceUnavailable, Error{
			Code: "inventory_unavailable", Message: "inventory folders could not be loaded"})
		return
	}
	root := ""
	for _, folder := range folders {
		// The root is the one folder whose parent is not itself a folder the
		// user owns. Derived rather than assumed to be a fixed id, because a
		// client that hard-codes one is wrong the first time that changes.
		if _, hasParent := inventory.FolderByID(folders, folder.ParentID); !hasParent {
			root = folder.ID
			break
		}
	}
	writeJSON(w, http.StatusOK, map[string]any{"rootId": root, "folders": folders})
}

// clientInventoryByID serves GET /v1/client/inventory/folder/{id} and
// GET /v1/client/inventory/item/{id}.
func (a *API) clientInventoryByID(w http.ResponseWriter, r *http.Request) {
	account, ok := a.requireAuth(w, r)
	if !ok {
		return
	}
	if r.Method != http.MethodGet {
		writeError(w, http.StatusMethodNotAllowed, Error{
			Code: "method_not_allowed", Message: "inventory listing requires GET"})
		return
	}
	if a.inventory == nil {
		writeError(w, http.StatusServiceUnavailable, Error{
			Code: "inventory_unavailable", Message: "inventory storage is unavailable"})
		return
	}
	rest := strings.TrimPrefix(r.URL.Path, "/v1/client/inventory/")
	kind, id, found := strings.Cut(rest, "/")
	if !found || id == "" || strings.Contains(id, "/") {
		a.writeInventoryNotFound(w)
		return
	}

	switch kind {
	case "folder":
		folders, err := a.inventory.ListFolders(r.Context(), account.ID)
		if err != nil {
			writeError(w, http.StatusServiceUnavailable, Error{
				Code: "inventory_unavailable", Message: "inventory folders could not be loaded"})
			return
		}
		// Looked up among this user's folders only, so another user's id is
		// not refused by a check — it is simply not here.
		folder, owned := inventory.FolderByID(folders, id)
		if !owned {
			a.writeInventoryNotFound(w)
			return
		}
		items, err := a.inventory.ListItems(r.Context(), account.ID)
		if err != nil {
			writeError(w, http.StatusServiceUnavailable, Error{
				Code: "inventory_unavailable", Message: "inventory items could not be loaded"})
			return
		}
		writeJSON(w, http.StatusOK, map[string]any{
			"folder":  folder,
			"folders": inventory.ChildFolders(folders, id),
			"items":   inventory.ItemsIn(items, id),
		})
	case "item":
		items, err := a.inventory.ListItems(r.Context(), account.ID)
		if err != nil {
			writeError(w, http.StatusServiceUnavailable, Error{
				Code: "inventory_unavailable", Message: "inventory items could not be loaded"})
			return
		}
		item, owned := inventory.ItemByID(items, id)
		if !owned {
			a.writeInventoryNotFound(w)
			return
		}
		writeJSON(w, http.StatusOK, map[string]any{"item": item})
	default:
		a.writeInventoryNotFound(w)
	}
}

// One message for every miss — a folder that does not exist, an item that does
// not exist, and either owned by somebody else. Distinguishing them would let a
// caller probe for the existence of other people's inventory by id.
func (a *API) writeInventoryNotFound(w http.ResponseWriter) {
	writeError(w, http.StatusNotFound, Error{
		Code: "not_found", Message: "no such inventory folder or item"})
}
