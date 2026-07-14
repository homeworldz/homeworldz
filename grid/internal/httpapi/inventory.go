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
	if !found || !validUUID(userID) {
		a.notFound(w, r)
		return
	}
	if suffix == "items" {
		a.inventoryItemsByUser(w, r, userID)
		return
	}
	if suffix != "folders" {
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

func (a *API) inventoryItemsByUser(w http.ResponseWriter, r *http.Request, userID string) {
	if r.Method != http.MethodPost {
		w.Header().Set("Allow", http.MethodPost)
		writeJSON(w, http.StatusMethodNotAllowed, Error{Code: "method_not_allowed", Message: "only POST is supported"})
		return
	}
	var request CreateInventoryItemRequest
	if !decodeJSON(w, r, &request) {
		return
	}
	if !validUUID(request.ID) || !validUUID(request.CreatorUserID) ||
		request.CreatorUserID != userID || !validUUID(request.FolderID) ||
		!validUUID(request.AssetID) || request.AssetType != 0 || request.InventoryType != 0 {
		writeJSON(w, http.StatusBadRequest, Error{Code: "invalid_inventory_item", Message: "texture inventory item is invalid"})
		return
	}
	const fullPermissions = uint32(0x7fffffff)
	item, err := a.inventory.CreateItem(r.Context(), inventory.Item{
		ID: request.ID, OwnerUserID: userID, CreatorUserID: request.CreatorUserID,
		FolderID: request.FolderID, AssetID: request.AssetID, AssetType: request.AssetType,
		InventoryType: request.InventoryType, Name: request.Name, Description: request.Description,
		BasePermissions: fullPermissions, CurrentPermissions: fullPermissions,
		EveryonePermissions: request.EveryonePermissions, NextPermissions: request.NextPermissions,
	})
	switch {
	case errors.Is(err, inventory.ErrInvalidItem):
		writeJSON(w, http.StatusBadRequest, Error{Code: "invalid_inventory_item", Message: "texture inventory item is invalid"})
	case errors.Is(err, inventory.ErrItemFolderNotFound):
		writeJSON(w, http.StatusNotFound, Error{Code: "inventory_folder_not_found", Message: "inventory item folder was not found"})
	case errors.Is(err, inventory.ErrItemConflict):
		writeJSON(w, http.StatusConflict, Error{Code: "inventory_item_exists", Message: "inventory item already exists"})
	case err != nil:
		writeJSON(w, http.StatusInternalServerError, Error{Code: "inventory_store_error", Message: "inventory item could not be created"})
	default:
		w.Header().Set("Location", "/api/v1/inventory/"+userID+"/items/"+item.ID)
		writeJSON(w, http.StatusCreated, item)
	}
}
