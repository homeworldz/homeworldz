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
		"0c5f60ae-1a54-1ee8-398d-7ed278552132", "2d408f13-f437-e00e-8f6d-b0857580b034",
		"aa9d6d53-2647-f731-97eb-e07a47febb57", "5725fae1-0087-ec20-a4a0-eaa82d5c121b",
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
