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
		if existing.TypeDefault != -1 || folder.TypeDefault != -1 || folder.ParentID == folder.ID {
			return inventory.Folder{}, inventory.ErrInvalidFolder
		}
		destinationFound := existing.ParentID == folder.ParentID
		if !destinationFound {
			for _, candidate := range s.folders[folder.OwnerUserID] {
				if candidate.ID == folder.ParentID {
					destinationFound = true
					break
				}
			}
		}
		if !destinationFound {
			return inventory.Folder{}, inventory.ErrFolderNotFound
		}
		for parent := folder.ParentID; parent != "00000000-0000-0000-0000-000000000000"; {
			if parent == folder.ID {
				return inventory.Folder{}, inventory.ErrInvalidFolder
			}
			next := "00000000-0000-0000-0000-000000000000"
			for _, candidate := range s.folders[folder.OwnerUserID] {
				if candidate.ID == parent {
					next = candidate.ParentID
					break
				}
			}
			parent = next
		}
		folder.Version = existing.Version + 1
		s.folders[folder.OwnerUserID][index] = folder
		if existing.ParentID != folder.ParentID {
			for parentIndex := range s.folders[folder.OwnerUserID] {
				parent := &s.folders[folder.OwnerUserID][parentIndex]
				if parent.ID == existing.ParentID || parent.ID == folder.ParentID {
					parent.Version++
				}
			}
		}
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

func (s *memoryInventoryStore) CreateItem(_ context.Context, item inventory.Item) (inventory.Item, error) {
	if s.items == nil {
		s.items = make(map[string][]inventory.Item)
	}
	folderFound := false
	for index := range s.folders[item.OwnerUserID] {
		if s.folders[item.OwnerUserID][index].ID == item.FolderID {
			s.folders[item.OwnerUserID][index].Version++
			folderFound = true
		}
	}
	if !folderFound {
		return inventory.Item{}, inventory.ErrItemFolderNotFound
	}
	for _, existing := range s.items[item.OwnerUserID] {
		if existing.ID == item.ID {
			return inventory.Item{}, inventory.ErrItemConflict
		}
	}
	s.items[item.OwnerUserID] = append(s.items[item.OwnerUserID], item)
	return item, nil
}

func (s *memoryInventoryStore) CreateItems(_ context.Context, items []inventory.Item) ([]inventory.Item, error) {
	if len(items) == 0 || len(items) > 256 {
		return nil, inventory.ErrInvalidItem
	}
	ownerID, folderID := items[0].OwnerUserID, items[0].FolderID
	folderIndex := -1
	for index, folder := range s.folders[ownerID] {
		if folder.ID == folderID {
			folderIndex = index
			break
		}
	}
	if folderIndex < 0 {
		return nil, inventory.ErrItemFolderNotFound
	}
	seen := make(map[string]bool, len(items))
	for _, item := range items {
		if item.OwnerUserID != ownerID || item.FolderID != folderID || item.ID == "" ||
			item.CreatorUserID == "" || item.AssetID == "" || item.Name == "" || seen[item.ID] {
			return nil, inventory.ErrInvalidItem
		}
		seen[item.ID] = true
		for _, existing := range s.items[item.OwnerUserID] {
			if existing.ID == item.ID {
				return nil, inventory.ErrItemConflict
			}
		}
	}
	s.items[ownerID] = append(s.items[ownerID], items...)
	s.folders[ownerID][folderIndex].Version += int64(len(items))
	return items, nil
}

func (s *memoryInventoryStore) UpdateItem(_ context.Context, item inventory.Item) (inventory.Item, error) {
	for itemIndex, existing := range s.items[item.OwnerUserID] {
		if existing.ID != item.ID {
			continue
		}
		destinationFound := false
		for folderIndex := range s.folders[item.OwnerUserID] {
			if s.folders[item.OwnerUserID][folderIndex].ID == item.FolderID {
				destinationFound = true
				break
			}
		}
		if !destinationFound {
			return inventory.Item{}, inventory.ErrItemFolderNotFound
		}
		for folderIndex := range s.folders[item.OwnerUserID] {
			folder := &s.folders[item.OwnerUserID][folderIndex]
			if folder.ID == existing.FolderID || (existing.FolderID != item.FolderID && folder.ID == item.FolderID) {
				folder.Version++
			}
		}
		item.CreatorUserID = existing.CreatorUserID
		item.AssetID = existing.AssetID
		item.AssetType = existing.AssetType
		item.InventoryType = existing.InventoryType
		item.CreatedAt = existing.CreatedAt
		s.items[item.OwnerUserID][itemIndex] = item
		return item, nil
	}
	return inventory.Item{}, inventory.ErrItemNotFound
}

func (s *memoryInventoryStore) DeleteItem(_ context.Context, userID, itemID string) (inventory.Item, error) {
	for itemIndex, item := range s.items[userID] {
		if item.ID != itemID {
			continue
		}
		s.items[userID] = append(s.items[userID][:itemIndex], s.items[userID][itemIndex+1:]...)
		for folderIndex := range s.folders[userID] {
			if s.folders[userID][folderIndex].ID == item.FolderID {
				s.folders[userID][folderIndex].Version++
			}
		}
		return item, nil
	}
	return inventory.Item{}, inventory.ErrItemNotFound
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
	moved := requestRegion[inventory.Folder](t, handler, http.MethodPut,
		"/api/v1/inventory/"+userID+"/folders/"+folderID,
		`{"parentId":"`+inventory.SystemFolderID(userID, 14)+`"}`,
		http.StatusOK)
	if moved.ParentID != inventory.SystemFolderID(userID, 14) || moved.Name != "Projects" {
		t.Fatalf("moved inventory folder = %#v", moved)
	}
}

func TestCreateTextureInventoryItemEndpoint(t *testing.T) {
	const userID = "20000000-0000-4000-8000-000000000001"
	store := &memoryInventoryStore{folders: make(map[string][]inventory.Folder)}
	_, _ = store.EnsureSystemFolders(context.Background(), userID)
	handler := New(checker{}, "test", Options{ServiceToken: "secret", Inventory: store})
	folderID := inventory.SystemFolderID(userID, 0)
	body := `{"id":"40000000-0000-4000-8000-000000000010",` +
		`"creatorUserId":"` + userID + `","folderId":"` + folderID + `",` +
		`"assetId":"50000000-0000-4000-8000-000000000010","assetType":0,` +
		`"inventoryType":0,"name":"Uploaded Terrain","description":"Library source",` +
		`"everyonePermissions":0,"nextPermissions":2147483647}`
	created := requestRegion[inventory.Item](t, handler, http.MethodPost,
		"/api/v1/inventory/"+userID+"/items", body, http.StatusCreated)
	if created.OwnerUserID != userID || created.CreatorUserID != userID ||
		created.FolderID != folderID || created.AssetType != 0 || created.InventoryType != 0 ||
		created.BasePermissions != 0x7fffffff || created.CurrentPermissions != 0x7fffffff {
		t.Fatalf("created inventory item = %#v", created)
	}
	requestRegion[Error](t, handler, http.MethodPost,
		"/api/v1/inventory/"+userID+"/items", body, http.StatusConflict)
}

func TestCopyLibraryInventoryItemEndpoint(t *testing.T) {
	const userID = "20000000-0000-4000-8000-000000000001"
	store := &memoryInventoryStore{folders: make(map[string][]inventory.Folder)}
	_, _ = store.EnsureSystemFolders(context.Background(), userID)
	handler := New(checker{}, "test", Options{ServiceToken: "secret", Inventory: store})
	source := inventory.LibraryItems()[4]
	created := requestRegion[inventory.Item](t, handler, http.MethodPost,
		"/api/v1/inventory/"+userID+"/copy-library-item",
		`{"sourceItemId":"`+source.ID+`","destinationFolderId":"00000000-0000-0000-0000-000000000000","name":""}`,
		http.StatusCreated)
	if created.ID == "" || created.ID == source.ID || created.OwnerUserID != userID ||
		created.CreatorUserID != inventory.LibraryOwnerID ||
		created.FolderID != inventory.SystemFolderID(userID, 13) || created.AssetID != source.AssetID ||
		created.AssetType != source.AssetType || created.InventoryType != source.InventoryType ||
		created.Name != source.Name || created.Flags != source.Flags ||
		created.BasePermissions != source.BasePermissions || created.NextPermissions != source.NextPermissions {
		t.Fatalf("copied inventory item = %#v", created)
	}
	missing := requestRegion[Error](t, handler, http.MethodPost,
		"/api/v1/inventory/"+userID+"/copy-library-item",
		`{"sourceItemId":"40000000-0000-4000-8000-000000000099","destinationFolderId":"00000000-0000-0000-0000-000000000000","name":""}`,
		http.StatusNotFound)
	if missing.Code != "library_item_not_found" {
		t.Fatalf("missing library copy error = %#v", missing)
	}
}
