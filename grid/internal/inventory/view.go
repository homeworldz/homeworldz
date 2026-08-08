package inventory

// Read-side composition shared by every inventory surface: the AIS capability a
// viewer uses and the JSON route the Homeworldz client uses. Both answer the
// same questions of the same rows and differ only in what they serialize, so the
// answering lives here and the encoding lives with each caller.
//
// These take the *already loaded* folder and item sets rather than a store and a
// user id, and that is the point. `ListFolders` and `ListItems` are both scoped
// by user, so a caller can only pass in rows it already fetched for the acting
// user, and a folder belonging to somebody else is not absent by a check that
// could be forgotten — it was never in the slice. Ownership is enforced by the
// shape of the call rather than by remembering to compare an id.

// FolderByID finds a folder within a user's own folders. The bool is false when
// the id names no folder that user has, which is the same answer as a folder
// that does not exist at all: a caller must not be able to tell those apart, or
// the response distinguishes "someone else owns this" from "no such thing".
func FolderByID(folders []Folder, folderID string) (Folder, bool) {
	for _, folder := range folders {
		if folder.ID == folderID {
			return folder, true
		}
	}
	return Folder{}, false
}

// ChildFolders returns the direct subfolders of parentID, in the order the store
// returned them.
func ChildFolders(folders []Folder, parentID string) []Folder {
	children := make([]Folder, 0)
	for _, folder := range folders {
		if folder.ParentID == parentID {
			children = append(children, folder)
		}
	}
	return children
}

// ItemsIn returns the items held directly by folderID.
func ItemsIn(items []Item, folderID string) []Item {
	held := make([]Item, 0)
	for _, item := range items {
		if item.FolderID == folderID {
			held = append(held, item)
		}
	}
	return held
}

// ItemByID finds an item within a user's own items, with the same
// indistinguishability property as FolderByID.
func ItemByID(items []Item, itemID string) (Item, bool) {
	for _, item := range items {
		if item.ID == itemID {
			return item, true
		}
	}
	return Item{}, false
}
