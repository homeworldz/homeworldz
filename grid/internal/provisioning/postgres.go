package provisioning

import (
	"context"
	"crypto/sha256"
	"database/sql"
	"errors"
	"fmt"
	"strings"

	"github.com/jackc/pgx/v5/pgconn"
)

type PostgresStore struct{ db *sql.DB }

func NewPostgresStore(db *sql.DB) *PostgresStore { return &PostgresStore{db: db} }

func (s *PostgresStore) Import(ctx context.Context, items []Region) error {
	for _, item := range items {
		if err := validate(item); err != nil {
			return err
		}
		hash := sha256.Sum256([]byte(item.AccessKey))
		_, err := s.db.ExecContext(ctx, `INSERT INTO provisioned_regions
			(id,name,owner_user_id,grid_x,grid_y,size,maturity,public_endpoint,viewer_port,enabled,access_key_hash,kind,tags)
			VALUES ($1,$2,$3,$4,$5,$6,$7,$8,$9,$10,$11,$12,$13) ON CONFLICT (id) DO NOTHING`,
			item.ID, item.Name, nullableOwner(item.OwnerUserID), item.MapX, item.MapY,
			item.Size, item.Maturity, item.PublicEndpoint, item.ViewerPort, item.Enabled, hash[:],
			regionKindOrDefault(item.Kind), item.Tags)
		if err != nil {
			return classify("import provisioned region", err)
		}
	}
	return nil
}

func (s *PostgresStore) Authenticate(ctx context.Context, id, accessKey string) (Region, bool) {
	hash := sha256.Sum256([]byte(accessKey))
	region, err := s.get(ctx, `(id::text = $1 OR lower(name) = lower($1))
		AND enabled AND access_key_hash = $2`, id, hash[:])
	return region, err == nil
}

func (s *PostgresStore) List(ctx context.Context) ([]Region, error) {
	rows, err := s.db.QueryContext(ctx, `SELECT id,name,owner_user_id,grid_x,grid_y,size,maturity,public_endpoint,viewer_port,enabled,kind,tags
        FROM provisioned_regions ORDER BY grid_y,grid_x,id`)
	if err != nil {
		return nil, fmt.Errorf("list provisioned regions: %w", err)
	}
	defer rows.Close()
	items := make([]Region, 0)
	for rows.Next() {
		item, err := scanRegion(rows)
		if err != nil {
			return nil, fmt.Errorf("scan provisioned region: %w", err)
		}
		items = append(items, item)
	}
	if err := rows.Err(); err != nil {
		return nil, fmt.Errorf("iterate provisioned regions: %w", err)
	}
	return items, nil
}

func (s *PostgresStore) Get(ctx context.Context, id string) (Region, error) {
	return s.get(ctx, `id = $1`, id)
}

func (s *PostgresStore) Create(ctx context.Context, item Region) (Region, error) {
	item.Name = normalize(item.Name)
	item.OwnerUserID = normalize(item.OwnerUserID)
	item.PublicEndpoint = normalize(item.PublicEndpoint)
	if item.Size == 0 {
		item.Size = 1
	}
	if err := validate(item); err != nil {
		return Region{}, err
	}
	hash := sha256.Sum256([]byte(item.AccessKey))
	row := s.db.QueryRowContext(ctx, `INSERT INTO provisioned_regions
		(id,name,owner_user_id,grid_x,grid_y,size,maturity,public_endpoint,viewer_port,enabled,access_key_hash,kind,tags)
		VALUES ($1,$2,$3,$4,$5,$6,$7,$8,$9,$10,$11,$12,$13)
		RETURNING id,name,owner_user_id,grid_x,grid_y,size,maturity,public_endpoint,viewer_port,enabled,kind,tags`,
		item.ID, item.Name, nullableOwner(item.OwnerUserID), item.MapX, item.MapY,
		item.Size, item.Maturity, item.PublicEndpoint, item.ViewerPort, item.Enabled, hash[:],
		regionKindOrDefault(item.Kind), item.Tags)
	created, err := scanRegion(row)
	if err != nil {
		return Region{}, classify("create provisioned region", err)
	}
	return created, nil
}

func (s *PostgresStore) Update(ctx context.Context, id string, update Update) (Region, error) {
	current, err := s.Get(ctx, id)
	if err != nil {
		return Region{}, err
	}
	if update.Name != nil {
		current.Name = normalize(*update.Name)
	}
	if update.OwnerUserID != nil {
		current.OwnerUserID = normalize(*update.OwnerUserID)
	}
	if update.MapX != nil {
		current.MapX = *update.MapX
	}
	if update.MapY != nil {
		current.MapY = *update.MapY
	}
	if update.Size != nil {
		current.Size = *update.Size
	}
	if update.Maturity != nil {
		current.Maturity = *update.Maturity
	}
	if update.PublicEndpoint != nil {
		current.PublicEndpoint = normalize(*update.PublicEndpoint)
	}
	if update.ViewerPort != nil {
		current.ViewerPort = *update.ViewerPort
	}
	if update.Enabled != nil {
		current.Enabled = *update.Enabled
	}
	if update.Kind != nil {
		current.Kind = *update.Kind
	}
	if update.Tags != nil {
		current.Tags = *update.Tags
	}
	current.AccessKey = "stored-key"
	if err := validate(current); err != nil {
		return Region{}, err
	}
	row := s.db.QueryRowContext(ctx, `UPDATE provisioned_regions SET
		name=$2,owner_user_id=$3,grid_x=$4,grid_y=$5,size=$6,maturity=$7,
		public_endpoint=$8,viewer_port=$9,enabled=$10,kind=$11,tags=$12,updated_at=now()
		WHERE id=$1 RETURNING id,name,owner_user_id,grid_x,grid_y,size,maturity,public_endpoint,viewer_port,enabled,kind,tags`,
		id, current.Name, nullableOwner(current.OwnerUserID), current.MapX, current.MapY,
		current.Size, current.Maturity, current.PublicEndpoint, current.ViewerPort, current.Enabled,
		regionKindOrDefault(current.Kind), current.Tags)
	item, err := scanRegion(row)
	if err != nil {
		return Region{}, classify("update provisioned region", err)
	}
	return item, nil
}

func (s *PostgresStore) RotateAccessKey(ctx context.Context, id, accessKey string) (Region, error) {
	if normalize(accessKey) == "" {
		return Region{}, fmt.Errorf("%w: access key is empty", ErrInvalid)
	}
	hash := sha256.Sum256([]byte(accessKey))
	row := s.db.QueryRowContext(ctx, `UPDATE provisioned_regions
		SET access_key_hash=$2,updated_at=now() WHERE id=$1
		RETURNING id,name,owner_user_id,grid_x,grid_y,size,maturity,public_endpoint,viewer_port,enabled,kind,tags`, id, hash[:])
	item, err := scanRegion(row)
	if err != nil {
		return Region{}, classify("rotate provisioned region access key", err)
	}
	return item, nil
}

func (s *PostgresStore) Delete(ctx context.Context, id string) error {
	result, err := s.db.ExecContext(ctx, `DELETE FROM provisioned_regions WHERE id=$1`, id)
	if err != nil {
		return fmt.Errorf("delete provisioned region: %w", err)
	}
	count, err := result.RowsAffected()
	if err != nil {
		return fmt.Errorf("count deleted provisioned regions: %w", err)
	}
	if count == 0 {
		return ErrNotFound
	}
	return nil
}

func (s *PostgresStore) get(ctx context.Context, predicate string, arguments ...any) (Region, error) {
	row := s.db.QueryRowContext(ctx, `SELECT id,name,owner_user_id,grid_x,grid_y,size,maturity,public_endpoint,viewer_port,enabled,kind,tags
        FROM provisioned_regions WHERE `+predicate, arguments...)
	item, err := scanRegion(row)
	if errors.Is(err, sql.ErrNoRows) {
		return Region{}, ErrNotFound
	}
	if err != nil {
		return Region{}, fmt.Errorf("get provisioned region: %w", err)
	}
	return item, nil
}

type rowScanner interface{ Scan(...any) error }

func scanRegion(row rowScanner) (Region, error) {
	var item Region
	var owner sql.NullString
	err := row.Scan(&item.ID, &item.Name, &owner, &item.MapX, &item.MapY, &item.Size, &item.Maturity,
		&item.PublicEndpoint, &item.ViewerPort, &item.Enabled, &item.Kind, &item.Tags)
	if owner.Valid {
		item.OwnerUserID = owner.String
	}
	return item, err
}

func classify(operation string, err error) error {
	if errors.Is(err, sql.ErrNoRows) {
		return ErrNotFound
	}
	var databaseError *pgconn.PgError
	if errors.As(err, &databaseError) && (databaseError.Code == "23505" || databaseError.Code == "23P01") {
		return fmt.Errorf("%w: %s", ErrConflict, operation)
	}
	if errors.As(err, &databaseError) && (databaseError.Code == "23503" || databaseError.Code == "23514") {
		return fmt.Errorf("%w: %s", ErrInvalid, operation)
	}
	return fmt.Errorf("%s: %w", operation, err)
}

func nullableOwner(owner string) any {
	if owner == "" {
		return nil
	}
	return owner
}

// regionKindOrDefault falls back to the "user" kind when none is supplied, so a
// row is never written with an empty kind.
func regionKindOrDefault(kind string) string {
	if strings.TrimSpace(kind) == "" {
		return "user"
	}
	return kind
}

func normalize(value string) string { return strings.TrimSpace(value) }
