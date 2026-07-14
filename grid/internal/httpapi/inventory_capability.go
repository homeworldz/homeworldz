package httpapi

import (
	"encoding/xml"
	"errors"
	"fmt"
	"html"
	"io"
	"net/http"
	"strconv"
	"strings"

	"github.com/homeworldz/homeworldz/grid/internal/identity"
	"github.com/homeworldz/homeworldz/grid/internal/inventory"
)

const maximumInventoryFolderBatch = 256
const nullInventoryFolderID = "00000000-0000-0000-0000-000000000000"

func (a *API) inventoryDescendentsCapability(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		w.Header().Set("Allow", http.MethodPost)
		writeLLSDError(w, http.StatusMethodNotAllowed, "only POST is supported")
		return
	}
	if a.identity == nil || a.inventory == nil {
		writeLLSDError(w, http.StatusServiceUnavailable, "inventory is unavailable")
		return
	}
	sessionID := strings.TrimPrefix(r.URL.Path, "/caps/inventory/descendents/")
	if !validUUID(sessionID) || strings.Contains(sessionID, "/") {
		writeLLSDError(w, http.StatusNotFound, "inventory capability was not found")
		return
	}
	session, err := a.identity.ValidateSession(r.Context(), sessionID)
	if errors.Is(err, identity.ErrSessionNotFound) {
		writeLLSDError(w, http.StatusNotFound, "inventory capability expired")
		return
	}
	if err != nil {
		writeLLSDError(w, http.StatusServiceUnavailable, "session validation failed")
		return
	}
	if session.ViewerCircuitCode == 0 || session.DestinationRegionID == "" {
		writeLLSDError(w, http.StatusNotFound, "inventory capability expired")
		return
	}
	folderIDs, err := parseInventoryFolderRequest(http.MaxBytesReader(w, r.Body, 1024*1024))
	if err != nil {
		writeLLSDError(w, http.StatusBadRequest, "invalid inventory folder request")
		return
	}
	folders, err := a.inventory.ListFolders(r.Context(), session.UserID)
	if err != nil {
		writeLLSDError(w, http.StatusServiceUnavailable, "inventory folders could not be listed")
		return
	}
	items, err := a.inventory.ListItems(r.Context(), session.UserID)
	if err != nil {
		writeLLSDError(w, http.StatusServiceUnavailable, "inventory items could not be listed")
		return
	}
	w.Header().Set("Content-Type", "application/llsd+xml; charset=utf-8")
	w.WriteHeader(http.StatusOK)
	_, _ = io.WriteString(w, inventoryDescendentsXML(session.UserID, folderIDs, folders, items))
}

func parseInventoryFolderRequest(reader io.Reader) ([]string, error) {
	return parseInventoryUUIDRequest(reader, "folder_id")
}

func parseInventoryUUIDRequest(reader io.Reader, requestedKey string) ([]string, error) {
	decoder := xml.NewDecoder(reader)
	var key string
	sawLLSD := false
	seen := make(map[string]bool)
	var folders []string
	for {
		token, err := decoder.Token()
		if errors.Is(err, io.EOF) {
			break
		}
		if err != nil {
			return nil, err
		}
		start, ok := token.(xml.StartElement)
		if !ok {
			continue
		}
		if start.Name.Local == "llsd" {
			sawLLSD = true
			continue
		}
		if start.Name.Local == "key" {
			if err := decoder.DecodeElement(&key, &start); err != nil {
				return nil, err
			}
			continue
		}
		if key == requestedKey && (start.Name.Local == "uuid" || start.Name.Local == "string") {
			var folderID string
			if err := decoder.DecodeElement(&folderID, &start); err != nil {
				return nil, err
			}
			key = ""
			folderID = strings.TrimSpace(folderID)
			if !validUUID(folderID) {
				return nil, errors.New("folder ID is not a UUID")
			}
			if !seen[folderID] {
				seen[folderID] = true
				folders = append(folders, folderID)
				if len(folders) > maximumInventoryFolderBatch {
					return nil, errors.New("inventory folder batch is too large")
				}
			}
			continue
		}
		if start.Name.Local != "map" && start.Name.Local != "array" {
			key = ""
		}
	}
	if !sawLLSD || len(folders) == 0 {
		return nil, errors.New("inventory folder batch is empty")
	}
	return folders, nil
}

func inventoryDescendentsXML(ownerID string, requested []string, folders []inventory.Folder,
	items []inventory.Item) string {
	byID := make(map[string]inventory.Folder, len(folders))
	children := make(map[string][]inventory.Folder)
	itemsByFolder := make(map[string][]inventory.Item)
	for _, folder := range folders {
		byID[folder.ID] = folder
		children[folder.ParentID] = append(children[folder.ParentID], folder)
	}
	for _, item := range items {
		itemsByFolder[item.FolderID] = append(itemsByFolder[item.FolderID], item)
	}
	var good, bad strings.Builder
	for _, folderID := range requested {
		if folderID == nullInventoryFolderID {
			fmt.Fprintf(&good, "<map><key>folder_id</key><uuid>%s</uuid><key>owner_id</key><uuid>%s</uuid>"+
				"<key>version</key><integer>0</integer><key>descendents</key><integer>0</integer>"+
				"<key>categories</key><array/><key>items</key><array/></map>", folderID, html.EscapeString(ownerID))
			continue
		}
		folder, ok := byID[folderID]
		if !ok {
			fmt.Fprintf(&bad, "<map><key>folder_id</key><uuid>%s</uuid><key>error</key><string>unknown folder</string></map>",
				html.EscapeString(folderID))
			continue
		}
		descendents := children[folder.ID]
		folderItems := itemsByFolder[folder.ID]
		fmt.Fprintf(&good, "<map><key>folder_id</key><uuid>%s</uuid><key>owner_id</key><uuid>%s</uuid>"+
			"<key>version</key><integer>%d</integer><key>descendents</key><integer>%d</integer>"+
			"<key>categories</key><array>", html.EscapeString(folder.ID), html.EscapeString(ownerID),
			folder.Version, len(descendents)+len(folderItems))
		for _, child := range descendents {
			fmt.Fprintf(&good, "<map><key>folder_id</key><uuid>%s</uuid><key>parent_id</key><uuid>%s</uuid>"+
				"<key>type_default</key><integer>%d</integer><key>name</key><string>%s</string></map>",
				html.EscapeString(child.ID), html.EscapeString(child.ParentID), child.TypeDefault,
				html.EscapeString(child.Name))
		}
		good.WriteString("</array><key>items</key><array>")
		for _, item := range folderItems {
			good.WriteString(inventoryItemXML(item))
		}
		good.WriteString("</array></map>")
	}
	return "<?xml version=\"1.0\"?><llsd><map><key>folders</key><array>" + good.String() +
		"</array><key>bad_folders</key><array>" + bad.String() + "</array></map></llsd>"
}

func (a *API) inventoryItemsCapability(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		w.Header().Set("Allow", http.MethodPost)
		writeLLSDError(w, http.StatusMethodNotAllowed, "only POST is supported")
		return
	}
	if a.identity == nil || a.inventory == nil {
		writeLLSDError(w, http.StatusServiceUnavailable, "inventory is unavailable")
		return
	}
	sessionID := strings.TrimPrefix(r.URL.Path, "/caps/inventory/items/")
	if !validUUID(sessionID) || strings.Contains(sessionID, "/") {
		writeLLSDError(w, http.StatusNotFound, "inventory capability was not found")
		return
	}
	session, err := a.identity.ValidateSession(r.Context(), sessionID)
	if errors.Is(err, identity.ErrSessionNotFound) ||
		(err == nil && (session.ViewerCircuitCode == 0 || session.DestinationRegionID == "")) {
		writeLLSDError(w, http.StatusNotFound, "inventory capability expired")
		return
	}
	if err != nil {
		writeLLSDError(w, http.StatusServiceUnavailable, "session validation failed")
		return
	}
	itemIDs, err := parseInventoryUUIDRequest(http.MaxBytesReader(w, r.Body, 1024*1024), "item_id")
	if err != nil {
		writeLLSDError(w, http.StatusBadRequest, "invalid inventory item request")
		return
	}
	items, err := a.inventory.ListItems(r.Context(), session.UserID)
	if err != nil {
		writeLLSDError(w, http.StatusServiceUnavailable, "inventory items could not be listed")
		return
	}
	byID := make(map[string]inventory.Item, len(items))
	for _, item := range items {
		byID[item.ID] = item
	}
	var content strings.Builder
	for _, itemID := range itemIDs {
		if item, ok := byID[itemID]; ok {
			content.WriteString(inventoryItemXML(item))
		}
	}
	w.Header().Set("Content-Type", "application/llsd+xml; charset=utf-8")
	w.WriteHeader(http.StatusOK)
	_, _ = io.WriteString(w, "<?xml version=\"1.0\"?><llsd><map><key>agent_id</key><uuid>"+
		html.EscapeString(session.UserID)+"</uuid><key>items</key><array>"+content.String()+"</array></map></llsd>")
}

type createInventoryFolderCapabilityRequest struct {
	ID       string
	ParentID string
	Name     string
	Type     int
}

func (a *API) createInventoryFolderCapability(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		w.Header().Set("Allow", http.MethodPost)
		writeLLSDError(w, http.StatusMethodNotAllowed, "only POST is supported")
		return
	}
	if a.identity == nil || a.inventory == nil {
		writeLLSDError(w, http.StatusServiceUnavailable, "inventory is unavailable")
		return
	}
	sessionID := strings.TrimPrefix(r.URL.Path, "/caps/inventory/create-folder/")
	if !validUUID(sessionID) || strings.Contains(sessionID, "/") {
		writeLLSDError(w, http.StatusNotFound, "inventory capability was not found")
		return
	}
	session, err := a.identity.ValidateSession(r.Context(), sessionID)
	if errors.Is(err, identity.ErrSessionNotFound) ||
		(err == nil && (session.ViewerCircuitCode == 0 || session.DestinationRegionID == "")) {
		writeLLSDError(w, http.StatusNotFound, "inventory capability expired")
		return
	}
	if err != nil {
		writeLLSDError(w, http.StatusServiceUnavailable, "session validation failed")
		return
	}
	request, err := parseCreateInventoryFolderRequest(http.MaxBytesReader(w, r.Body, 64*1024))
	if err != nil || !validUUID(request.ID) || !validUUID(request.ParentID) || request.Type != -1 {
		writeLLSDError(w, http.StatusBadRequest, "invalid inventory folder request")
		return
	}
	folder, err := a.inventory.CreateFolder(r.Context(), inventory.Folder{
		ID: request.ID, OwnerUserID: session.UserID, ParentID: request.ParentID,
		Name: request.Name, TypeDefault: request.Type,
	})
	if errors.Is(err, inventory.ErrInvalidFolder) {
		writeLLSDError(w, http.StatusBadRequest, "invalid inventory folder request")
		return
	}
	if errors.Is(err, inventory.ErrFolderNotFound) {
		writeLLSDError(w, http.StatusNotFound, "inventory parent folder was not found")
		return
	}
	if errors.Is(err, inventory.ErrFolderConflict) {
		writeLLSDError(w, http.StatusConflict, "inventory folder already exists")
		return
	}
	if err != nil {
		writeLLSDError(w, http.StatusServiceUnavailable, "inventory folder could not be created")
		return
	}
	w.Header().Set("Content-Type", "application/llsd+xml; charset=utf-8")
	w.WriteHeader(http.StatusOK)
	_, _ = fmt.Fprintf(w, "<?xml version=\"1.0\"?><llsd><map>"+
		"<key>folder_id</key><uuid>%s</uuid><key>parent_id</key><uuid>%s</uuid>"+
		"<key>type</key><integer>%d</integer><key>name</key><string>%s</string>"+
		"</map></llsd>", html.EscapeString(folder.ID), html.EscapeString(folder.ParentID),
		folder.TypeDefault, html.EscapeString(folder.Name))
}

func parseCreateInventoryFolderRequest(reader io.Reader) (createInventoryFolderCapabilityRequest, error) {
	decoder := xml.NewDecoder(reader)
	var request createInventoryFolderCapabilityRequest
	var key string
	sawLLSD := false
	seen := make(map[string]bool)
	for {
		token, err := decoder.Token()
		if errors.Is(err, io.EOF) {
			break
		}
		if err != nil {
			return request, err
		}
		start, ok := token.(xml.StartElement)
		if !ok {
			continue
		}
		if start.Name.Local == "llsd" {
			sawLLSD = true
			continue
		}
		if start.Name.Local == "key" {
			if err := decoder.DecodeElement(&key, &start); err != nil {
				return request, err
			}
			continue
		}
		if key == "" {
			continue
		}
		if seen[key] {
			return request, errors.New("inventory folder request repeats a field")
		}
		var value string
		if start.Name.Local != "uuid" && start.Name.Local != "string" && start.Name.Local != "integer" {
			key = ""
			continue
		}
		if err := decoder.DecodeElement(&value, &start); err != nil {
			return request, err
		}
		value = strings.TrimSpace(value)
		switch key {
		case "folder_id":
			request.ID = value
		case "parent_id":
			request.ParentID = value
		case "name":
			request.Name = value
		case "type":
			request.Type, err = strconv.Atoi(value)
			if err != nil {
				return request, err
			}
		default:
			key = ""
			continue
		}
		seen[key] = true
		key = ""
	}
	if !sawLLSD || !seen["folder_id"] || !seen["parent_id"] || !seen["name"] || !seen["type"] {
		return request, errors.New("inventory folder request is incomplete")
	}
	return request, nil
}

func inventoryItemXML(item inventory.Item) string {
	creator := item.CreatorUserID
	if creator == "" {
		creator = nullInventoryFolderID
	}
	created := item.CreatedAt.Unix()
	if item.CreatedAt.IsZero() {
		created = 0
	}
	return fmt.Sprintf("<map><key>item_id</key><uuid>%s</uuid><key>parent_id</key><uuid>%s</uuid>"+
		"<key>permissions</key><map><key>creator_id</key><uuid>%s</uuid><key>owner_id</key><uuid>%s</uuid>"+
		"<key>last_owner_id</key><uuid>%s</uuid><key>group_id</key><uuid>%s</uuid>"+
		"<key>is_owner_group</key><boolean>false</boolean><key>base_mask</key><integer>%d</integer>"+
		"<key>owner_mask</key><integer>%d</integer><key>group_mask</key><integer>0</integer>"+
		"<key>everyone_mask</key><integer>%d</integer><key>next_owner_mask</key><integer>%d</integer></map>"+
		"<key>asset_id</key><uuid>%s</uuid><key>type</key><integer>%d</integer>"+
		"<key>inv_type</key><integer>%d</integer><key>flags</key><integer>%d</integer>"+
		"<key>sale_info</key><map><key>sale_type</key><integer>%d</integer><key>sale_price</key><integer>%d</integer></map>"+
		"<key>name</key><string>%s</string><key>desc</key><string>%s</string>"+
		"<key>created_at</key><integer>%d</integer></map>",
		html.EscapeString(item.ID), html.EscapeString(item.FolderID), html.EscapeString(creator),
		html.EscapeString(item.OwnerUserID), nullInventoryFolderID, nullInventoryFolderID,
		item.BasePermissions, item.CurrentPermissions, item.EveryonePermissions, item.NextPermissions,
		html.EscapeString(item.AssetID), item.AssetType, item.InventoryType, item.Flags, item.SaleType,
		item.SalePrice, html.EscapeString(item.Name), html.EscapeString(item.Description), created)
}

func writeLLSDError(w http.ResponseWriter, status int, message string) {
	w.Header().Set("Content-Type", "application/llsd+xml; charset=utf-8")
	w.WriteHeader(status)
	_, _ = io.WriteString(w, "<?xml version=\"1.0\"?><llsd><map><key>error</key><string>"+
		html.EscapeString(message)+"</string></map></llsd>")
}
