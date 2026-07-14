package inventory

import "testing"

func TestSystemFoldersAreStableAndComplete(t *testing.T) {
	const userID = "11111111-2222-4333-8444-555555555555"
	first := SystemFolders(userID)
	second := SystemFolders(userID)
	if len(first) != 21 || len(second) != len(first) {
		t.Fatalf("system folder count = %d, want 21", len(first))
	}
	seen := make(map[int]bool)
	for index, folder := range first {
		if folder != second[index] || folder.OwnerUserID != userID || seen[folder.TypeDefault] {
			t.Fatalf("invalid system folder %d: %#v", index, folder)
		}
		seen[folder.TypeDefault] = true
		if index == 0 {
			if folder.TypeDefault != 8 || folder.ParentID != zeroUUID {
				t.Fatalf("invalid root folder: %#v", folder)
			}
		} else if folder.ParentID != first[0].ID {
			t.Fatalf("folder parent = %q, want %q", folder.ParentID, first[0].ID)
		}
	}
}

func TestDefaultWearablesAreStableAndLinkedFromCurrentOutfit(t *testing.T) {
	const userID = "11111111-2222-4333-8444-555555555555"
	first := DefaultWearables(userID)
	second := DefaultWearables(userID)
	if len(first) != 8 || len(second) != len(first) {
		t.Fatalf("default wearable count = %d, want 8", len(first))
	}
	bodyPartsID := SystemFolderID(userID, 13)
	currentOutfitID := SystemFolderID(userID, 46)
	wantAssets := []string{
		"66c41e39-38f9-f75a-024e-585989bfab73", "77c41e39-38f9-f75a-024e-585989bbabbb",
		"d342e6c0-b9d2-11dc-95ff-0800200c9a66", "4bb6fa4d-1cd2-498a-a84c-95c1a0e745a7",
	}
	for index := 0; index < len(first); index += 2 {
		item, link := first[index], first[index+1]
		if item != second[index] || link != second[index+1] || item.FolderID != bodyPartsID ||
			item.AssetType != 13 || item.InventoryType != 18 || link.FolderID != currentOutfitID ||
			link.AssetType != 24 || link.InventoryType != 18 || link.AssetID != item.ID ||
			item.AssetID != wantAssets[index/2] || item.Flags != uint32(index/2) || link.Flags != item.Flags {
			t.Fatalf("invalid default wearable pair %d: item=%#v link=%#v", index/2, item, link)
		}
	}
}
