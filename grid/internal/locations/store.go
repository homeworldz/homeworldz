package locations

import (
	"context"
	"database/sql"
	"errors"
	"fmt"
	"time"
)

var ErrNotFound = errors.New("last location not found")

type Location struct {
	UserID    string     `json:"userId"`
	RegionID  string     `json:"regionId"`
	Position  [3]float32 `json:"position"`
	LookAt    [3]float32 `json:"lookAt"`
	Flying    bool       `json:"flying"`
	UpdatedAt time.Time  `json:"updatedAt"`
}

type Store interface {
	Get(context.Context, string) (Location, error)
	Update(context.Context, Location) (Location, error)
}

func (s *PostgresStore) Update(ctx context.Context, value Location) (Location, error) {
	err := s.db.QueryRowContext(ctx, `UPDATE users SET
		last_region_id=$2, last_position_x=$3, last_position_y=$4, last_position_z=$5,
		last_look_x=$6, last_look_y=$7, last_look_z=$8, last_flying=$9,
		last_location_updated_at=now()
		WHERE id=$1 AND EXISTS (SELECT 1 FROM regions WHERE id=$2)
		RETURNING last_location_updated_at`,
		value.UserID, value.RegionID,
		value.Position[0], value.Position[1], value.Position[2],
		value.LookAt[0], value.LookAt[1], value.LookAt[2], value.Flying).
		Scan(&value.UpdatedAt)
	if errors.Is(err, sql.ErrNoRows) {
		return Location{}, ErrNotFound
	}
	if err != nil {
		return Location{}, fmt.Errorf("update user last location: %w", err)
	}
	return value, nil
}

type PostgresStore struct{ db *sql.DB }

func NewPostgresStore(db *sql.DB) *PostgresStore { return &PostgresStore{db: db} }

func (s *PostgresStore) Get(ctx context.Context, userID string) (Location, error) {
	var value Location
	err := s.db.QueryRowContext(ctx, `SELECT id, last_region_id,
		last_position_x, last_position_y, last_position_z,
		last_look_x, last_look_y, last_look_z, last_flying, last_location_updated_at
		FROM users WHERE id = $1 AND last_region_id IS NOT NULL`, userID).Scan(
		&value.UserID, &value.RegionID,
		&value.Position[0], &value.Position[1], &value.Position[2],
		&value.LookAt[0], &value.LookAt[1], &value.LookAt[2],
		&value.Flying, &value.UpdatedAt)
	if errors.Is(err, sql.ErrNoRows) {
		return Location{}, ErrNotFound
	}
	if err != nil {
		return Location{}, fmt.Errorf("get user last location: %w", err)
	}
	return value, nil
}
