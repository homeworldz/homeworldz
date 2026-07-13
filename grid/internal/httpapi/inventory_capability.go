package httpapi

import (
	"encoding/xml"
	"errors"
	"fmt"
	"html"
	"io"
	"net/http"
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
	w.Header().Set("Content-Type", "application/llsd+xml; charset=utf-8")
	w.WriteHeader(http.StatusOK)
	_, _ = io.WriteString(w, inventoryDescendentsXML(session.UserID, folderIDs, folders))
}

func parseInventoryFolderRequest(reader io.Reader) ([]string, error) {
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
		if key == "folder_id" && (start.Name.Local == "uuid" || start.Name.Local == "string") {
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

func inventoryDescendentsXML(ownerID string, requested []string, folders []inventory.Folder) string {
	byID := make(map[string]inventory.Folder, len(folders))
	children := make(map[string][]inventory.Folder)
	for _, folder := range folders {
		byID[folder.ID] = folder
		children[folder.ParentID] = append(children[folder.ParentID], folder)
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
		fmt.Fprintf(&good, "<map><key>folder_id</key><uuid>%s</uuid><key>owner_id</key><uuid>%s</uuid>"+
			"<key>version</key><integer>%d</integer><key>descendents</key><integer>%d</integer>"+
			"<key>categories</key><array>", html.EscapeString(folder.ID), html.EscapeString(ownerID),
			folder.Version, len(descendents))
		for _, child := range descendents {
			fmt.Fprintf(&good, "<map><key>folder_id</key><uuid>%s</uuid><key>parent_id</key><uuid>%s</uuid>"+
				"<key>type_default</key><integer>%d</integer><key>name</key><string>%s</string></map>",
				html.EscapeString(child.ID), html.EscapeString(child.ParentID), child.TypeDefault,
				html.EscapeString(child.Name))
		}
		good.WriteString("</array><key>items</key><array/></map>")
	}
	return "<?xml version=\"1.0\"?><llsd><map><key>folders</key><array>" + good.String() +
		"</array><key>bad_folders</key><array>" + bad.String() + "</array></map></llsd>"
}

func writeLLSDError(w http.ResponseWriter, status int, message string) {
	w.Header().Set("Content-Type", "application/llsd+xml; charset=utf-8")
	w.WriteHeader(status)
	_, _ = io.WriteString(w, "<?xml version=\"1.0\"?><llsd><map><key>error</key><string>"+
		html.EscapeString(message)+"</string></map></llsd>")
}
