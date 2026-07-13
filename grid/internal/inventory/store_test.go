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
