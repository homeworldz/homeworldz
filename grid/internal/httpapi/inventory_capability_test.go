package httpapi

import (
	"bytes"
	"context"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
	"time"

	"github.com/homeworldz/homeworldz/grid/internal/inventory"
)

func TestInventoryDescendentsCapability(t *testing.T) {
	identities := newMemoryIdentityStore()
	user, err := identities.CreateUser(context.Background(), "inventory.user", "development-password")
	if err != nil {
		t.Fatal(err)
	}
	session, err := identities.CreateSession(context.Background(), "inventory.user", "development-password", time.Hour)
	if err != nil {
		t.Fatal(err)
	}
	if err := identities.AssignViewerDestination(context.Background(), session.ID, 123456,
		"30000000-0000-4000-8000-000000000001"); err != nil {
		t.Fatal(err)
	}
	inventories := &memoryInventoryStore{folders: make(map[string][]inventory.Folder)}
	folders, _ := inventories.EnsureSystemFolders(context.Background(), user.ID)
	handler := New(checker{}, "test", Options{Identity: identities, Inventory: inventories})
	body := `<?xml version="1.0"?><llsd><map><key>folders</key><array><map>` +
		`<key>folder_id</key><uuid>` + folders[0].ID + `</uuid><key>owner_id</key><uuid>` + user.ID +
		`</uuid><key>fetch_folders</key><boolean>true</boolean><key>fetch_items</key><boolean>true</boolean>` +
		`</map></array></map></llsd>`
	r := httptest.NewRequest(http.MethodPost, "/caps/inventory/descendents/"+session.ID, bytes.NewBufferString(body))
	w := httptest.NewRecorder()
	handler.ServeHTTP(w, r)
	if w.Code != http.StatusOK || !strings.HasPrefix(w.Header().Get("Content-Type"), "application/llsd+xml") {
		t.Fatalf("status = %d, content type = %q, body = %s", w.Code, w.Header().Get("Content-Type"), w.Body.String())
	}
	content := w.Body.String()
	if !strings.Contains(content, "<key>descendents</key><integer>20</integer>") ||
		!strings.Contains(content, "<key>type_default</key><integer>13</integer>") ||
		!strings.Contains(content, "<key>items</key><array></array>") {
		t.Fatalf("inventory response = %s", content)
	}
	nullResponse := inventoryDescendentsXML(user.ID, []string{nullInventoryFolderID}, folders, nil)
	if strings.Contains(nullResponse, "unknown folder") ||
		!strings.Contains(nullResponse, "<key>folder_id</key><uuid>"+nullInventoryFolderID+"</uuid>") {
		t.Fatalf("null inventory folder response = %s", nullResponse)
	}
}

func TestInventoryItemsCapability(t *testing.T) {
	identities := newMemoryIdentityStore()
	user, err := identities.CreateUser(context.Background(), "inventory.items", "development-password")
	if err != nil {
		t.Fatal(err)
	}
	session, err := identities.CreateSession(context.Background(), "inventory.items", "development-password", time.Hour)
	if err != nil {
		t.Fatal(err)
	}
	if err := identities.AssignViewerDestination(context.Background(), session.ID, 123456,
		"30000000-0000-4000-8000-000000000001"); err != nil {
		t.Fatal(err)
	}
	inventories := &memoryInventoryStore{folders: make(map[string][]inventory.Folder)}
	folders, _ := inventories.EnsureSystemFolders(context.Background(), user.ID)
	item := inventory.Item{
		ID: "40000000-0000-4000-8000-000000000001", OwnerUserID: user.ID, CreatorUserID: user.ID,
		FolderID: folders[9].ID, AssetID: "60000000-0000-4000-8000-000000000001",
		AssetType: 13, InventoryType: 18, Name: "Default Shape", BasePermissions: 0x7fffffff,
		CurrentPermissions: 0x7fffffff, NextPermissions: 0x7fffffff,
	}
	_, _ = inventories.EnsureItem(context.Background(), item)
	handler := New(checker{}, "test", Options{Identity: identities, Inventory: inventories})
	body := `<?xml version="1.0"?><llsd><map><key>items</key><array><map>` +
		`<key>owner_id</key><uuid>` + user.ID + `</uuid><key>item_id</key><uuid>` + item.ID +
		`</uuid></map></array></map></llsd>`
	r := httptest.NewRequest(http.MethodPost, "/caps/inventory/items/"+session.ID, bytes.NewBufferString(body))
	w := httptest.NewRecorder()
	handler.ServeHTTP(w, r)
	if w.Code != http.StatusOK || !strings.Contains(w.Body.String(),
		"<key>item_id</key><uuid>"+item.ID+"</uuid>") {
		t.Fatalf("status = %d, inventory item response = %s", w.Code, w.Body.String())
	}
}

func TestCreateInventoryFolderCapability(t *testing.T) {
	identities := newMemoryIdentityStore()
	user, err := identities.CreateUser(context.Background(), "inventory.create", "development-password")
	if err != nil {
		t.Fatal(err)
	}
	session, err := identities.CreateSession(context.Background(), "inventory.create", "development-password", time.Hour)
	if err != nil {
		t.Fatal(err)
	}
	if err := identities.AssignViewerDestination(context.Background(), session.ID, 123456,
		"30000000-0000-4000-8000-000000000001"); err != nil {
		t.Fatal(err)
	}
	inventories := &memoryInventoryStore{folders: make(map[string][]inventory.Folder)}
	folders, _ := inventories.EnsureSystemFolders(context.Background(), user.ID)
	const folderID = "40000000-0000-4000-8000-000000000002"
	body := `<?xml version="1.0"?><llsd><map>` +
		`<key>folder_id</key><uuid>` + folderID + `</uuid>` +
		`<key>parent_id</key><uuid>` + folders[0].ID + `</uuid>` +
		`<key>type_default</key><integer>-1</integer><key>name</key><string>New Folder</string>` +
		`</map></llsd>`
	r := httptest.NewRequest(http.MethodPost, "/caps/inventory/create-folder/"+session.ID,
		bytes.NewBufferString(body))
	w := httptest.NewRecorder()
	New(checker{}, "test", Options{Identity: identities, Inventory: inventories}).ServeHTTP(w, r)
	if w.Code != http.StatusOK || !strings.Contains(w.Body.String(),
		"<key>folder_id</key><uuid>"+folderID+"</uuid>") ||
		!strings.Contains(w.Body.String(), "<key>name</key><string>New Folder</string>") {
		t.Fatalf("status = %d, create folder response = %s", w.Code, w.Body.String())
	}
	stored, _ := inventories.ListFolders(context.Background(), user.ID)
	if len(stored) != len(folders)+1 || stored[len(stored)-1].ID != folderID {
		t.Fatalf("stored inventory folders = %#v", stored)
	}
}

func TestAISCreateAndRenameInventoryFolder(t *testing.T) {
	identities := newMemoryIdentityStore()
	user, err := identities.CreateUser(context.Background(), "inventory.ais", "development-password")
	if err != nil {
		t.Fatal(err)
	}
	session, err := identities.CreateSession(context.Background(), "inventory.ais", "development-password", time.Hour)
	if err != nil {
		t.Fatal(err)
	}
	if err := identities.AssignViewerDestination(context.Background(), session.ID, 123456,
		"30000000-0000-4000-8000-000000000001"); err != nil {
		t.Fatal(err)
	}
	inventories := &memoryInventoryStore{folders: make(map[string][]inventory.Folder)}
	folders, _ := inventories.EnsureSystemFolders(context.Background(), user.ID)
	handler := New(checker{}, "test", Options{Identity: identities, Inventory: inventories})
	base := "/caps/inventory/ais/" + session.ID
	createBody := `<?xml version="1.0"?><llsd><map><key>categories</key><array><map>` +
		`<key>folder_id</key><uuid>` + nullInventoryFolderID + `</uuid>` +
		`<key>parent_id</key><uuid>` + folders[0].ID + `</uuid>` +
		`<key>type</key><integer>-1</integer><key>name</key><string>New Folder</string>` +
		`</map></array></map></llsd>`
	createRequest := httptest.NewRequest(http.MethodPost, base+"/category/"+folders[0].ID+"?tid="+session.ID,
		bytes.NewBufferString(createBody))
	createResponse := httptest.NewRecorder()
	handler.ServeHTTP(createResponse, createRequest)
	stored, _ := inventories.ListFolders(context.Background(), user.ID)
	if createResponse.Code != http.StatusOK || len(stored) != len(folders)+1 {
		t.Fatalf("status = %d, response = %s, folders = %#v", createResponse.Code, createResponse.Body.String(), stored)
	}
	created := stored[len(stored)-1]
	if created.Name != "New Folder" || !strings.Contains(createResponse.Body.String(),
		"<key>_created_categories</key><array><uuid>"+created.ID+"</uuid>") {
		t.Fatalf("created folder = %#v, response = %s", created, createResponse.Body.String())
	}
	// Firestorm sends category renames as a partial AIS update containing only the changed name.
	updateBody := `<?xml version="1.0"?><llsd><map>` +
		`<key>name</key><string>Projects</string></map></llsd>`
	updateRequest := httptest.NewRequest(http.MethodPatch, base+"/category/"+created.ID,
		bytes.NewBufferString(updateBody))
	updateResponse := httptest.NewRecorder()
	handler.ServeHTTP(updateResponse, updateRequest)
	stored, _ = inventories.ListFolders(context.Background(), user.ID)
	if updateResponse.Code != http.StatusOK || stored[len(stored)-1].Name != "Projects" ||
		!strings.Contains(updateResponse.Body.String(),
			"<key>_updated_categories</key><array><uuid>"+created.ID+"</uuid>") {
		t.Fatalf("status = %d, response = %s, folder = %#v",
			updateResponse.Code, updateResponse.Body.String(), stored[len(stored)-1])
	}
}

func TestInventoryItemXML(t *testing.T) {
	item := inventory.Item{ID: "40000000-0000-4000-8000-000000000001",
		OwnerUserID: "20000000-0000-4000-8000-000000000001",
		FolderID:    "50000000-0000-4000-8000-000000000001",
		AssetID:     "60000000-0000-4000-8000-000000000001", AssetType: 13, InventoryType: 18,
		Name: "Default <Shape>", BasePermissions: 0x7fffffff, CurrentPermissions: 0x7fffffff}
	content := inventoryItemXML(item)
	for _, expected := range []string{"<key>item_id</key><uuid>" + item.ID, "<key>type</key><integer>13</integer>",
		"<key>inv_type</key><integer>18</integer>", "Default &lt;Shape&gt;"} {
		if !strings.Contains(content, expected) {
			t.Fatalf("inventory item XML lacks %q: %s", expected, content)
		}
	}
}

func TestInventoryDescendentsCapabilityRejectsBadRequest(t *testing.T) {
	handler := New(checker{}, "test", Options{})
	r := httptest.NewRequest(http.MethodGet, "/caps/inventory/descendents/not-a-session", nil)
	w := httptest.NewRecorder()
	handler.ServeHTTP(w, r)
	if w.Code != http.StatusMethodNotAllowed {
		t.Fatalf("status = %d, want %d", w.Code, http.StatusMethodNotAllowed)
	}
}
