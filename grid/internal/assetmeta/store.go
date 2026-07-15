package assetmeta

import (
	"context"
	"database/sql"
	"errors"
	"fmt"
	"time"
)

var (
	ErrNotFound = errors.New("asset metadata not found")
	ErrConflict = errors.New("asset metadata conflicts with immutable registration")
)

type Location struct {
	Endpoint   string    `json:"endpoint"`
	Origin     bool      `json:"origin"`
	VerifiedAt time.Time `json:"verifiedAt"`
}

type Asset struct {
	ID            string     `json:"id"`
	CreatorUserID string     `json:"creatorUserId"`
	SHA256        string     `json:"sha256"`
	Size          int64      `json:"size"`
	Locations     []Location `json:"locations"`
}

type Registration struct {
	ID            string
	CreatorUserID string
	SHA256        string
	Size          int64
	Endpoint      string
	Origin        bool
}

type Store interface {
	Register(context.Context, Registration) (Asset, error)
	Get(context.Context, string) (Asset, error)
}

type PostgresStore struct{ db *sql.DB }

func NewPostgresStore(db *sql.DB) *PostgresStore { return &PostgresStore{db: db} }

func (s *PostgresStore) Register(ctx context.Context, input Registration) (Asset, error) {
	tx, err := s.db.BeginTx(ctx, nil)
	if err != nil {
		return Asset{}, fmt.Errorf("begin asset registration: %w", err)
	}
	defer tx.Rollback()
	if _, err := tx.ExecContext(ctx, `
		INSERT INTO asset_metadata (asset_id, creator_user_id, sha256, size)
		VALUES ($1, $2, $3, $4) ON CONFLICT (asset_id) DO NOTHING`,
		input.ID, input.CreatorUserID, input.SHA256, input.Size); err != nil {
		return Asset{}, fmt.Errorf("insert asset metadata: %w", err)
	}
	var creator, hash string
	var size int64
	if err := tx.QueryRowContext(ctx,
		"SELECT creator_user_id, sha256, size FROM asset_metadata WHERE asset_id = $1", input.ID).
		Scan(&creator, &hash, &size); err != nil {
		return Asset{}, fmt.Errorf("load asset metadata: %w", err)
	}
	if creator != input.CreatorUserID || hash != input.SHA256 || size != input.Size {
		return Asset{}, ErrConflict
	}
	if _, err := tx.ExecContext(ctx, `
		INSERT INTO asset_locations (asset_id, endpoint, is_origin)
		VALUES ($1, $2, $3)
		ON CONFLICT (asset_id, endpoint) DO UPDATE
		SET is_origin = asset_locations.is_origin OR EXCLUDED.is_origin, verified_at = now()`,
		input.ID, input.Endpoint, input.Origin); err != nil {
		return Asset{}, fmt.Errorf("register asset location: %w", err)
	}
	if err := tx.Commit(); err != nil {
		return Asset{}, fmt.Errorf("commit asset registration: %w", err)
	}
	return s.Get(ctx, input.ID)
}

func (s *PostgresStore) Get(ctx context.Context, id string) (Asset, error) {
	asset := Asset{ID: id, Locations: make([]Location, 0)}
	if err := s.db.QueryRowContext(ctx,
		"SELECT creator_user_id, sha256, size FROM asset_metadata WHERE asset_id = $1", id).
		Scan(&asset.CreatorUserID, &asset.SHA256, &asset.Size); errors.Is(err, sql.ErrNoRows) {
		return Asset{}, ErrNotFound
	} else if err != nil {
		return Asset{}, fmt.Errorf("get asset metadata: %w", err)
	}
	rows, err := s.db.QueryContext(ctx, `
		SELECT endpoint, is_origin, verified_at FROM asset_locations
		WHERE asset_id = $1 ORDER BY is_origin DESC, verified_at DESC, endpoint`, id)
	if err != nil {
		return Asset{}, fmt.Errorf("list asset locations: %w", err)
	}
	defer rows.Close()
	for rows.Next() {
		var location Location
		if err := rows.Scan(&location.Endpoint, &location.Origin, &location.VerifiedAt); err != nil {
			return Asset{}, fmt.Errorf("scan asset location: %w", err)
		}
		asset.Locations = append(asset.Locations, location)
	}
	if err := rows.Err(); err != nil {
		return Asset{}, fmt.Errorf("iterate asset locations: %w", err)
	}
	return asset, nil
}
