package inventory

import (
	"context"
	"crypto/sha256"
	"database/sql"
	"encoding/hex"
	"fmt"
	"strconv"
)

const zeroUUID = "00000000-0000-0000-0000-000000000000"

type Folder struct {
	ID          string `json:"id"`
	OwnerUserID string `json:"ownerUserId"`
	ParentID    string `json:"parentId"`
	Name        string `json:"name"`
	TypeDefault int    `json:"typeDefault"`
	Version     int64  `json:"version"`
}

type Store interface {
	EnsureSystemFolders(context.Context, string) ([]Folder, error)
	ListFolders(context.Context, string) ([]Folder, error)
}

type PostgresStore struct{ db *sql.DB }

func NewPostgresStore(db *sql.DB) *PostgresStore { return &PostgresStore{db: db} }

func SystemFolderID(userID string, folderType int) string {
	value := sha256.Sum256([]byte(userID + "\x00homeworldz-inventory-folder\x00" + strconv.Itoa(folderType)))
	value[6] = (value[6] & 0x0f) | 0x80
	value[8] = (value[8] & 0x3f) | 0x80
	encoded := hex.EncodeToString(value[:16])
	return encoded[0:8] + "-" + encoded[8:12] + "-" + encoded[12:16] + "-" +
		encoded[16:20] + "-" + encoded[20:32]
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
