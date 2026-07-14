package inventory

const (
	LibraryRootID           = "00000000-0000-0000-0000-000000000001"
	LibraryOwnerID          = "00000000-0000-0000-0000-000000000002"
	LibraryClothingID       = "b75056e0-b9bf-11dc-95ff-0800200c9a66"
	LibraryBodyPartsID      = "d499e5e0-b9bf-11dc-95ff-0800200c9a66"
	LibraryTexturesID       = "e6e4a35f-b9bf-11dc-95ff-0800200c9a66"
	LibraryTerrainID        = "00000000-0000-4000-8000-000000000020"
	LibraryInitialOutfitsID = "00000000-0000-4000-8000-000000000010"
	LibraryDefaultAvatarID  = "00000000-0000-4000-8000-000000000011"
)

// LibraryFolders is the grid-owned, read-only inventory catalog. Its stable
// identifiers are shared by every viewer session and are not user inventory.
func LibraryFolders() []Folder {
	return []Folder{
		{ID: LibraryRootID, OwnerUserID: LibraryOwnerID, ParentID: zeroUUID,
			Name: "Library", TypeDefault: 8, Version: 1},
		{ID: LibraryClothingID, OwnerUserID: LibraryOwnerID, ParentID: LibraryRootID,
			Name: "Clothing", TypeDefault: 5, Version: 1},
		{ID: LibraryBodyPartsID, OwnerUserID: LibraryOwnerID, ParentID: LibraryRootID,
			Name: "Body Parts", TypeDefault: 13, Version: 1},
		{ID: LibraryTexturesID, OwnerUserID: LibraryOwnerID, ParentID: LibraryRootID,
			Name: "Textures", TypeDefault: 0, Version: 1},
		{ID: LibraryTerrainID, OwnerUserID: LibraryOwnerID, ParentID: LibraryTexturesID,
			Name: "Terrain", TypeDefault: -1, Version: 1},
		{ID: LibraryInitialOutfitsID, OwnerUserID: LibraryOwnerID, ParentID: LibraryClothingID,
			Name: "Initial Outfits", TypeDefault: -1, Version: 1},
		{ID: LibraryDefaultAvatarID, OwnerUserID: LibraryOwnerID, ParentID: LibraryInitialOutfitsID,
			Name: "Default Avatar", TypeDefault: -1, Version: 1},
	}
}

func LibraryItems() []Item {
	const fullPermissions = uint32(0x7fffffff)
	return []Item{
		{ID: "00000000-0000-4000-8000-000000000021", OwnerUserID: LibraryOwnerID,
			CreatorUserID: LibraryOwnerID, FolderID: LibraryTerrainID,
			AssetID: "b8d3965a-ad78-bf43-699b-bff8eca6c975", AssetType: 0, InventoryType: 0,
			Name: "Terrain Sand and Dirt", BasePermissions: fullPermissions,
			CurrentPermissions: fullPermissions, EveryonePermissions: fullPermissions, NextPermissions: fullPermissions},
		{ID: "00000000-0000-4000-8000-000000000022", OwnerUserID: LibraryOwnerID,
			CreatorUserID: LibraryOwnerID, FolderID: LibraryTerrainID,
			AssetID: "abb783e6-3e93-26c0-248a-247666855da3", AssetType: 0, InventoryType: 0,
			Name: "Terrain Grass", BasePermissions: fullPermissions,
			CurrentPermissions: fullPermissions, EveryonePermissions: fullPermissions, NextPermissions: fullPermissions},
		{ID: "00000000-0000-4000-8000-000000000023", OwnerUserID: LibraryOwnerID,
			CreatorUserID: LibraryOwnerID, FolderID: LibraryTerrainID,
			AssetID: "179cdabd-398a-9b6b-1391-4dc333ba321f", AssetType: 0, InventoryType: 0,
			Name: "Terrain Mountain", BasePermissions: fullPermissions,
			CurrentPermissions: fullPermissions, EveryonePermissions: fullPermissions, NextPermissions: fullPermissions},
		{ID: "00000000-0000-4000-8000-000000000024", OwnerUserID: LibraryOwnerID,
			CreatorUserID: LibraryOwnerID, FolderID: LibraryTerrainID,
			AssetID: "beb169c7-11ea-fff2-efe5-0f24dc881df2", AssetType: 0, InventoryType: 0,
			Name: "Terrain Rock", BasePermissions: fullPermissions,
			CurrentPermissions: fullPermissions, EveryonePermissions: fullPermissions, NextPermissions: fullPermissions},
		{ID: "5c86b033-b9cc-11dc-95ff-0800200c9a66", OwnerUserID: LibraryOwnerID,
			CreatorUserID: LibraryOwnerID,
			FolderID:      LibraryDefaultAvatarID, AssetID: "66c41e39-38f9-f75a-024e-585989bfab73",
			AssetType: 13, InventoryType: 18, Name: "Default Shape", Flags: 0,
			BasePermissions: fullPermissions, CurrentPermissions: fullPermissions,
			EveryonePermissions: fullPermissions, NextPermissions: fullPermissions},
		{ID: "5c86b030-b9cc-11dc-95ff-0800200c9a66", OwnerUserID: LibraryOwnerID,
			CreatorUserID: LibraryOwnerID,
			FolderID:      LibraryDefaultAvatarID, AssetID: "77c41e39-38f9-f75a-024e-585989bbabbb",
			AssetType: 13, InventoryType: 18, Name: "Default Skin", Flags: 1,
			BasePermissions: fullPermissions, CurrentPermissions: fullPermissions,
			EveryonePermissions: fullPermissions, NextPermissions: fullPermissions},
		{ID: "d342e6c1-b9d2-11dc-95ff-0800200c9a66", OwnerUserID: LibraryOwnerID,
			CreatorUserID: LibraryOwnerID,
			FolderID:      LibraryDefaultAvatarID, AssetID: "d342e6c0-b9d2-11dc-95ff-0800200c9a66",
			AssetType: 13, InventoryType: 18, Name: "Default Hair", Flags: 2,
			BasePermissions: fullPermissions, CurrentPermissions: fullPermissions,
			EveryonePermissions: fullPermissions, NextPermissions: fullPermissions},
		{ID: "4bb6fa4e-1cd2-498a-a84c-95c1a0e745a7", OwnerUserID: LibraryOwnerID,
			CreatorUserID: LibraryOwnerID,
			FolderID:      LibraryDefaultAvatarID, AssetID: "4bb6fa4d-1cd2-498a-a84c-95c1a0e745a7",
			AssetType: 13, InventoryType: 18, Name: "Default Eyes", Flags: 3,
			BasePermissions: fullPermissions, CurrentPermissions: fullPermissions,
			EveryonePermissions: fullPermissions, NextPermissions: fullPermissions},
		{ID: "d5e46210-b9d1-11dc-95ff-0800200c9a66", OwnerUserID: LibraryOwnerID,
			CreatorUserID: LibraryOwnerID,
			FolderID:      LibraryDefaultAvatarID, AssetID: "00000000-38f9-1111-024e-222222111110",
			AssetType: 5, InventoryType: 18, Name: "Default Shirt", Flags: 4,
			BasePermissions: fullPermissions, CurrentPermissions: fullPermissions,
			EveryonePermissions: fullPermissions, NextPermissions: fullPermissions},
		{ID: "d5e46211-b9d1-11dc-95ff-0800200c9a66", OwnerUserID: LibraryOwnerID,
			CreatorUserID: LibraryOwnerID,
			FolderID:      LibraryDefaultAvatarID, AssetID: "00000000-38f9-1111-024e-222222111120",
			AssetType: 5, InventoryType: 18, Name: "Default Pants", Flags: 5,
			BasePermissions: fullPermissions, CurrentPermissions: fullPermissions,
			EveryonePermissions: fullPermissions, NextPermissions: fullPermissions},
	}
}

// IsLibraryFolder reports whether id belongs to the shared catalog. Capability
// handlers use it to keep library reads separate from personal inventory.
func IsLibraryFolder(id string) bool {
	for _, folder := range LibraryFolders() {
		if folder.ID == id {
			return true
		}
	}
	return false
}
