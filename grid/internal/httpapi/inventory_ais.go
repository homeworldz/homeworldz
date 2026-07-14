package httpapi

import (
	"errors"
	"fmt"
	"html"
	"net/http"
	"strings"

	"github.com/homeworldz/homeworldz/grid/internal/identifier"
	"github.com/homeworldz/homeworldz/grid/internal/identity"
	"github.com/homeworldz/homeworldz/grid/internal/inventory"
)

func (a *API) inventoryAISCapability(w http.ResponseWriter, r *http.Request) {
	if a.identity == nil || a.inventory == nil {
		writeLLSDError(w, http.StatusServiceUnavailable, "inventory is unavailable")
		return
	}
	parts := strings.Split(strings.TrimPrefix(r.URL.Path, "/caps/inventory/ais/"), "/")
	if len(parts) != 3 || !validUUID(parts[0]) || parts[1] != "category" || !validUUID(parts[2]) {
		writeLLSDError(w, http.StatusNotFound, "AIS inventory capability was not found")
		return
	}
	session, err := a.identity.ValidateSession(r.Context(), parts[0])
	if errors.Is(err, identity.ErrSessionNotFound) ||
		(err == nil && (session.ViewerCircuitCode == 0 || session.DestinationRegionID == "")) {
		writeLLSDError(w, http.StatusNotFound, "AIS inventory capability expired")
		return
	}
	if err != nil {
		writeLLSDError(w, http.StatusServiceUnavailable, "session validation failed")
		return
	}
	switch r.Method {
	case http.MethodPost:
		a.createAISInventoryFolder(w, r, session.UserID, parts[2])
	case http.MethodPatch:
		a.updateAISInventoryFolder(w, r, session.UserID, parts[2])
	default:
		w.Header().Set("Allow", "POST, PATCH")
		writeLLSDError(w, http.StatusMethodNotAllowed, "only POST and PATCH are supported")
	}
}

func (a *API) createAISInventoryFolder(w http.ResponseWriter, r *http.Request, userID, parentID string) {
	request, err := parseInventoryFolderMutationRequest(http.MaxBytesReader(w, r.Body, 64*1024), "folder_id")
	if err != nil || !validUUID(request.ID) || !validUUID(request.ParentID) ||
		request.ParentID != parentID || request.Type != -1 {
		writeLLSDError(w, http.StatusBadRequest, "invalid AIS inventory folder request")
		return
	}
	if request.ID == nullInventoryFolderID {
		request.ID, err = identifier.NewUUID()
		if err != nil {
			writeLLSDError(w, http.StatusServiceUnavailable, "inventory folder ID could not be allocated")
			return
		}
	}
	folder, err := a.inventory.CreateFolder(r.Context(), inventory.Folder{
		ID: request.ID, OwnerUserID: userID, ParentID: request.ParentID,
		Name: request.Name, TypeDefault: request.Type,
	})
	if writeAISFolderError(w, err) {
		return
	}
	writeAISUUIDArray(w, "_created_categories", folder.ID)
}

func (a *API) updateAISInventoryFolder(w http.ResponseWriter, r *http.Request, userID, folderID string) {
	request, seen, err := decodeInventoryFolderMutationRequest(http.MaxBytesReader(w, r.Body, 64*1024), "item_id")
	if err != nil || !seen["name"] || (seen["item_id"] && request.ID != folderID) ||
		(seen["parent_id"] && !validUUID(request.ParentID)) || (seen["type"] && request.Type != -1) {
		writeLLSDError(w, http.StatusBadRequest, "invalid AIS inventory folder update")
		return
	}
	folders, err := a.inventory.ListFolders(r.Context(), userID)
	if err != nil {
		writeLLSDError(w, http.StatusServiceUnavailable, "AIS inventory folder could not be loaded")
		return
	}
	var existing inventory.Folder
	for _, folder := range folders {
		if folder.ID == folderID {
			existing = folder
			break
		}
	}
	if existing.ID == "" {
		writeLLSDError(w, http.StatusNotFound, "AIS inventory folder was not found")
		return
	}
	if seen["parent_id"] && request.ParentID != existing.ParentID {
		writeLLSDError(w, http.StatusBadRequest, "moving AIS inventory folders is not supported")
		return
	}
	existing.Name = request.Name
	folder, err := a.inventory.UpdateFolder(r.Context(), inventory.Folder{
		ID: existing.ID, OwnerUserID: userID, ParentID: existing.ParentID,
		Name: existing.Name, TypeDefault: existing.TypeDefault,
	})
	if writeAISFolderError(w, err) {
		return
	}
	writeAISUUIDArray(w, "_updated_categories", folder.ID)
}

func writeAISFolderError(w http.ResponseWriter, err error) bool {
	if err == nil {
		return false
	}
	switch {
	case errors.Is(err, inventory.ErrInvalidFolder):
		writeLLSDError(w, http.StatusBadRequest, "invalid AIS inventory folder request")
	case errors.Is(err, inventory.ErrFolderNotFound):
		writeLLSDError(w, http.StatusNotFound, "AIS inventory folder was not found")
	case errors.Is(err, inventory.ErrFolderConflict):
		writeLLSDError(w, http.StatusConflict, "AIS inventory folder already exists")
	default:
		writeLLSDError(w, http.StatusServiceUnavailable, "AIS inventory folder operation failed")
	}
	return true
}

func writeAISUUIDArray(w http.ResponseWriter, key, id string) {
	w.Header().Set("Content-Type", "application/llsd+xml; charset=utf-8")
	w.WriteHeader(http.StatusOK)
	_, _ = fmt.Fprintf(w, "<?xml version=\"1.0\"?><llsd><map><key>%s</key>"+
		"<array><uuid>%s</uuid></array></map></llsd>", html.EscapeString(key), html.EscapeString(id))
}
