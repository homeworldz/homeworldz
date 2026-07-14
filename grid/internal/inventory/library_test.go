package inventory

import "testing"

func TestLibraryCatalogIsStableAndReadOnlyData(t *testing.T) {
	folders := LibraryFolders()
	items := LibraryItems()
	if len(folders) != 5 || folders[0].ID != LibraryRootID || folders[0].ParentID != zeroUUID ||
		folders[1].ID != LibraryClothingID || folders[1].ParentID != LibraryRootID ||
		folders[2].ID != LibraryBodyPartsID || folders[2].ParentID != LibraryRootID ||
		folders[3].ID != LibraryInitialOutfitsID || folders[3].ParentID != LibraryClothingID ||
		folders[4].ID != LibraryDefaultAvatarID || folders[4].ParentID != LibraryInitialOutfitsID || len(items) != 6 {
		t.Fatalf("invalid library catalog: folders=%#v items=%#v", folders, items)
	}
	for _, item := range items {
		if item.OwnerUserID != LibraryOwnerID || item.FolderID != LibraryDefaultAvatarID ||
			(item.AssetType != 5 && item.AssetType != 13) || item.InventoryType != 18 || item.CreatorUserID != "" {
			t.Fatalf("invalid library item: %#v", item)
		}
	}
}
