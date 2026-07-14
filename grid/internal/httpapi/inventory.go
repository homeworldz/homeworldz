package httpapi

import (
	"errors"
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
	switch r.Method {
	case http.MethodGet:
		folders, err := a.inventory.ListFolders(r.Context(), userID)
		if err != nil {
			writeJSON(w, http.StatusInternalServerError, Error{Code: "inventory_store_error", Message: "inventory folders could not be listed"})
			return
		}
		if folders == nil {
			folders = []inventory.Folder{}
		}
		writeJSON(w, http.StatusOK, InventoryFolderList{Folders: folders})
	case http.MethodPost:
		var request CreateInventoryFolderRequest
		if !decodeJSON(w, r, &request) {
			return
		}
		if !validUUID(request.ID) || !validUUID(request.ParentID) || request.TypeDefault != -1 {
			writeJSON(w, http.StatusBadRequest, Error{Code: "invalid_inventory_folder", Message: "inventory folder is invalid"})
			return
		}
		folder, err := a.inventory.CreateFolder(r.Context(), inventory.Folder{
			ID: request.ID, OwnerUserID: userID, ParentID: request.ParentID,
			Name: request.Name, TypeDefault: request.TypeDefault,
		})
		if errors.Is(err, inventory.ErrInvalidFolder) {
			writeJSON(w, http.StatusBadRequest, Error{Code: "invalid_inventory_folder", Message: "inventory folder is invalid"})
			return
		}
		if errors.Is(err, inventory.ErrFolderNotFound) {
			writeJSON(w, http.StatusNotFound, Error{Code: "inventory_parent_not_found", Message: "inventory parent folder was not found"})
			return
		}
		if errors.Is(err, inventory.ErrFolderConflict) {
			writeJSON(w, http.StatusConflict, Error{Code: "inventory_folder_exists", Message: "inventory folder already exists"})
			return
		}
		if err != nil {
			writeJSON(w, http.StatusInternalServerError, Error{Code: "inventory_store_error", Message: "inventory folder could not be created"})
			return
		}
		w.Header().Set("Location", "/api/v1/inventory/"+userID+"/folders/"+folder.ID)
		writeJSON(w, http.StatusCreated, folder)
	default:
		w.Header().Set("Allow", "GET, POST")
		writeJSON(w, http.StatusMethodNotAllowed, Error{Code: "method_not_allowed", Message: "only GET and POST are supported"})
	}
}
