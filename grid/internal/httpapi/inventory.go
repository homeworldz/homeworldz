package httpapi

import (
	"net/http"
	"strings"

	"github.com/homeworldz/homeworldz/grid/internal/inventory"
)

func (a *API) inventoryByUser(w http.ResponseWriter, r *http.Request) {
	if a.inventory == nil {
		writeJSON(w, http.StatusServiceUnavailable, Error{Code: "inventory_store_unavailable", Message: "inventory storage is unavailable"})
		return
	}
	path := strings.TrimPrefix(r.URL.Path, "/api/v1/inventory/")
	userID, suffix, found := strings.Cut(path, "/")
	if !found || !validUUID(userID) || suffix != "folders" {
		a.notFound(w, r)
		return
	}
	if r.Method != http.MethodGet {
		w.Header().Set("Allow", http.MethodGet)
		writeJSON(w, http.StatusMethodNotAllowed, Error{Code: "method_not_allowed", Message: "only GET is supported"})
		return
	}
	folders, err := a.inventory.ListFolders(r.Context(), userID)
	if err != nil {
		writeJSON(w, http.StatusInternalServerError, Error{Code: "inventory_store_error", Message: "inventory folders could not be listed"})
		return
	}
	if folders == nil {
		folders = []inventory.Folder{}
	}
	writeJSON(w, http.StatusOK, InventoryFolderList{Folders: folders})
}
