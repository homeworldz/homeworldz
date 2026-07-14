package inventory

import "testing"

func TestLibraryCatalogIsStableAndReadOnlyData(t *testing.T) {
	folders := LibraryFolders()
	items := LibraryItems()
	if len(folders) != 7 || folders[0].ID != LibraryRootID || folders[0].ParentID != zeroUUID ||
		folders[1].ID != LibraryClothingID || folders[1].ParentID != LibraryRootID ||
		folders[2].ID != LibraryBodyPartsID || folders[2].ParentID != LibraryRootID ||
		folders[3].ID != LibraryTexturesID || folders[3].ParentID != LibraryRootID ||
		folders[4].ID != LibraryTerrainID || folders[4].ParentID != LibraryTexturesID ||
		folders[5].ID != LibraryInitialOutfitsID || folders[5].ParentID != LibraryClothingID ||
		folders[6].ID != LibraryDefaultAvatarID || folders[6].ParentID != LibraryInitialOutfitsID || len(items) != 10 {
		t.Fatalf("invalid library catalog: folders=%#v items=%#v", folders, items)
	}
	for _, item := range items {
		if item.OwnerUserID != LibraryOwnerID ||
			(item.FolderID != LibraryDefaultAvatarID && item.FolderID != LibraryTerrainID) ||
			(item.AssetType != 0 && item.AssetType != 5 && item.AssetType != 13) ||
			(item.AssetType == 0 && item.InventoryType != 0) ||
			(item.AssetType != 0 && item.InventoryType != 18) ||
			item.CreatorUserID != LibraryOwnerID {
			t.Fatalf("invalid library item: %#v", item)
		}
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
