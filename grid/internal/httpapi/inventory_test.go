package httpapi

import (
	"context"
	"net/http"
	"testing"

	"github.com/homeworldz/homeworldz/grid/internal/inventory"
)

type memoryInventoryStore struct {
	folders map[string][]inventory.Folder
	items   map[string][]inventory.Item
}

func (s *memoryInventoryStore) EnsureSystemFolders(_ context.Context, userID string) ([]inventory.Folder, error) {
	values := inventory.SystemFolders(userID)
	s.folders[userID] = values
	return values, nil
}

func (s *memoryInventoryStore) ListFolders(_ context.Context, userID string) ([]inventory.Folder, error) {
	return s.folders[userID], nil
}

func (s *memoryInventoryStore) CreateFolder(_ context.Context, folder inventory.Folder) (inventory.Folder, error) {
	for _, existing := range s.folders[folder.OwnerUserID] {
		if existing.ID == folder.ID {
			return inventory.Folder{}, inventory.ErrFolderConflict
		}
	}
	parentFound := false
	for index := range s.folders[folder.OwnerUserID] {
		if s.folders[folder.OwnerUserID][index].ID == folder.ParentID {
			s.folders[folder.OwnerUserID][index].Version++
			parentFound = true
		}
	}
	if !parentFound {
		return inventory.Folder{}, inventory.ErrFolderNotFound
	}
	folder.Version = 1
	s.folders[folder.OwnerUserID] = append(s.folders[folder.OwnerUserID], folder)
	return folder, nil
}

func (s *memoryInventoryStore) UpdateFolder(_ context.Context, folder inventory.Folder) (inventory.Folder, error) {
	for index, existing := range s.folders[folder.OwnerUserID] {
		if existing.ID != folder.ID {
			continue
		}
		if existing.TypeDefault != -1 || existing.ParentID != folder.ParentID || folder.TypeDefault != -1 {
			return inventory.Folder{}, inventory.ErrInvalidFolder
		}
		folder.Version = existing.Version + 1
		s.folders[folder.OwnerUserID][index] = folder
		return folder, nil
	}
	return inventory.Folder{}, inventory.ErrFolderNotFound
}

func (s *memoryInventoryStore) EnsureItem(_ context.Context, item inventory.Item) (bool, error) {
	if s.items == nil {
		s.items = make(map[string][]inventory.Item)
	}
	for index, existing := range s.items[item.OwnerUserID] {
		if existing.ID == item.ID {
			if existing == item {
				return false, nil
			}
			s.items[item.OwnerUserID][index] = item
			return true, nil
		}
	}
	s.items[item.OwnerUserID] = append(s.items[item.OwnerUserID], item)
	return true, nil
}

func (s *memoryInventoryStore) ListItems(_ context.Context, userID string) ([]inventory.Item, error) {
	return s.items[userID], nil
}

func TestInventoryFoldersEndpoint(t *testing.T) {
	const userID = "20000000-0000-4000-8000-000000000001"
	store := &memoryInventoryStore{folders: make(map[string][]inventory.Folder)}
	_, _ = store.EnsureSystemFolders(context.Background(), userID)
	handler := New(checker{}, "test", Options{ServiceToken: "secret", Inventory: store})

	result := requestRegion[InventoryFolderList](t, handler, http.MethodGet,
		"/api/v1/inventory/"+userID+"/folders", "", http.StatusOK)
	if len(result.Folders) != 21 || result.Folders[0].TypeDefault != 8 {
		t.Fatalf("inventory folders = %#v", result.Folders)
	}
	requestRegion[Error](t, handler, http.MethodGet,
		"/api/v1/inventory/not-a-uuid/folders", "", http.StatusNotFound)
	const folderID = "40000000-0000-4000-8000-000000000001"
	created := requestRegion[inventory.Folder](t, handler, http.MethodPost,
		"/api/v1/inventory/"+userID+"/folders",
		`{"id":"`+folderID+`","parentId":"`+inventory.SystemFolderID(userID, 8)+`","name":"Projects","typeDefault":-1}`,
		http.StatusCreated)
	if created.ID != folderID || created.Name != "Projects" || created.Version != 1 {
		t.Fatalf("created inventory folder = %#v", created)
	}
	requestRegion[Error](t, handler, http.MethodPost,
		"/api/v1/inventory/"+userID+"/folders",
		`{"id":"`+folderID+`","parentId":"`+inventory.SystemFolderID(userID, 8)+`","name":"Projects","typeDefault":-1}`,
		http.StatusConflict)
}
