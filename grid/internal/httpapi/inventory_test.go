package httpapi

import (
	"context"
	"net/http"
	"testing"

	"github.com/homeworldz/homeworldz/grid/internal/inventory"
)

type memoryInventoryStore struct{ folders map[string][]inventory.Folder }

func (s *memoryInventoryStore) EnsureSystemFolders(_ context.Context, userID string) ([]inventory.Folder, error) {
	values := inventory.SystemFolders(userID)
	s.folders[userID] = values
	return values, nil
}

func (s *memoryInventoryStore) ListFolders(_ context.Context, userID string) ([]inventory.Folder, error) {
	return s.folders[userID], nil
}

func (s *memoryInventoryStore) EnsureItem(context.Context, inventory.Item) (bool, error) {
	return true, nil
}

func (s *memoryInventoryStore) ListItems(context.Context, string) ([]inventory.Item, error) {
	return nil, nil
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
	requestRegion[Error](t, handler, http.MethodPost,
		"/api/v1/inventory/"+userID+"/folders", "", http.StatusMethodNotAllowed)
}
