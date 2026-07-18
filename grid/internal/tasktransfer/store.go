package tasktransfer

import (
	"context"
	"database/sql"
	"encoding/json"
	"errors"
	"fmt"
	"time"

	"github.com/homeworldz/homeworldz/grid/internal/inventory"
)

var (
	ErrNotFound = errors.New("task inventory transfer not found")
	ErrConflict = errors.New("task inventory transfer conflicts with durable state")
	ErrInvalid  = errors.New("task inventory transfer is invalid")
)

type State string

const (
	Prepared   State = "prepared"
	Finalized  State = "finalized"
	RolledBack State = "rolled_back"
)

type Transfer struct {
	ID           string         `json:"id"`
	UserID       string         `json:"userId"`
	SourceItemID string         `json:"sourceItemId"`
	RegionID     string         `json:"regionId"`
	ObjectID     string         `json:"objectId"`
	TaskItemID   string         `json:"taskItemId"`
	Item         inventory.Item `json:"item"`
	State        State          `json:"state"`
	CreatedAt    time.Time      `json:"createdAt"`
	UpdatedAt    time.Time      `json:"updatedAt"`
}

type Prepare struct {
	ID, UserID, SourceItemID, RegionID, ObjectID, TaskItemID string
}

type Extraction struct {
	ID                  string         `json:"id"`
	UserID              string         `json:"userId"`
	RegionID            string         `json:"regionId"`
	ObjectID            string         `json:"objectId"`
	SourceTaskItemID    string         `json:"sourceTaskItemId"`
	DestinationFolderID string         `json:"destinationFolderId"`
	PersonalItemID      string         `json:"personalItemId"`
	Item                inventory.Item `json:"item"`
	State               State          `json:"state"`
	CreatedAt           time.Time      `json:"createdAt"`
	UpdatedAt           time.Time      `json:"updatedAt"`
}

type PrepareExtraction struct {
	ID, UserID, RegionID, ObjectID, SourceTaskItemID, DestinationFolderID,
	PersonalItemID string
	Item inventory.Item
}

type Store interface {
	Prepare(context.Context, Prepare) (Transfer, error)
	Pending(context.Context, string) ([]Transfer, error)
	Finalize(context.Context, string, string) (Transfer, error)
	PrepareExtraction(context.Context, PrepareExtraction) (Extraction, error)
	PendingExtractions(context.Context, string) ([]Extraction, error)
	FinalizeExtraction(context.Context, string, string) (Extraction, error)
}

type PostgresStore struct{ db *sql.DB }

func NewPostgresStore(db *sql.DB) *PostgresStore { return &PostgresStore{db: db} }

const columns = `id, user_id, source_item_id, region_id, object_id, task_item_id,
item, state, created_at, updated_at`

type scanner interface{ Scan(...any) error }

func scan(row scanner) (Transfer, error) {
	var value Transfer
	var encoded []byte
	err := row.Scan(&value.ID, &value.UserID, &value.SourceItemID, &value.RegionID,
		&value.ObjectID, &value.TaskItemID, &encoded, &value.State, &value.CreatedAt, &value.UpdatedAt)
	if err == nil {
		err = json.Unmarshal(encoded, &value.Item)
	}
	return value, err
}

func same(value Transfer, input Prepare) bool {
	return value.ID == input.ID && value.UserID == input.UserID &&
		value.SourceItemID == input.SourceItemID && value.RegionID == input.RegionID &&
		value.ObjectID == input.ObjectID && value.TaskItemID == input.TaskItemID
}

func (s *PostgresStore) Prepare(ctx context.Context, input Prepare) (Transfer, error) {
	if input.ID == "" || input.UserID == "" || input.SourceItemID == "" ||
		input.RegionID == "" || input.ObjectID == "" || input.TaskItemID == "" {
		return Transfer{}, ErrInvalid
	}
	tx, err := s.db.BeginTx(ctx, nil)
	if err != nil {
		return Transfer{}, fmt.Errorf("begin task transfer: %w", err)
	}
	defer tx.Rollback()
	if _, err := tx.ExecContext(ctx, "SELECT pg_advisory_xact_lock(hashtext($1))",
		input.UserID+"/"+input.SourceItemID); err != nil {
		return Transfer{}, fmt.Errorf("lock task transfer: %w", err)
	}
	existing, err := scan(tx.QueryRowContext(ctx, "SELECT "+columns+
		" FROM task_inventory_transfers WHERE user_id=$1 AND source_item_id=$2 FOR UPDATE",
		input.UserID, input.SourceItemID))
	if err == nil {
		if !same(existing, input) {
			return Transfer{}, ErrConflict
		}
		return existing, tx.Commit()
	}
	if !errors.Is(err, sql.ErrNoRows) {
		return Transfer{}, fmt.Errorf("find task transfer: %w", err)
	}
	const zero = "00000000-0000-0000-0000-000000000000"
	var item inventory.Item
	err = tx.QueryRowContext(ctx, `DELETE FROM inventory_items WHERE id=$1 AND owner_user_id=$2
		RETURNING id, owner_user_id, COALESCE(creator_user_id::text,$3), folder_id, asset_id,
		asset_type, inventory_type, name, description, flags, base_permissions,
		current_permissions, everyone_permissions, next_permissions, sale_type, sale_price, created_at`,
		input.SourceItemID, input.UserID, zero).Scan(&item.ID, &item.OwnerUserID, &item.CreatorUserID,
		&item.FolderID, &item.AssetID, &item.AssetType, &item.InventoryType, &item.Name,
		&item.Description, &item.Flags, &item.BasePermissions, &item.CurrentPermissions,
		&item.EveryonePermissions, &item.NextPermissions, &item.SaleType, &item.SalePrice,
		&item.CreatedAt)
	if errors.Is(err, sql.ErrNoRows) {
		return Transfer{}, ErrNotFound
	}
	if err != nil {
		return Transfer{}, fmt.Errorf("withdraw task transfer item: %w", err)
	}
	if item.CurrentPermissions&0x00008000 != 0 {
		return Transfer{}, ErrInvalid
	}
	if _, err := tx.ExecContext(ctx, `UPDATE inventory_folders SET version=version+1,
		updated_at=now() WHERE id=$1 AND owner_user_id=$2`, item.FolderID, input.UserID); err != nil {
		return Transfer{}, fmt.Errorf("update withdrawn item folder: %w", err)
	}
	encoded, err := json.Marshal(item)
	if err != nil {
		return Transfer{}, fmt.Errorf("encode task transfer item: %w", err)
	}
	value, err := scan(tx.QueryRowContext(ctx, `INSERT INTO task_inventory_transfers
		(id,user_id,source_item_id,region_id,object_id,task_item_id,item,state)
		VALUES($1,$2,$3,$4,$5,$6,$7,'prepared') RETURNING `+columns,
		input.ID, input.UserID, input.SourceItemID, input.RegionID, input.ObjectID,
		input.TaskItemID, encoded))
	if err != nil {
		return Transfer{}, fmt.Errorf("insert task transfer: %w", err)
	}
	if err := tx.Commit(); err != nil {
		return Transfer{}, fmt.Errorf("commit task transfer: %w", err)
	}
	return value, nil
}

func (s *PostgresStore) Pending(ctx context.Context, regionID string) ([]Transfer, error) {
	rows, err := s.db.QueryContext(ctx, "SELECT "+columns+
		" FROM task_inventory_transfers WHERE region_id=$1 AND state='prepared' ORDER BY created_at", regionID)
	if err != nil {
		return nil, fmt.Errorf("list pending task transfers: %w", err)
	}
	defer rows.Close()
	var result []Transfer
	for rows.Next() {
		value, err := scan(rows)
		if err != nil {
			return nil, fmt.Errorf("scan pending task transfer: %w", err)
		}
		result = append(result, value)
	}
	return result, rows.Err()
}

func (s *PostgresStore) Finalize(ctx context.Context, id, regionID string) (Transfer, error) {
	return s.transition(ctx, id, regionID, Finalized)
}

func (s *PostgresStore) transition(ctx context.Context, id, regionID string, state State) (Transfer, error) {
	value, err := scan(s.db.QueryRowContext(ctx, `UPDATE task_inventory_transfers SET state=$3,
		updated_at=now() WHERE id=$1 AND region_id=$2 AND state='prepared' RETURNING `+columns,
		id, regionID, state))
	if err == nil {
		return value, nil
	}
	if !errors.Is(err, sql.ErrNoRows) {
		return Transfer{}, fmt.Errorf("transition task transfer: %w", err)
	}
	existing, getErr := scan(s.db.QueryRowContext(ctx, "SELECT "+columns+
		" FROM task_inventory_transfers WHERE id=$1", id))
	if errors.Is(getErr, sql.ErrNoRows) {
		return Transfer{}, ErrNotFound
	}
	if getErr != nil {
		return Transfer{}, getErr
	}
	if existing.RegionID == regionID && existing.State == state {
		return existing, nil
	}
	return Transfer{}, ErrConflict
}

const extractionColumns = `id, user_id, region_id, object_id, source_task_item_id,
destination_folder_id, personal_item_id, item, state, created_at, updated_at`

func scanExtraction(row scanner) (Extraction, error) {
	var value Extraction
	var encoded []byte
	err := row.Scan(&value.ID, &value.UserID, &value.RegionID, &value.ObjectID,
		&value.SourceTaskItemID, &value.DestinationFolderID, &value.PersonalItemID,
		&encoded, &value.State, &value.CreatedAt, &value.UpdatedAt)
	if err == nil {
		err = json.Unmarshal(encoded, &value.Item)
	}
	return value, err
}

func sameExtraction(value Extraction, input PrepareExtraction) bool {
	return value.ID == input.ID && value.UserID == input.UserID &&
		value.RegionID == input.RegionID && value.ObjectID == input.ObjectID &&
		value.SourceTaskItemID == input.SourceTaskItemID &&
		value.DestinationFolderID == input.DestinationFolderID &&
		value.PersonalItemID == input.PersonalItemID
}

func (s *PostgresStore) PrepareExtraction(ctx context.Context, input PrepareExtraction) (Extraction, error) {
	if input.ID == "" || input.UserID == "" || input.RegionID == "" || input.ObjectID == "" ||
		input.SourceTaskItemID == "" || input.DestinationFolderID == "" || input.PersonalItemID == "" ||
		input.Item.AssetID == "" || input.Item.CurrentPermissions&0x00008000 != 0 {
		return Extraction{}, ErrInvalid
	}
	if input.Item.OwnerUserID != input.UserID {
		return Extraction{}, ErrInvalid
	}
	tx, err := s.db.BeginTx(ctx, nil)
	if err != nil {
		return Extraction{}, fmt.Errorf("begin task extraction: %w", err)
	}
	defer tx.Rollback()
	if _, err := tx.ExecContext(ctx, "SELECT pg_advisory_xact_lock(hashtext($1))",
		input.RegionID+"/"+input.ObjectID+"/"+input.SourceTaskItemID); err != nil {
		return Extraction{}, fmt.Errorf("lock task extraction: %w", err)
	}
	existing, err := scanExtraction(tx.QueryRowContext(ctx, "SELECT "+extractionColumns+
		" FROM task_inventory_extractions WHERE region_id=$1 AND object_id=$2 AND source_task_item_id=$3 FOR UPDATE",
		input.RegionID, input.ObjectID, input.SourceTaskItemID))
	if err == nil {
		if !sameExtraction(existing, input) {
			return Extraction{}, ErrConflict
		}
		return existing, tx.Commit()
	}
	if !errors.Is(err, sql.ErrNoRows) {
		return Extraction{}, fmt.Errorf("find task extraction: %w", err)
	}
	var folderOwner string
	if err := tx.QueryRowContext(ctx, `SELECT owner_user_id FROM inventory_folders
		WHERE id=$1 FOR UPDATE`, input.DestinationFolderID).Scan(&folderOwner); err != nil {
		if errors.Is(err, sql.ErrNoRows) {
			return Extraction{}, ErrNotFound
		}
		return Extraction{}, fmt.Errorf("find task extraction folder: %w", err)
	}
	if folderOwner != input.UserID {
		return Extraction{}, ErrInvalid
	}
	input.Item.ID = input.PersonalItemID
	input.Item.FolderID = input.DestinationFolderID
	encoded, err := json.Marshal(input.Item)
	if err != nil {
		return Extraction{}, fmt.Errorf("encode task extraction item: %w", err)
	}
	value, err := scanExtraction(tx.QueryRowContext(ctx, `INSERT INTO task_inventory_extractions
		(id,user_id,region_id,object_id,source_task_item_id,destination_folder_id,personal_item_id,item,state)
		VALUES($1,$2,$3,$4,$5,$6,$7,$8,'prepared') RETURNING `+extractionColumns,
		input.ID, input.UserID, input.RegionID, input.ObjectID, input.SourceTaskItemID,
		input.DestinationFolderID, input.PersonalItemID, encoded))
	if err != nil {
		return Extraction{}, fmt.Errorf("insert task extraction: %w", err)
	}
	if err := tx.Commit(); err != nil {
		return Extraction{}, fmt.Errorf("commit task extraction: %w", err)
	}
	return value, nil
}

func (s *PostgresStore) PendingExtractions(ctx context.Context, regionID string) ([]Extraction, error) {
	rows, err := s.db.QueryContext(ctx, "SELECT "+extractionColumns+
		" FROM task_inventory_extractions WHERE region_id=$1 AND state='prepared' ORDER BY created_at", regionID)
	if err != nil {
		return nil, fmt.Errorf("list pending task extractions: %w", err)
	}
	defer rows.Close()
	var result []Extraction
	for rows.Next() {
		value, err := scanExtraction(rows)
		if err != nil {
			return nil, fmt.Errorf("scan pending task extraction: %w", err)
		}
		result = append(result, value)
	}
	return result, rows.Err()
}

func (s *PostgresStore) FinalizeExtraction(ctx context.Context, id, regionID string) (Extraction, error) {
	tx, err := s.db.BeginTx(ctx, nil)
	if err != nil {
		return Extraction{}, fmt.Errorf("begin final task extraction: %w", err)
	}
	defer tx.Rollback()
	value, err := scanExtraction(tx.QueryRowContext(ctx, "SELECT "+extractionColumns+
		" FROM task_inventory_extractions WHERE id=$1 FOR UPDATE", id))
	if errors.Is(err, sql.ErrNoRows) {
		return Extraction{}, ErrNotFound
	}
	if err != nil {
		return Extraction{}, fmt.Errorf("find final task extraction: %w", err)
	}
	if value.RegionID != regionID {
		return Extraction{}, ErrConflict
	}
	if value.State == Finalized {
		return value, tx.Commit()
	}
	item := value.Item
	_, err = tx.ExecContext(ctx, `INSERT INTO inventory_items
		(id,owner_user_id,creator_user_id,folder_id,asset_id,asset_type,inventory_type,name,
		description,flags,base_permissions,current_permissions,everyone_permissions,next_permissions,
		sale_type,sale_price,created_at,updated_at)
		VALUES($1,$2,NULLIF($3,'')::uuid,$4,$5,$6,$7,$8,$9,$10,$11,$12,$13,$14,$15,$16,now(),now())`,
		value.PersonalItemID, value.UserID, item.CreatorUserID, value.DestinationFolderID,
		item.AssetID, item.AssetType, item.InventoryType, item.Name, item.Description, item.Flags,
		item.BasePermissions, item.CurrentPermissions, item.EveryonePermissions,
		item.NextPermissions, item.SaleType, item.SalePrice)
	if err != nil {
		return Extraction{}, fmt.Errorf("create extracted inventory item: %w", err)
	}
	if _, err := tx.ExecContext(ctx, `UPDATE inventory_folders SET version=version+1,
		updated_at=now() WHERE id=$1 AND owner_user_id=$2`, value.DestinationFolderID, value.UserID); err != nil {
		return Extraction{}, fmt.Errorf("update extraction folder: %w", err)
	}
	value, err = scanExtraction(tx.QueryRowContext(ctx, `UPDATE task_inventory_extractions
		SET state='finalized',updated_at=now() WHERE id=$1 RETURNING `+extractionColumns, id))
	if err != nil {
		return Extraction{}, fmt.Errorf("finalize task extraction: %w", err)
	}
	if err := tx.Commit(); err != nil {
		return Extraction{}, fmt.Errorf("commit final task extraction: %w", err)
	}
	return value, nil
}
