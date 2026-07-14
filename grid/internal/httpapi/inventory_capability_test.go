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
			"<key>_updated_categories</key><array><uuid>"+created.ID+"</uuid>") ||
		!strings.Contains(updateResponse.Body.String(),
			"<key>category_id</key><uuid>"+created.ID+"</uuid>") ||
		!strings.Contains(updateResponse.Body.String(),
			"<key>name</key><string>Projects</string>") {
		t.Fatalf("status = %d, response = %s, folder = %#v",
			updateResponse.Code, updateResponse.Body.String(), stored[len(stored)-1])
	}
}

func TestAISFetchInventoryFolderChildrenAndCurrentLinks(t *testing.T) {
	identities := newMemoryIdentityStore()
	user, err := identities.CreateUser(context.Background(), "inventory.ais.read", "development-password")
	if err != nil {
		t.Fatal(err)
	}
	session, err := identities.CreateSession(context.Background(), "inventory.ais.read", "development-password", time.Hour)
	if err != nil {
		t.Fatal(err)
	}
	if err := identities.AssignViewerDestination(context.Background(), session.ID, 123456,
		"30000000-0000-4000-8000-000000000001"); err != nil {
		t.Fatal(err)
	}
	inventories := &memoryInventoryStore{folders: make(map[string][]inventory.Folder)}
	folders, _ := inventories.EnsureSystemFolders(context.Background(), user.ID)
	for _, item := range inventory.DefaultWearables(user.ID) {
		if _, err := inventories.EnsureItem(context.Background(), item); err != nil {
			t.Fatal(err)
		}
	}
	handler := New(checker{}, "test", Options{Identity: identities, Inventory: inventories})
	base := "/caps/inventory/ais/" + session.ID
	rootRequest := httptest.NewRequest(http.MethodGet, base+"/category/"+folders[0].ID+"/children?depth=50", nil)
	rootResponse := httptest.NewRecorder()
	handler.ServeHTTP(rootResponse, rootRequest)
	if rootResponse.Code != http.StatusOK ||
		!strings.Contains(rootResponse.Body.String(), "<string>Default Shape</string>") ||
		!strings.Contains(rootResponse.Body.String(), "<string>Default Pants</string>") {
		t.Fatalf("status = %d, recursive AIS root response lacks default outfit: %s",
			rootResponse.Code, rootResponse.Body.String())
	}
	bodyPartsID := inventory.SystemFolderID(user.ID, 13)
	request := httptest.NewRequest(http.MethodGet, base+"/category/"+bodyPartsID+"/children?depth=0", nil)
	response := httptest.NewRecorder()
	handler.ServeHTTP(response, request)
	for _, expected := range []string{
		"<key>category_id</key><uuid>" + bodyPartsID + "</uuid>",
		"<key>items</key><map>", "<string>Default Shape</string>", "<string>Default Eyes</string>",
	} {
		if response.Code != http.StatusOK || !strings.Contains(response.Body.String(), expected) {
			t.Fatalf("status = %d, AIS children response lacks %q: %s", response.Code, expected, response.Body.String())
		}
	}
	currentRequest := httptest.NewRequest(http.MethodGet, base+"/category/current/links", nil)
	currentResponse := httptest.NewRecorder()
	handler.ServeHTTP(currentResponse, currentRequest)
	if currentResponse.Code != http.StatusOK ||
		!strings.Contains(currentResponse.Body.String(), "<key>links</key><map>") ||
		!strings.Contains(currentResponse.Body.String(), "<key>linked_id</key><uuid>") ||
		!strings.Contains(currentResponse.Body.String(), "<string>Default Pants</string>") {
		t.Fatalf("status = %d, AIS current links response = %s", currentResponse.Code, currentResponse.Body.String())
	}
	if len(folders) == 0 {
		t.Fatal("system folders were not created")
	}
	orphansRequest := httptest.NewRequest(http.MethodGet, base+"/orphans", nil)
	orphansResponse := httptest.NewRecorder()
	handler.ServeHTTP(orphansResponse, orphansRequest)
	if orphansResponse.Code != http.StatusOK || !strings.Contains(orphansResponse.Body.String(), "<key>items</key><map></map>") {
		t.Fatalf("status = %d, AIS orphans response = %s", orphansResponse.Code, orphansResponse.Body.String())
	}
}

func TestAISLibraryIsReadableAndRejectsMutations(t *testing.T) {
	identities := newMemoryIdentityStore()
	_, err := identities.CreateUser(context.Background(), "inventory.library", "development-password")
	if err != nil {
		t.Fatal(err)
	}
	session, err := identities.CreateSession(context.Background(), "inventory.library", "development-password", time.Hour)
	if err != nil {
		t.Fatal(err)
	}
	if err := identities.AssignViewerDestination(context.Background(), session.ID, 123456,
		"30000000-0000-4000-8000-000000000001"); err != nil {
		t.Fatal(err)
	}
	handler := New(checker{}, "test", Options{
		Identity: identities,
		Inventory: &memoryInventoryStore{
			folders: make(map[string][]inventory.Folder),
		},
	})
	expectedValues := []string{
		"<key>agent_id</key><uuid>" + inventory.LibraryOwnerID + "</uuid>",
		"<string>Library</string>", "<string>Clothing</string>", "<string>Body Parts</string>",
		"<string>Textures</string>", "<string>Terrain</string>", "<string>Terrain Sand and Dirt</string>",
		"<string>Terrain Grass</string>", "<string>Terrain Mountain</string>", "<string>Terrain Rock</string>",
		"<string>Initial Outfits</string>", "<string>Default Avatar</string>",
		"<string>Default Shape</string>", "<string>Default Skin</string>",
		"<string>Default Hair</string>", "<string>Default Eyes</string>",
		"<string>Default Shirt</string>", "<string>Default Pants</string>",
	}
	for _, prefix := range []string{"/caps/inventory/library/", "/caps/inventory/ais/"} {
		base := prefix + session.ID
		request := httptest.NewRequest(http.MethodGet,
			base+"/category/"+inventory.LibraryRootID+"/children?depth=50", nil)
		response := httptest.NewRecorder()
		handler.ServeHTTP(response, request)
		for _, expected := range expectedValues {
			if response.Code != http.StatusOK || !strings.Contains(response.Body.String(), expected) {
				t.Fatalf("status = %d, AIS library response from %q lacks %q: %s",
					response.Code, prefix, expected, response.Body.String())
			}
		}
	}
	base := "/caps/inventory/library/" + session.ID
	mutation := httptest.NewRequest(http.MethodPatch,
		base+"/category/"+inventory.LibraryDefaultAvatarID, strings.NewReader("<llsd><map></map></llsd>"))
	mutationResponse := httptest.NewRecorder()
	handler.ServeHTTP(mutationResponse, mutation)
	if mutationResponse.Code != http.StatusMethodNotAllowed || mutationResponse.Header().Get("Allow") != http.MethodGet {
		t.Fatalf("library mutation status = %d, Allow = %q, body = %s",
			mutationResponse.Code, mutationResponse.Header().Get("Allow"), mutationResponse.Body.String())
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
