package httpapi

import (
	"errors"
	"net/http"
	"strconv"
	"strings"

	"github.com/homeworldz/homeworldz/grid/internal/identifier"
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
	if suffix == "copy-library-item" {
		a.copyLibraryInventoryItem(w, r, userID)
		return
	}
	if strings.HasPrefix(suffix, "system-folders/") {
		folderTypeText := strings.TrimPrefix(suffix, "system-folders/")
		folderType, err := strconv.Atoi(folderTypeText)
		if err != nil || strings.Contains(folderTypeText, "/") {
			a.notFound(w, r)
			return
		}
		a.inventorySystemFolderByUser(w, r, userID, folderType)
		return
	}
	if strings.HasPrefix(suffix, "folders/") {
		folderID := strings.TrimPrefix(suffix, "folders/")
		if !validUUID(folderID) || strings.Contains(folderID, "/") {
			a.notFound(w, r)
			return
		}
		a.inventoryFolderByUser(w, r, userID, folderID)
		return
	}
	if strings.HasPrefix(suffix, "items/") {
		itemID := strings.TrimPrefix(suffix, "items/")
		if !validUUID(itemID) || strings.Contains(itemID, "/") {
			a.notFound(w, r)
			return
		}
		a.inventoryItemByUser(w, r, userID, itemID)
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

func (a *API) inventorySystemFolderByUser(w http.ResponseWriter, r *http.Request, userID string, folderType int) {
	if r.Method != http.MethodGet {
		w.Header().Set("Allow", http.MethodGet)
		writeJSON(w, http.StatusMethodNotAllowed, Error{Code: "method_not_allowed", Message: "only GET is supported"})
		return
	}
	folders, err := a.inventory.ListFolders(r.Context(), userID)
	if err != nil {
		writeJSON(w, http.StatusInternalServerError, Error{Code: "inventory_store_error", Message: "inventory folder could not be loaded"})
		return
	}
	for _, folder := range folders {
		if folder.TypeDefault == folderType && folder.ID == inventory.SystemFolderID(userID, folderType) {
			writeJSON(w, http.StatusOK, folder)
			return
		}
	}
	writeJSON(w, http.StatusNotFound, Error{Code: "inventory_system_folder_not_found", Message: "inventory system folder was not found"})
}

func (a *API) inventoryItemByUser(w http.ResponseWriter, r *http.Request, userID, itemID string) {
	if r.Method != http.MethodPut {
		w.Header().Set("Allow", http.MethodPut)
		writeJSON(w, http.StatusMethodNotAllowed, Error{Code: "method_not_allowed", Message: "only PUT is supported"})
		return
	}
	var request MoveInventoryItemRequest
	if !decodeJSON(w, r, &request) {
		return
	}
	if !validUUID(request.FolderID) || len(request.Name) > 255 {
		writeJSON(w, http.StatusBadRequest, Error{Code: "invalid_inventory_item_move", Message: "inventory item move is invalid"})
		return
	}
	items, err := a.inventory.ListItems(r.Context(), userID)
	if err != nil {
		writeJSON(w, http.StatusInternalServerError, Error{Code: "inventory_store_error", Message: "inventory item could not be loaded"})
		return
	}
	var item inventory.Item
	for _, existing := range items {
		if existing.ID == itemID {
			item = existing
			break
		}
	}
	if item.ID == "" {
		writeJSON(w, http.StatusNotFound, Error{Code: "inventory_item_not_found", Message: "inventory item was not found"})
		return
	}
	item.FolderID = request.FolderID
	if request.Name != "" {
		item.Name = request.Name
	}
	item, err = a.inventory.UpdateItem(r.Context(), item)
	switch {
	case errors.Is(err, inventory.ErrInvalidItem):
		writeJSON(w, http.StatusBadRequest, Error{Code: "invalid_inventory_item_move", Message: "inventory item move is invalid"})
	case errors.Is(err, inventory.ErrItemFolderNotFound):
		writeJSON(w, http.StatusNotFound, Error{Code: "inventory_folder_not_found", Message: "inventory destination folder was not found"})
	case errors.Is(err, inventory.ErrItemNotFound):
		writeJSON(w, http.StatusNotFound, Error{Code: "inventory_item_not_found", Message: "inventory item was not found"})
	case err != nil:
		writeJSON(w, http.StatusInternalServerError, Error{Code: "inventory_store_error", Message: "inventory item could not be moved"})
	default:
		writeJSON(w, http.StatusOK, item)
	}
}

func (a *API) inventoryFolderByUser(w http.ResponseWriter, r *http.Request, userID, folderID string) {
	if r.Method != http.MethodPut {
		w.Header().Set("Allow", http.MethodPut)
		writeJSON(w, http.StatusMethodNotAllowed, Error{Code: "method_not_allowed", Message: "only PUT is supported"})
		return
	}
	var request MoveInventoryFolderRequest
	if !decodeJSON(w, r, &request) {
		return
	}
	if !validUUID(request.ParentID) {
		writeJSON(w, http.StatusBadRequest, Error{Code: "invalid_inventory_folder_move", Message: "inventory folder destination is invalid"})
		return
	}
	folders, err := a.inventory.ListFolders(r.Context(), userID)
	if err != nil {
		writeJSON(w, http.StatusInternalServerError, Error{Code: "inventory_store_error", Message: "inventory folder could not be loaded"})
		return
	}
	var folder inventory.Folder
	for _, existing := range folders {
		if existing.ID == folderID {
			folder = existing
			break
		}
	}
	if folder.ID == "" {
		writeJSON(w, http.StatusNotFound, Error{Code: "inventory_folder_not_found", Message: "inventory folder was not found"})
		return
	}
	folder.ParentID = request.ParentID
	folder, err = a.inventory.UpdateFolder(r.Context(), folder)
	switch {
	case errors.Is(err, inventory.ErrInvalidFolder):
		writeJSON(w, http.StatusBadRequest, Error{Code: "invalid_inventory_folder_move", Message: "inventory folder move is invalid"})
	case errors.Is(err, inventory.ErrFolderNotFound):
		writeJSON(w, http.StatusNotFound, Error{Code: "inventory_folder_not_found", Message: "inventory folder or destination was not found"})
	case err != nil:
		writeJSON(w, http.StatusInternalServerError, Error{Code: "inventory_store_error", Message: "inventory folder could not be moved"})
	default:
		writeJSON(w, http.StatusOK, folder)
	}
}

func (a *API) copyLibraryInventoryItem(w http.ResponseWriter, r *http.Request, userID string) {
	if r.Method != http.MethodPost {
		w.Header().Set("Allow", http.MethodPost)
		writeJSON(w, http.StatusMethodNotAllowed, Error{Code: "method_not_allowed", Message: "only POST is supported"})
		return
	}
	var request CopyLibraryInventoryItemRequest
	if !decodeJSON(w, r, &request) {
		return
	}
	if !validUUID(request.SourceItemID) || !validUUID(request.DestinationFolderID) || len(request.Name) > 255 {
		writeJSON(w, http.StatusBadRequest, Error{Code: "invalid_library_copy", Message: "library inventory copy is invalid"})
		return
	}
	var source inventory.Item
	for _, item := range inventory.LibraryItems() {
		if item.ID == request.SourceItemID {
			source = item
			break
		}
	}
	if source.ID == "" {
		writeJSON(w, http.StatusNotFound, Error{Code: "library_item_not_found", Message: "library inventory item was not found"})
		return
	}
	destinationID := request.DestinationFolderID
	if destinationID == "00000000-0000-0000-0000-000000000000" {
		switch source.AssetType {
		case 5:
			destinationID = inventory.SystemFolderID(userID, 5)
		case 13:
			destinationID = inventory.SystemFolderID(userID, 13)
		default:
			writeJSON(w, http.StatusBadRequest, Error{Code: "unsupported_library_copy", Message: "this library item type cannot yet be copied automatically"})
			return
		}
	}
	itemID, err := identifier.NewUUID()
	if err != nil {
		writeJSON(w, http.StatusServiceUnavailable, Error{Code: "inventory_id_unavailable", Message: "inventory item ID could not be allocated"})
		return
	}
	name := request.Name
	if name == "" {
		name = source.Name
	}
	item, err := a.inventory.CreateItem(r.Context(), inventory.Item{
		ID: itemID, OwnerUserID: userID, CreatorUserID: source.CreatorUserID,
		FolderID: destinationID, AssetID: source.AssetID, AssetType: source.AssetType,
		InventoryType: source.InventoryType, Name: name, Description: source.Description,
		Flags: source.Flags, BasePermissions: source.BasePermissions,
		CurrentPermissions: source.CurrentPermissions, EveryonePermissions: source.EveryonePermissions,
		NextPermissions: source.NextPermissions, SaleType: source.SaleType, SalePrice: source.SalePrice,
	})
	switch {
	case errors.Is(err, inventory.ErrItemFolderNotFound):
		writeJSON(w, http.StatusNotFound, Error{Code: "inventory_folder_not_found", Message: "inventory destination folder was not found"})
	case errors.Is(err, inventory.ErrInvalidItem):
		writeJSON(w, http.StatusBadRequest, Error{Code: "invalid_library_copy", Message: "library inventory copy is invalid"})
	case err != nil:
		writeJSON(w, http.StatusInternalServerError, Error{Code: "inventory_store_error", Message: "library inventory item could not be copied"})
	default:
		w.Header().Set("Location", "/api/v1/inventory/"+userID+"/items/"+item.ID)
		writeJSON(w, http.StatusCreated, item)
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
	validType := (request.AssetType == 0 && request.InventoryType == 0) ||
		(request.AssetType == 6 && request.InventoryType == 6)
	if !validUUID(request.ID) || !validUUID(request.CreatorUserID) ||
		!validUUID(request.FolderID) ||
		!validUUID(request.AssetID) || !validType {
		writeJSON(w, http.StatusBadRequest, Error{Code: "invalid_inventory_item", Message: "inventory item is invalid"})
		return
	}
	item, err := a.inventory.CreateItem(r.Context(), inventory.Item{
		ID: request.ID, OwnerUserID: userID, CreatorUserID: request.CreatorUserID,
		FolderID: request.FolderID, AssetID: request.AssetID, AssetType: request.AssetType,
		InventoryType: request.InventoryType, Name: request.Name, Description: request.Description,
		BasePermissions: request.BasePermissions, CurrentPermissions: request.CurrentPermissions,
		EveryonePermissions: request.EveryonePermissions, NextPermissions: request.NextPermissions,
	})
	switch {
	case errors.Is(err, inventory.ErrInvalidItem):
		writeJSON(w, http.StatusBadRequest, Error{Code: "invalid_inventory_item", Message: "inventory item is invalid"})
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
