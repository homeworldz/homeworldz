package httpapi

import (
	"bytes"
	"encoding/xml"
	"errors"
	"fmt"
	"html"
	"io"
	"net/http"
	"strconv"
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
	if len(parts) < 2 || !validUUID(parts[0]) {
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
	if r.Method == http.MethodGet {
		switch {
		case len(parts) == 4 && parts[1] == "category" && validUUID(parts[2]) && parts[3] == "children":
			if inventory.IsLibraryFolder(parts[2]) {
				writeAISInventoryFolder(w, r, inventory.LibraryOwnerID, parts[2],
					inventory.LibraryFolders(), inventory.LibraryItems(), false)
			} else {
				a.fetchAISInventoryFolder(w, r, session.UserID, parts[2], false)
			}
			return
		case len(parts) == 4 && parts[1] == "category" && parts[2] == "current" && parts[3] == "links":
			a.fetchAISInventoryFolder(w, r, session.UserID, inventory.SystemFolderID(session.UserID, 46), true)
			return
		case len(parts) == 2 && parts[1] == "orphans":
			writeAISEmptyOrphans(w)
			return
		default:
			writeLLSDError(w, http.StatusNotFound, "AIS inventory resource was not found")
			return
		}
	}
	if len(parts) != 3 || !validUUID(parts[2]) {
		writeLLSDError(w, http.StatusNotFound, "AIS inventory capability was not found")
		return
	}
	switch parts[1] {
	case "category":
		switch r.Method {
		case http.MethodPost:
			a.createAISInventoryFolder(w, r, session.UserID, parts[2])
		case http.MethodPatch:
			a.updateAISInventoryFolder(w, r, session.UserID, parts[2])
		default:
			w.Header().Set("Allow", "POST, PATCH")
			writeLLSDError(w, http.StatusMethodNotAllowed, "only POST and PATCH are supported")
		}
	case "item":
		switch r.Method {
		case http.MethodPatch:
			a.updateAISInventoryItem(w, r, session.UserID, parts[2])
		case http.MethodDelete:
			a.deleteAISInventoryItem(w, r, session.UserID, parts[2])
		default:
			w.Header().Set("Allow", "PATCH, DELETE")
			writeLLSDError(w, http.StatusMethodNotAllowed, "only PATCH and DELETE are supported")
		}
	default:
		writeLLSDError(w, http.StatusNotFound, "AIS inventory resource was not found")
	}
}

func (a *API) libraryAISCapability(w http.ResponseWriter, r *http.Request) {
	if a.identity == nil {
		writeLLSDError(w, http.StatusServiceUnavailable, "inventory library is unavailable")
		return
	}
	parts := strings.Split(strings.TrimPrefix(r.URL.Path, "/caps/inventory/library/"), "/")
	if len(parts) < 2 || !validUUID(parts[0]) {
		writeLLSDError(w, http.StatusNotFound, "AIS library capability was not found")
		return
	}
	session, err := a.identity.ValidateSession(r.Context(), parts[0])
	if errors.Is(err, identity.ErrSessionNotFound) ||
		(err == nil && (session.ViewerCircuitCode == 0 || session.DestinationRegionID == "")) {
		writeLLSDError(w, http.StatusNotFound, "AIS library capability expired")
		return
	}
	if err != nil {
		writeLLSDError(w, http.StatusServiceUnavailable, "session validation failed")
		return
	}
	if r.Method != http.MethodGet {
		w.Header().Set("Allow", http.MethodGet)
		writeLLSDError(w, http.StatusMethodNotAllowed, "the inventory library is read-only")
		return
	}
	switch {
	case len(parts) == 4 && parts[1] == "category" && validUUID(parts[2]) && parts[3] == "children":
		writeAISInventoryFolder(w, r, inventory.LibraryOwnerID, parts[2],
			inventory.LibraryFolders(), inventory.LibraryItems(), false)
	case len(parts) == 2 && parts[1] == "orphans":
		writeAISEmptyOrphans(w)
	default:
		writeLLSDError(w, http.StatusNotFound, "AIS library resource was not found")
	}
}

func (a *API) fetchAISInventoryFolder(w http.ResponseWriter, r *http.Request, userID, folderID string, linksOnly bool) {
	folders, err := a.inventory.ListFolders(r.Context(), userID)
	if err != nil {
		writeLLSDError(w, http.StatusServiceUnavailable, "AIS inventory folders could not be loaded")
		return
	}
	items, err := a.inventory.ListItems(r.Context(), userID)
	if err != nil {
		writeLLSDError(w, http.StatusServiceUnavailable, "AIS inventory items could not be loaded")
		return
	}
	writeAISInventoryFolder(w, r, userID, folderID, folders, items, linksOnly)
}

func writeAISInventoryFolder(w http.ResponseWriter, r *http.Request, userID, folderID string,
	folders []inventory.Folder, items []inventory.Item, linksOnly bool) {
	var requested inventory.Folder
	for _, folder := range folders {
		if folder.ID == folderID {
			requested = folder
			break
		}
	}
	if requested.ID == "" {
		writeLLSDError(w, http.StatusNotFound, "AIS inventory folder was not found")
		return
	}
	depth := 0
	if value := r.URL.Query().Get("depth"); value != "" && value != "*" {
		if parsed, parseErr := strconv.Atoi(value); parseErr == nil && parsed > 0 {
			depth = min(parsed, 8)
		}
	} else if value == "*" {
		depth = 8
	}
	embedded := inventoryAISEmbeddedXML(requested.ID, userID, folders, items, depth, linksOnly)
	w.Header().Set("Content-Type", "application/llsd+xml; charset=utf-8")
	w.WriteHeader(http.StatusOK)
	_, _ = io.WriteString(w, "<?xml version=\"1.0\"?><llsd><map>"+
		"<key>category_id</key><uuid>"+html.EscapeString(requested.ID)+"</uuid>"+
		"<key>folder_id</key><uuid>"+html.EscapeString(requested.ID)+"</uuid>"+
		"<key>agent_id</key><uuid>"+html.EscapeString(userID)+"</uuid>"+
		"<key>parent_id</key><uuid>"+html.EscapeString(requested.ParentID)+"</uuid>"+
		"<key>type_default</key><integer>"+fmt.Sprint(requested.TypeDefault)+"</integer>"+
		"<key>name</key><string>"+html.EscapeString(requested.Name)+"</string>"+
		"<key>version</key><integer>"+fmt.Sprint(requested.Version)+"</integer>"+
		embedded+"</map></llsd>")
}

func inventoryAISFolderXML(folder inventory.Folder, userID string) string {
	return fmt.Sprintf("<map><key>category_id</key><uuid>%s</uuid><key>folder_id</key><uuid>%s</uuid>"+
		"<key>agent_id</key><uuid>%s</uuid><key>parent_id</key><uuid>%s</uuid>"+
		"<key>type_default</key><integer>%d</integer><key>name</key><string>%s</string>"+
		"<key>version</key><integer>%d</integer></map>",
		html.EscapeString(folder.ID), html.EscapeString(folder.ID), html.EscapeString(userID),
		html.EscapeString(folder.ParentID), folder.TypeDefault, html.EscapeString(folder.Name), folder.Version)
}

func inventoryAISEmbeddedXML(folderID, userID string, folders []inventory.Folder, items []inventory.Item,
	depth int, linksOnly bool) string {
	var categories, ordinaryItems, links strings.Builder
	itemsByID := make(map[string]inventory.Item, len(items))
	for _, item := range items {
		itemsByID[item.ID] = item
	}
	if !linksOnly {
		for _, folder := range folders {
			if folder.ParentID != folderID {
				continue
			}
			categories.WriteString("<key>" + html.EscapeString(folder.ID) + "</key>")
			folderXML := inventoryAISFolderXML(folder, userID)
			if depth > 0 {
				folderXML = strings.TrimSuffix(folderXML, "</map>") +
					inventoryAISEmbeddedXML(folder.ID, userID, folders, items, depth-1, false) + "</map>"
			}
			categories.WriteString(folderXML)
		}
	}
	for _, item := range items {
		if item.FolderID != folderID {
			continue
		}
		if item.AssetType == 24 {
			links.WriteString("<key>" + html.EscapeString(item.ID) + "</key>")
			source, found := itemsByID[item.AssetID]
			links.WriteString(inventoryAISLinkXML(item, source, found))
		} else if !linksOnly {
			ordinaryItems.WriteString("<key>" + html.EscapeString(item.ID) + "</key>")
			ordinaryItems.WriteString(inventoryAISItemXML(item, false))
		}
	}
	return "<key>_embedded</key><map>" +
		"<key>categories</key><map>" + categories.String() + "</map>" +
		"<key>items</key><map>" + ordinaryItems.String() + "</map>" +
		"<key>links</key><map>" + links.String() + "</map></map>"
}

func inventoryAISLinkXML(link inventory.Item, source inventory.Item, sourceFound bool) string {
	content := inventoryAISItemXML(link, true)
	if !sourceFound {
		return content
	}
	return strings.TrimSuffix(content, "</map>") +
		"<key>_embedded</key><map><key>item</key>" + inventoryItemXML(source) + "</map></map>"
}

func inventoryAISItemXML(item inventory.Item, link bool) string {
	if link {
		created := item.CreatedAt.Unix()
		if item.CreatedAt.IsZero() {
			created = 0
		}
		return fmt.Sprintf("<map><key>item_id</key><uuid>%s</uuid>"+
			"<key>parent_id</key><uuid>%s</uuid><key>agent_id</key><uuid>%s</uuid>"+
			"<key>linked_id</key><uuid>%s</uuid><key>type</key><integer>%d</integer>"+
			"<key>inv_type</key><integer>%d</integer><key>name</key><string>%s</string>"+
			"<key>desc</key><string>%s</string><key>created_at</key><integer>%d</integer></map>",
			html.EscapeString(item.ID), html.EscapeString(item.FolderID),
			html.EscapeString(item.OwnerUserID), html.EscapeString(item.AssetID),
			item.AssetType, item.InventoryType, html.EscapeString(item.Name),
			html.EscapeString(item.Description), created)
	}
	return inventoryItemXML(item)
}

func writeAISEmptyOrphans(w http.ResponseWriter) {
	w.Header().Set("Content-Type", "application/llsd+xml; charset=utf-8")
	w.WriteHeader(http.StatusOK)
	_, _ = io.WriteString(w, "<?xml version=\"1.0\"?><llsd><map><key>_embedded</key><map>"+
		"<key>categories</key><map></map><key>items</key><map></map>"+
		"<key>links</key><map></map></map></map></llsd>")
}

func (a *API) createAISInventoryFolder(w http.ResponseWriter, r *http.Request, userID, parentID string) {
	body, err := io.ReadAll(http.MaxBytesReader(w, r.Body, 256*1024))
	if err != nil {
		writeLLSDError(w, http.StatusBadRequest, "invalid AIS inventory request")
		return
	}
	links, foundLinks, err := decodeAISInventoryLinks(bytes.NewReader(body))
	if foundLinks {
		if err != nil {
			writeLLSDError(w, http.StatusBadRequest, "invalid AIS inventory link request")
			return
		}
		a.createAISInventoryLinks(w, r, userID, parentID, links)
		return
	}
	request, err := parseInventoryFolderMutationRequest(bytes.NewReader(body), "folder_id")
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

type aisInventoryLink struct {
	LinkedID      string
	AssetType     int
	InventoryType int
	Name          string
	Description   string
}

func decodeAISInventoryLinks(reader io.Reader) ([]aisInventoryLink, bool, error) {
	decoder := xml.NewDecoder(reader)
	var key string
	sawLLSD := false
	for {
		token, err := decoder.Token()
		if errors.Is(err, io.EOF) {
			return nil, false, nil
		}
		if err != nil {
			return nil, false, err
		}
		switch value := token.(type) {
		case xml.StartElement:
			switch value.Name.Local {
			case "llsd":
				sawLLSD = true
			case "key":
				if err := decoder.DecodeElement(&key, &value); err != nil {
					return nil, false, err
				}
			case "array":
				if sawLLSD && strings.TrimSpace(key) == "links" {
					links, err := decodeAISInventoryLinkArray(decoder)
					return links, true, err
				}
			}
		}
	}
}

func decodeAISInventoryLinkArray(decoder *xml.Decoder) ([]aisInventoryLink, error) {
	links := make([]aisInventoryLink, 0, 8)
	for {
		token, err := decoder.Token()
		if err != nil {
			return nil, err
		}
		switch value := token.(type) {
		case xml.StartElement:
			if value.Name.Local != "map" {
				return nil, errors.New("AIS inventory links array contains a non-map value")
			}
			link, err := decodeAISInventoryLinkMap(decoder)
			if err != nil {
				return nil, err
			}
			links = append(links, link)
			if len(links) > 256 {
				return nil, errors.New("AIS inventory link batch is too large")
			}
		case xml.EndElement:
			if value.Name.Local == "array" {
				if len(links) == 0 {
					return nil, errors.New("AIS inventory link batch is empty")
				}
				return links, nil
			}
		}
	}
}

func decodeAISInventoryLinkMap(decoder *xml.Decoder) (aisInventoryLink, error) {
	var link aisInventoryLink
	var key string
	seen := make(map[string]bool)
	for {
		token, err := decoder.Token()
		if err != nil {
			return link, err
		}
		switch value := token.(type) {
		case xml.StartElement:
			if value.Name.Local == "key" {
				if err := decoder.DecodeElement(&key, &value); err != nil {
					return link, err
				}
				key = strings.TrimSpace(key)
				continue
			}
			if key == "" {
				return link, errors.New("AIS inventory link value has no key")
			}
			if seen[key] {
				return link, errors.New("AIS inventory link repeats a field")
			}
			var field string
			if err := decoder.DecodeElement(&field, &value); err != nil {
				return link, err
			}
			field = strings.TrimSpace(field)
			switch key {
			case "linked_id":
				link.LinkedID = field
			case "type":
				link.AssetType, err = strconv.Atoi(field)
			case "inv_type":
				link.InventoryType, err = strconv.Atoi(field)
			case "name":
				link.Name = field
			case "desc":
				link.Description = field
			default:
				key = ""
				continue
			}
			if err != nil {
				return link, err
			}
			seen[key] = true
			key = ""
		case xml.EndElement:
			if value.Name.Local == "map" {
				if !seen["linked_id"] || !seen["type"] || !seen["inv_type"] || !seen["name"] {
					return link, errors.New("AIS inventory link omits a required field")
				}
				return link, nil
			}
		}
	}
}

func (a *API) createAISInventoryLinks(w http.ResponseWriter, r *http.Request, userID, parentID string,
	requests []aisInventoryLink) {
	folders, err := a.inventory.ListFolders(r.Context(), userID)
	if err != nil {
		writeLLSDError(w, http.StatusServiceUnavailable, "AIS inventory folder could not be loaded")
		return
	}
	parentFound := false
	for _, folder := range folders {
		if folder.ID == parentID {
			parentFound = true
			break
		}
	}
	if !parentFound {
		writeLLSDError(w, http.StatusNotFound, "AIS inventory destination folder was not found")
		return
	}
	sourceItems, err := a.inventory.ListItems(r.Context(), userID)
	if err != nil {
		writeLLSDError(w, http.StatusServiceUnavailable, "AIS inventory items could not be loaded")
		return
	}
	sources := make(map[string]inventory.Item, len(sourceItems))
	for _, item := range sourceItems {
		sources[item.ID] = item
	}
	created := make([]inventory.Item, 0, len(requests))
	seenSources := make(map[string]bool, len(requests))
	for _, request := range requests {
		source, ok := sources[request.LinkedID]
		if !ok {
			writeLLSDError(w, http.StatusNotFound, "AIS inventory link source was not found")
			return
		}
		if !validUUID(request.LinkedID) || request.AssetType != 24 ||
			request.InventoryType != source.InventoryType || request.Name == "" ||
			len(request.Name) > 255 || len(request.Description) > 1024 || seenSources[request.LinkedID] {
			writeLLSDError(w, http.StatusBadRequest, "invalid AIS inventory link request")
			return
		}
		seenSources[request.LinkedID] = true
		itemID, err := identifier.NewUUID()
		if err != nil {
			writeLLSDError(w, http.StatusServiceUnavailable, "inventory link ID could not be allocated")
			return
		}
		creatorID := source.CreatorUserID
		if creatorID == "" || creatorID == nullInventoryFolderID {
			creatorID = userID
		}
		created = append(created, inventory.Item{
			ID: itemID, OwnerUserID: userID, CreatorUserID: creatorID, FolderID: parentID,
			AssetID: request.LinkedID, AssetType: 24, InventoryType: source.InventoryType,
			Name: request.Name, Description: request.Description, Flags: source.Flags,
			BasePermissions: source.BasePermissions, CurrentPermissions: source.CurrentPermissions,
			EveryonePermissions: source.EveryonePermissions, NextPermissions: source.NextPermissions,
		})
	}
	created, err = a.inventory.CreateItems(r.Context(), created)
	if writeAISItemError(w, err) {
		return
	}
	writeAISLinkCreation(w, created)
}

func writeAISLinkCreation(w http.ResponseWriter, items []inventory.Item) {
	var created, embedded strings.Builder
	for _, item := range items {
		created.WriteString("<uuid>" + html.EscapeString(item.ID) + "</uuid>")
		embedded.WriteString("<key>" + html.EscapeString(item.ID) + "</key>")
		embedded.WriteString(inventoryAISItemXML(item, true))
	}
	w.Header().Set("Content-Type", "application/llsd+xml; charset=utf-8")
	w.WriteHeader(http.StatusOK)
	_, _ = io.WriteString(w, "<?xml version=\"1.0\"?><llsd><map>"+
		"<key>_created_items</key><array>"+created.String()+"</array>"+
		"<key>_embedded</key><map><key>categories</key><map></map>"+
		"<key>items</key><map></map><key>links</key><map>"+embedded.String()+
		"</map></map></map></llsd>")
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
	writeAISFolderUpdate(w, folder)
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

func writeAISFolderUpdate(w http.ResponseWriter, folder inventory.Folder) {
	w.Header().Set("Content-Type", "application/llsd+xml; charset=utf-8")
	w.WriteHeader(http.StatusOK)
	_, _ = fmt.Fprintf(w, "<?xml version=\"1.0\"?><llsd><map>"+
		"<key>_updated_categories</key><array><uuid>%s</uuid></array>"+
		"<key>category_id</key><uuid>%s</uuid><key>parent_id</key><uuid>%s</uuid>"+
		"<key>type_default</key><integer>%d</integer><key>name</key><string>%s</string>"+
		"<key>version</key><integer>%d</integer></map></llsd>",
		html.EscapeString(folder.ID), html.EscapeString(folder.ID),
		html.EscapeString(folder.ParentID), folder.TypeDefault,
		html.EscapeString(folder.Name), folder.Version)
}

type aisInventoryItemMutation struct {
	ID          string
	ParentID    string
	Name        string
	Description string
}

func decodeAISInventoryItemMutation(reader io.Reader) (aisInventoryItemMutation, map[string]bool, error) {
	decoder := xml.NewDecoder(reader)
	var request aisInventoryItemMutation
	seen := make(map[string]bool)
	var key string
	mapDepth := 0
	sawLLSD := false
	for {
		token, err := decoder.Token()
		if errors.Is(err, io.EOF) {
			break
		}
		if err != nil {
			return request, seen, err
		}
		switch value := token.(type) {
		case xml.StartElement:
			switch value.Name.Local {
			case "llsd":
				sawLLSD = true
			case "map":
				mapDepth++
			case "key":
				if mapDepth == 1 {
					if err := decoder.DecodeElement(&key, &value); err != nil {
						return request, seen, err
					}
				}
			default:
				if mapDepth != 1 || key == "" ||
					(value.Name.Local != "uuid" && value.Name.Local != "string") {
					continue
				}
				var field string
				if err := decoder.DecodeElement(&field, &value); err != nil {
					return request, seen, err
				}
				field = strings.TrimSpace(field)
				normalized := key
				if normalized == "desc" {
					normalized = "description"
				}
				if seen[normalized] {
					return request, seen, errors.New("AIS inventory item update repeats a field")
				}
				switch normalized {
				case "item_id":
					request.ID = field
				case "parent_id":
					request.ParentID = field
				case "name":
					request.Name = field
				case "description":
					request.Description = field
				default:
					key = ""
					continue
				}
				seen[normalized] = true
				key = ""
			}
		case xml.EndElement:
			if value.Name.Local == "map" {
				mapDepth--
			}
		}
	}
	if !sawLLSD || (!seen["name"] && !seen["description"] && !seen["parent_id"]) {
		return request, seen, errors.New("AIS inventory item update is empty")
	}
	return request, seen, nil
}

func (a *API) updateAISInventoryItem(w http.ResponseWriter, r *http.Request, userID, itemID string) {
	request, seen, err := decodeAISInventoryItemMutation(http.MaxBytesReader(w, r.Body, 64*1024))
	if err != nil || (seen["item_id"] && request.ID != itemID) ||
		(seen["parent_id"] && !validUUID(request.ParentID)) {
		writeLLSDError(w, http.StatusBadRequest, "invalid AIS inventory item update")
		return
	}
	items, err := a.inventory.ListItems(r.Context(), userID)
	if err != nil {
		writeLLSDError(w, http.StatusServiceUnavailable, "AIS inventory item could not be loaded")
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
		writeLLSDError(w, http.StatusNotFound, "AIS inventory item was not found")
		return
	}
	if seen["name"] {
		item.Name = request.Name
	}
	if seen["description"] {
		item.Description = request.Description
	}
	if seen["parent_id"] {
		item.FolderID = request.ParentID
	}
	item, err = a.inventory.UpdateItem(r.Context(), item)
	if writeAISItemError(w, err) {
		return
	}
	w.Header().Set("Content-Type", "application/llsd+xml; charset=utf-8")
	w.WriteHeader(http.StatusOK)
	_, _ = io.WriteString(w, "<?xml version=\"1.0\"?><llsd>"+
		inventoryAISItemXML(item, item.AssetType == 24)+"</llsd>")
}

func (a *API) deleteAISInventoryItem(w http.ResponseWriter, r *http.Request, userID, itemID string) {
	_, err := a.inventory.DeleteItem(r.Context(), userID, itemID)
	if writeAISItemError(w, err) {
		return
	}
	writeAISUUIDArray(w, "_removed_items", itemID)
}

func writeAISItemError(w http.ResponseWriter, err error) bool {
	if err == nil {
		return false
	}
	switch {
	case errors.Is(err, inventory.ErrInvalidItem):
		writeLLSDError(w, http.StatusBadRequest, "invalid AIS inventory item update")
	case errors.Is(err, inventory.ErrItemNotFound):
		writeLLSDError(w, http.StatusNotFound, "AIS inventory item was not found")
	case errors.Is(err, inventory.ErrItemFolderNotFound):
		writeLLSDError(w, http.StatusNotFound, "AIS inventory destination folder was not found")
	case errors.Is(err, inventory.ErrItemConflict):
		writeLLSDError(w, http.StatusConflict, "AIS inventory item already exists")
	default:
		writeLLSDError(w, http.StatusServiceUnavailable, "AIS inventory item operation failed")
	}
	return true
}
