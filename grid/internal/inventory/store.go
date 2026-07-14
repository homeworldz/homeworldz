package inventory

import (
	"context"
	"crypto/sha256"
	"database/sql"
	"encoding/hex"
	"errors"
	"fmt"
	"strconv"
	"strings"
	"time"
)

const zeroUUID = "00000000-0000-0000-0000-000000000000"

var (
	ErrFolderConflict = errors.New("inventory folder already exists")
	ErrFolderNotFound = errors.New("inventory parent folder not found")
	ErrInvalidFolder  = errors.New("inventory folder is invalid")
)

type Folder struct {
	ID          string `json:"id"`
	OwnerUserID string `json:"ownerUserId"`
	ParentID    string `json:"parentId"`
	Name        string `json:"name"`
	TypeDefault int    `json:"typeDefault"`
	Version     int64  `json:"version"`
}

type Item struct {
	ID                  string    `json:"id"`
	OwnerUserID         string    `json:"ownerUserId"`
	CreatorUserID       string    `json:"creatorUserId"`
	FolderID            string    `json:"folderId"`
	AssetID             string    `json:"assetId"`
	AssetType           int       `json:"assetType"`
	InventoryType       int       `json:"inventoryType"`
	Name                string    `json:"name"`
	Description         string    `json:"description"`
	Flags               uint32    `json:"flags"`
	BasePermissions     uint32    `json:"basePermissions"`
	CurrentPermissions  uint32    `json:"currentPermissions"`
	EveryonePermissions uint32    `json:"everyonePermissions"`
	NextPermissions     uint32    `json:"nextPermissions"`
	SaleType            int       `json:"saleType"`
	SalePrice           int       `json:"salePrice"`
	CreatedAt           time.Time `json:"createdAt"`
}

type Store interface {
	EnsureSystemFolders(context.Context, string) ([]Folder, error)
	CreateFolder(context.Context, Folder) (Folder, error)
	UpdateFolder(context.Context, Folder) (Folder, error)
	ListFolders(context.Context, string) ([]Folder, error)
	EnsureItem(context.Context, Item) (bool, error)
	ListItems(context.Context, string) ([]Item, error)
}

type PostgresStore struct{ db *sql.DB }

func NewPostgresStore(db *sql.DB) *PostgresStore { return &PostgresStore{db: db} }

func SystemFolderID(userID string, folderType int) string {
	return deterministicUUID(userID + "\x00homeworldz-inventory-folder\x00" + strconv.Itoa(folderType))
}

func deterministicUUID(seed string) string {
	value := sha256.Sum256([]byte(seed))
	value[6] = (value[6] & 0x0f) | 0x80
	value[8] = (value[8] & 0x3f) | 0x80
	encoded := hex.EncodeToString(value[:16])
	return encoded[0:8] + "-" + encoded[8:12] + "-" + encoded[12:16] + "-" +
		encoded[16:20] + "-" + encoded[20:32]
}

func DefaultWearables(userID string) []Item {
	const fullPermissions = uint32(0x7fffffff)
	bodyPartsID := SystemFolderID(userID, 13)
	currentOutfitID := SystemFolderID(userID, 46)
	definitions := []struct {
		name         string
		wearableType uint32
		assetID      string
	}{
		{"Default Shape", 0, "66c41e39-38f9-f75a-024e-585989bfab73"},
		{"Default Skin", 1, "77c41e39-38f9-f75a-024e-585989bbabbb"},
		{"Default Hair", 2, "d342e6c0-b9d2-11dc-95ff-0800200c9a66"},
		{"Default Eyes", 3, "4bb6fa4d-1cd2-498a-a84c-95c1a0e745a7"},
	}
	items := make([]Item, 0, len(definitions)*2)
	for _, definition := range definitions {
		itemID := deterministicUUID(userID + "\x00homeworldz-default-wearable-item\x00" +
			strconv.FormatUint(uint64(definition.wearableType), 10))
		items = append(items, Item{
			ID: itemID, OwnerUserID: userID, FolderID: bodyPartsID,
			AssetID:   definition.assetID,
			AssetType: 13, InventoryType: 18, Name: definition.name, Flags: definition.wearableType,
			BasePermissions: fullPermissions, CurrentPermissions: fullPermissions,
			NextPermissions: fullPermissions,
		})
		items = append(items, Item{
			ID: deterministicUUID(userID + "\x00homeworldz-current-outfit-link\x00" +
				strconv.FormatUint(uint64(definition.wearableType), 10)),
			OwnerUserID: userID, FolderID: currentOutfitID, AssetID: itemID,
			AssetType: 24, InventoryType: 18, Name: definition.name, Flags: definition.wearableType,
			BasePermissions: fullPermissions, CurrentPermissions: fullPermissions,
			NextPermissions: fullPermissions,
		})
	}
	return items
}

func SystemFolders(userID string) []Folder {
	rootID := SystemFolderID(userID, 8)
	folders := []Folder{{rootID, userID, zeroUUID, "My Inventory", 8, 1}}
	for _, folder := range []struct {
		name       string
		folderType int
	}{
		{"Textures", 0}, {"Sounds", 1}, {"Calling Cards", 2}, {"Landmarks", 3},
		{"Clothing", 5}, {"Objects", 6}, {"Notecards", 7}, {"Scripts", 10},
		{"Body Parts", 13}, {"Trash", 14}, {"Photo Album", 15}, {"Lost And Found", 16},
		{"Animations", 20}, {"Gestures", 21}, {"Favorites", 23}, {"Current Outfit", 46},
		{"My Outfits", 48}, {"Received Items", 50}, {"Settings", 56}, {"Materials", 57},
	} {
		folders = append(folders, Folder{SystemFolderID(userID, folder.folderType), userID, rootID,
			folder.name, folder.folderType, 1})
	}
	return folders
}

func (s *PostgresStore) EnsureSystemFolders(ctx context.Context, userID string) ([]Folder, error) {
	tx, err := s.db.BeginTx(ctx, nil)
	if err != nil {
		return nil, fmt.Errorf("begin inventory folder transaction: %w", err)
	}
	defer tx.Rollback()
	for _, folder := range SystemFolders(userID) {
		var parent any
		if folder.ParentID != zeroUUID {
			parent = folder.ParentID
		}
		if _, err := tx.ExecContext(ctx, `
			INSERT INTO inventory_folders (id, owner_user_id, parent_id, name, type_default, version)
			VALUES ($1, $2, $3, $4, $5, 1) ON CONFLICT (id) DO NOTHING`,
			folder.ID, folder.OwnerUserID, parent, folder.Name, folder.TypeDefault); err != nil {
			return nil, fmt.Errorf("ensure inventory folder type %d: %w", folder.TypeDefault, err)
		}
	}
	folders, err := listFolders(ctx, tx, userID)
	if err != nil {
		return nil, err
	}
	if err := tx.Commit(); err != nil {
		return nil, fmt.Errorf("commit inventory folders: %w", err)
	}
	return folders, nil
}

func (s *PostgresStore) ListFolders(ctx context.Context, userID string) ([]Folder, error) {
	return listFolders(ctx, s.db, userID)
}

func (s *PostgresStore) CreateFolder(ctx context.Context, folder Folder) (Folder, error) {
	folder.Name = strings.TrimSpace(folder.Name)
	if folder.ID == "" || folder.OwnerUserID == "" || folder.ParentID == "" ||
		folder.ParentID == zeroUUID || len(folder.Name) == 0 || len(folder.Name) > 255 ||
		folder.TypeDefault != -1 {
		return Folder{}, ErrInvalidFolder
	}
	tx, err := s.db.BeginTx(ctx, nil)
	if err != nil {
		return Folder{}, fmt.Errorf("begin inventory folder creation: %w", err)
	}
	defer tx.Rollback()
	var parentVersion int64
	if err := tx.QueryRowContext(ctx, `SELECT version FROM inventory_folders
		WHERE id = $1 AND owner_user_id = $2 FOR UPDATE`, folder.ParentID, folder.OwnerUserID).Scan(&parentVersion); errors.Is(err, sql.ErrNoRows) {
		return Folder{}, ErrFolderNotFound
	} else if err != nil {
		return Folder{}, fmt.Errorf("find inventory parent folder: %w", err)
	}
	result, err := tx.ExecContext(ctx, `INSERT INTO inventory_folders
		(id, owner_user_id, parent_id, name, type_default, version)
		VALUES ($1, $2, $3, $4, -1, 1) ON CONFLICT (id) DO NOTHING`,
		folder.ID, folder.OwnerUserID, folder.ParentID, folder.Name)
	if err != nil {
		return Folder{}, fmt.Errorf("create inventory folder: %w", err)
	}
	created, err := result.RowsAffected()
	if err != nil {
		return Folder{}, fmt.Errorf("count created inventory folder: %w", err)
	}
	if created == 0 {
		return Folder{}, ErrFolderConflict
	}
	if _, err := tx.ExecContext(ctx, `UPDATE inventory_folders
		SET version = version + 1, updated_at = now() WHERE id = $1 AND owner_user_id = $2`,
		folder.ParentID, folder.OwnerUserID); err != nil {
		return Folder{}, fmt.Errorf("update inventory parent version: %w", err)
	}
	if err := tx.Commit(); err != nil {
		return Folder{}, fmt.Errorf("commit inventory folder creation: %w", err)
	}
	folder.Version = 1
	return folder, nil
}

func (s *PostgresStore) UpdateFolder(ctx context.Context, folder Folder) (Folder, error) {
	folder.Name = strings.TrimSpace(folder.Name)
	if folder.ID == "" || folder.OwnerUserID == "" || folder.ParentID == "" ||
		len(folder.Name) == 0 || len(folder.Name) > 255 || folder.TypeDefault != -1 {
		return Folder{}, ErrInvalidFolder
	}
	tx, err := s.db.BeginTx(ctx, nil)
	if err != nil {
		return Folder{}, fmt.Errorf("begin inventory folder update: %w", err)
	}
	defer tx.Rollback()
	var existing Folder
	if err := tx.QueryRowContext(ctx, `SELECT id, owner_user_id, parent_id, name, type_default, version
		FROM inventory_folders WHERE id = $1 AND owner_user_id = $2 FOR UPDATE`,
		folder.ID, folder.OwnerUserID).Scan(&existing.ID, &existing.OwnerUserID, &existing.ParentID,
		&existing.Name, &existing.TypeDefault, &existing.Version); errors.Is(err, sql.ErrNoRows) {
		return Folder{}, ErrFolderNotFound
	} else if err != nil {
		return Folder{}, fmt.Errorf("find inventory folder for update: %w", err)
	}
	if existing.TypeDefault != -1 || existing.ParentID != folder.ParentID {
		return Folder{}, ErrInvalidFolder
	}
	if existing.Name == folder.Name {
		return existing, nil
	}
	if err := tx.QueryRowContext(ctx, `UPDATE inventory_folders
		SET name = $3, version = version + 1, updated_at = now()
		WHERE id = $1 AND owner_user_id = $2 RETURNING version`,
		folder.ID, folder.OwnerUserID, folder.Name).Scan(&folder.Version); err != nil {
		return Folder{}, fmt.Errorf("update inventory folder: %w", err)
	}
	if err := tx.Commit(); err != nil {
		return Folder{}, fmt.Errorf("commit inventory folder update: %w", err)
	}
	return folder, nil
}

func (s *PostgresStore) EnsureItem(ctx context.Context, item Item) (bool, error) {
	tx, err := s.db.BeginTx(ctx, nil)
	if err != nil {
		return false, fmt.Errorf("begin inventory item transaction: %w", err)
	}
	defer tx.Rollback()
	var creator any
	if item.CreatorUserID != "" && item.CreatorUserID != zeroUUID {
		creator = item.CreatorUserID
	}
	result, err := tx.ExecContext(ctx, `
		INSERT INTO inventory_items
			(id, owner_user_id, creator_user_id, folder_id, asset_id, asset_type, inventory_type,
			 name, description, flags, base_permissions, current_permissions, everyone_permissions,
			 next_permissions, sale_type, sale_price)
		VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, $14, $15, $16)
		ON CONFLICT (id) DO UPDATE SET
			creator_user_id = EXCLUDED.creator_user_id, folder_id = EXCLUDED.folder_id,
			asset_id = EXCLUDED.asset_id, asset_type = EXCLUDED.asset_type,
			inventory_type = EXCLUDED.inventory_type, name = EXCLUDED.name,
			description = EXCLUDED.description, flags = EXCLUDED.flags,
			base_permissions = EXCLUDED.base_permissions,
			current_permissions = EXCLUDED.current_permissions,
			everyone_permissions = EXCLUDED.everyone_permissions,
			next_permissions = EXCLUDED.next_permissions, sale_type = EXCLUDED.sale_type,
			sale_price = EXCLUDED.sale_price, updated_at = now()
		WHERE (inventory_items.creator_user_id, inventory_items.folder_id, inventory_items.asset_id,
			inventory_items.asset_type, inventory_items.inventory_type, inventory_items.name,
			inventory_items.description, inventory_items.flags, inventory_items.base_permissions,
			inventory_items.current_permissions, inventory_items.everyone_permissions,
			inventory_items.next_permissions, inventory_items.sale_type, inventory_items.sale_price)
		IS DISTINCT FROM
			(EXCLUDED.creator_user_id, EXCLUDED.folder_id, EXCLUDED.asset_id, EXCLUDED.asset_type,
			 EXCLUDED.inventory_type, EXCLUDED.name, EXCLUDED.description, EXCLUDED.flags,
			 EXCLUDED.base_permissions, EXCLUDED.current_permissions, EXCLUDED.everyone_permissions,
			 EXCLUDED.next_permissions, EXCLUDED.sale_type, EXCLUDED.sale_price)`,
		item.ID, item.OwnerUserID, creator, item.FolderID, item.AssetID,
		item.AssetType, item.InventoryType, item.Name, item.Description, item.Flags, item.BasePermissions,
		item.CurrentPermissions, item.EveryonePermissions, item.NextPermissions, item.SaleType, item.SalePrice)
	if err != nil {
		return false, fmt.Errorf("ensure inventory item: %w", err)
	}
	count, err := result.RowsAffected()
	if err != nil {
		return false, fmt.Errorf("count ensured inventory item: %w", err)
	}
	if count != 0 {
		if _, err := tx.ExecContext(ctx, `UPDATE inventory_folders
			SET version = version + 1, updated_at = now()
			WHERE id = $1 AND owner_user_id = $2`, item.FolderID, item.OwnerUserID); err != nil {
			return false, fmt.Errorf("update inventory folder version: %w", err)
		}
	}
	if err := tx.Commit(); err != nil {
		return false, fmt.Errorf("commit inventory item: %w", err)
	}
	return count != 0, nil
}

func (s *PostgresStore) ListItems(ctx context.Context, userID string) ([]Item, error) {
	rows, err := s.db.QueryContext(ctx, `
		SELECT id, owner_user_id, COALESCE(creator_user_id::text, $2), folder_id, asset_id,
		       asset_type, inventory_type, name, description, flags, base_permissions,
		       current_permissions, everyone_permissions, next_permissions, sale_type, sale_price, created_at
		FROM inventory_items WHERE owner_user_id = $1 ORDER BY folder_id, name, id`, userID, zeroUUID)
	if err != nil {
		return nil, fmt.Errorf("list inventory items: %w", err)
	}
	defer rows.Close()
	var items []Item
	for rows.Next() {
		var item Item
		if err := rows.Scan(&item.ID, &item.OwnerUserID, &item.CreatorUserID, &item.FolderID, &item.AssetID,
			&item.AssetType, &item.InventoryType, &item.Name, &item.Description, &item.Flags,
			&item.BasePermissions, &item.CurrentPermissions, &item.EveryonePermissions, &item.NextPermissions,
			&item.SaleType, &item.SalePrice, &item.CreatedAt); err != nil {
			return nil, fmt.Errorf("scan inventory item: %w", err)
		}
		items = append(items, item)
	}
	if err := rows.Err(); err != nil {
		return nil, fmt.Errorf("iterate inventory items: %w", err)
	}
	return items, nil
}

type folderQuery interface {
	QueryContext(context.Context, string, ...any) (*sql.Rows, error)
}

func listFolders(ctx context.Context, query folderQuery, userID string) ([]Folder, error) {
	rows, err := query.QueryContext(ctx, `
		SELECT id, owner_user_id, COALESCE(parent_id::text, $2), name, type_default, version
		FROM inventory_folders WHERE owner_user_id = $1
		ORDER BY parent_id NULLS FIRST, type_default, id`, userID, zeroUUID)
	if err != nil {
		return nil, fmt.Errorf("list inventory folders: %w", err)
	}
	defer rows.Close()
	var folders []Folder
	for rows.Next() {
		var folder Folder
		if err := rows.Scan(&folder.ID, &folder.OwnerUserID, &folder.ParentID, &folder.Name,
			&folder.TypeDefault, &folder.Version); err != nil {
			return nil, fmt.Errorf("scan inventory folder: %w", err)
		}
		folders = append(folders, folder)
	}
	if err := rows.Err(); err != nil {
		return nil, fmt.Errorf("iterate inventory folders: %w", err)
	}
	return folders, nil
}
