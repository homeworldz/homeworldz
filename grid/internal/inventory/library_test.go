package inventory

import "testing"

func TestLibraryCatalogIsStableAndReadOnlyData(t *testing.T) {
	folders := LibraryFolders()
	items := LibraryItems()
	if len(folders) != 8 || folders[0].ID != LibraryRootID || folders[0].ParentID != zeroUUID ||
		folders[1].ID != LibraryClothingID || folders[1].ParentID != LibraryRootID ||
		folders[2].ID != LibraryBodyPartsID || folders[2].ParentID != LibraryRootID ||
		folders[3].ID != LibraryTexturesID || folders[3].ParentID != LibraryRootID || folders[3].Version != 3 ||
		folders[4].ID != LibraryTerrainID || folders[4].ParentID != LibraryTexturesID ||
		folders[5].ID != LibraryInitialOutfitsID || folders[5].ParentID != LibraryClothingID ||
		folders[6].ID != LibraryDefaultAvatarID || folders[6].ParentID != LibraryInitialOutfitsID ||
		folders[7].ID != LibraryBodiesID || folders[7].ParentID != LibraryRootID || len(items) != 16 {
		t.Fatalf("invalid library catalog: folders=%#v items=%#v", folders, items)
	}
	for _, item := range items {
		// Asset type 6 is object, and an object's inventory type is 6 as well —
		// unlike wearables, which are inventory type 18 whatever their asset type.
		// A mesh body is an object in this protocol family, not a body part.
		if item.OwnerUserID != LibraryOwnerID ||
			(item.FolderID != LibraryDefaultAvatarID && item.FolderID != LibraryTerrainID &&
				item.FolderID != LibraryTexturesID && item.FolderID != LibraryBodiesID) ||
			(item.AssetType != 0 && item.AssetType != 5 && item.AssetType != 6 && item.AssetType != 13) ||
			(item.AssetType == 0 && item.InventoryType != 0) ||
			(item.AssetType == 6 && item.InventoryType != 6) ||
			(item.AssetType != 0 && item.AssetType != 6 && item.InventoryType != 18) ||
			item.CreatorUserID != LibraryOwnerID {
			t.Fatalf("invalid library item: %#v", item)
		}
	}
	// The bodies are last, so nothing above them shifts when they are added, and
	// asserted by name and asset id because the whole point of bundling them is
	// that a specific pair of assets is reachable.
	if items[14].Name != "Female" || items[14].AssetID != "5a2e7f10-8c34-4b96-a1d7-6e3f92b45c08" ||
		items[15].Name != "Male" || items[15].AssetID != "9e6a3d72-4f18-4c85-b23f-8d51e07ba946" ||
		items[14].FolderID != LibraryBodiesID || items[15].FolderID != LibraryBodiesID {
		t.Fatalf("library body entries are invalid: %#v", items[14:16])
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
