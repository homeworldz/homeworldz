package inventory

import "testing"

func TestSystemFoldersAreStableAndComplete(t *testing.T) {
	const userID = "11111111-2222-4333-8444-555555555555"
	first := SystemFolders(userID)
	second := SystemFolders(userID)
	if len(first) != 23 || len(second) != len(first) {
		t.Fatalf("system folder count = %d, want 23", len(first))
	}
	seen := make(map[int]bool)
	for index, folder := range first {
		if folder != second[index] || folder.OwnerUserID != userID ||
			(seen[folder.TypeDefault] && folder.TypeDefault != 2) {
			t.Fatalf("invalid system folder %d: %#v", index, folder)
		}
		seen[folder.TypeDefault] = true
		if index == 0 {
			if folder.TypeDefault != 8 || folder.ParentID != zeroUUID {
				t.Fatalf("invalid root folder: %#v", folder)
			}
		} else if folder.Name != "Friends" && folder.Name != "All" && folder.ParentID != first[0].ID {
			t.Fatalf("folder parent = %q, want %q", folder.ParentID, first[0].ID)
		}
	}
	callingCardsID := SystemFolderID(userID, 2)
	if first[21].Name != "Friends" || first[21].TypeDefault != 2 || first[21].ParentID != callingCardsID ||
		first[22].Name != "All" || first[22].TypeDefault != 2 || first[22].ParentID != first[21].ID {
		t.Fatalf("calling-card hierarchy = %#v", first[21:])
	}
}

func TestDefaultWearablesAreStableAndLinkedFromCurrentOutfit(t *testing.T) {
	const userID = "11111111-2222-4333-8444-555555555555"
	first := DefaultWearables(userID)
	second := DefaultWearables(userID)
	if len(first) != 12 || len(second) != len(first) {
		t.Fatalf("default wearable count = %d, want 12", len(first))
	}
	bodyPartsID := SystemFolderID(userID, 13)
	clothingID := SystemFolderID(userID, 5)
	currentOutfitID := SystemFolderID(userID, 46)
	wantAssets := []string{
		"66c41e39-38f9-f75a-024e-585989bfab73", "77c41e39-38f9-f75a-024e-585989bbabbb",
		"d342e6c0-b9d2-11dc-95ff-0800200c9a66", "4bb6fa4d-1cd2-498a-a84c-95c1a0e745a7",
		"00000000-38f9-1111-024e-222222111110", "00000000-38f9-1111-024e-222222111120",
	}
	for index := 0; index < len(first); index += 2 {
		item, link := first[index], first[index+1]
		wantFolderID, wantAssetType := bodyPartsID, 13
		if index >= 8 {
			wantFolderID, wantAssetType = clothingID, 5
		}
		if item != second[index] || link != second[index+1] || item.FolderID != wantFolderID ||
			item.AssetType != wantAssetType || item.InventoryType != 18 || link.FolderID != currentOutfitID ||
			link.AssetType != 24 || link.InventoryType != 18 || link.AssetID != item.ID ||
			item.AssetID != wantAssets[index/2] || item.Flags != uint32(index/2) || link.Flags != item.Flags {
			t.Fatalf("invalid default wearable pair %d: item=%#v link=%#v", index/2, item, link)
		}
	}
}

func TestDefaultOutfitInitializedUsesStableShapeMarker(t *testing.T) {
	const userID = "11111111-2222-4333-8444-555555555555"
	items := DefaultWearables(userID)
	if DefaultOutfitInitialized(userID, nil) ||
		DefaultOutfitInitialized("aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee", items) ||
		!DefaultOutfitInitialized(userID, items[:1]) {
		t.Fatal("default outfit initialization marker was not recognized correctly")
	}
}

func TestDefaultOutfitRepairRequiresEmptyCOFAndAllSources(t *testing.T) {
	const userID = "11111111-2222-4333-8444-555555555555"
	defaults := DefaultWearables(userID)
	sources := make([]Item, 0, len(defaults)/2)
	for index := 0; index < len(defaults); index += 2 {
		sources = append(sources, defaults[index])
	}
	if !DefaultOutfitNeedsRepair(userID, sources) {
		t.Fatal("empty Current Outfit with all default sources was not repairable")
	}
	if DefaultOutfitNeedsRepair(userID, sources[:len(sources)-1]) {
		t.Fatal("incomplete default sources unexpectedly allowed an outfit repair")
	}
	if DefaultOutfitNeedsRepair(userID, append(sources, defaults[1])) {
		t.Fatal("non-empty Current Outfit unexpectedly allowed an outfit repair")
	}
}
