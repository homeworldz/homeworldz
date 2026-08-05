package inventory

import "testing"

func TestLibraryCatalogIsStableAndReadOnlyData(t *testing.T) {
	folders := LibraryFolders()
	items := LibraryItems()
	if len(folders) != 7 || folders[0].ID != LibraryRootID || folders[0].ParentID != zeroUUID ||
		folders[1].ID != LibraryClothingID || folders[1].ParentID != LibraryRootID ||
		folders[2].ID != LibraryBodyPartsID || folders[2].ParentID != LibraryRootID ||
		folders[3].ID != LibraryTexturesID || folders[3].ParentID != LibraryRootID || folders[3].Version != 3 ||
		folders[4].ID != LibraryTerrainID || folders[4].ParentID != LibraryTexturesID ||
		folders[5].ID != LibraryInitialOutfitsID || folders[5].ParentID != LibraryClothingID ||
		folders[6].ID != LibraryDefaultAvatarID || folders[6].ParentID != LibraryInitialOutfitsID || len(items) != 14 {
		t.Fatalf("invalid library catalog: folders=%#v items=%#v", folders, items)
	}
	for _, item := range items {
		if item.OwnerUserID != LibraryOwnerID ||
			(item.FolderID != LibraryDefaultAvatarID && item.FolderID != LibraryTerrainID &&
				item.FolderID != LibraryTexturesID) ||
			(item.AssetType != 0 && item.AssetType != 5 && item.AssetType != 13) ||
			(item.AssetType == 0 && item.InventoryType != 0) ||
			(item.AssetType != 0 && item.InventoryType != 18) ||
			item.CreatorUserID != LibraryOwnerID {
			t.Fatalf("invalid library item: %#v", item)
		}
	}
	if items[4].Name != "Blank" || items[4].AssetID != "5748decc-f629-461c-9a36-a35a221fe21f" ||
		items[5].Name != "Plywood" || items[5].AssetID != "89556747-24cb-43ed-920b-47caed15465f" ||
		items[6].Name != "Transparent" || items[6].AssetID != "8dcd4a48-2d37-4909-9f78-f7a9eb4ef903" ||
		items[7].Name != "Media" || items[7].AssetID != "8b5fec65-8d8d-9dc5-cda8-8fdf2716e361" {
		t.Fatalf("standard texture catalog entries are invalid: %#v", items[4:8])
	}
	for _, folder := range folders {
		if !IsLibraryFolder(folder.ID) {
			t.Fatalf("library folder was not recognized: %#v", folder)
		}
	}
	if IsLibraryFolder("ffffffff-ffff-4fff-8fff-ffffffffffff") {
		t.Fatal("unrelated folder was recognized as library data")
	}
}
