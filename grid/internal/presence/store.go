package presence

import (
	"context"
	"database/sql"
	"errors"
	"fmt"
	"time"
)

const StaleAfter = 90 * time.Second

var ErrNotFound = errors.New("user or active region not found")

type Presence struct {
	UserID     string    `json:"userId"`
	RegionID   string    `json:"regionId"`
	LastSeenAt time.Time `json:"lastSeenAt"`
}

type Store interface {
	Update(context.Context, string, string) (Presence, error)
	Clear(context.Context, string) error
	Get(context.Context, string) (Presence, error)
	List(context.Context) ([]Presence, error)
}

type PostgresStore struct{ db *sql.DB }

func NewPostgresStore(db *sql.DB) *PostgresStore { return &PostgresStore{db: db} }

func (s *PostgresStore) Update(ctx context.Context, userID, regionID string) (Presence, error) {
	var value Presence
	err := s.db.QueryRowContext(ctx, `
        INSERT INTO presence (user_id, region_id, last_seen_at)
        SELECT $1, $2, now()
        WHERE EXISTS (SELECT 1 FROM users WHERE id = $1)
          AND EXISTS (SELECT 1 FROM regions WHERE id = $2 AND lease_expires_at > now())
        ON CONFLICT (user_id) DO UPDATE
        SET region_id = EXCLUDED.region_id, last_seen_at = EXCLUDED.last_seen_at
        RETURNING user_id, region_id, last_seen_at`, userID, regionID,
	).Scan(&value.UserID, &value.RegionID, &value.LastSeenAt)
	if errors.Is(err, sql.ErrNoRows) {
		return Presence{}, ErrNotFound
	}
	if err != nil {
		return Presence{}, fmt.Errorf("update presence: %w", err)
	}
	return value, nil
}

func (s *PostgresStore) Clear(ctx context.Context, userID string) error {
	result, err := s.db.ExecContext(ctx, "DELETE FROM presence WHERE user_id = $1", userID)
	if err != nil {
		return fmt.Errorf("clear presence: %w", err)
	}
	count, err := result.RowsAffected()
	if err != nil {
		return fmt.Errorf("count cleared presence: %w", err)
	}
	if count == 0 {
		return ErrNotFound
	}
	return nil
}

func (s *PostgresStore) Get(ctx context.Context, userID string) (Presence, error) {
	var value Presence
	err := s.db.QueryRowContext(ctx, `
        SELECT user_id, region_id, last_seen_at FROM presence
        WHERE user_id = $1 AND last_seen_at > now() - $2 * interval '1 second'`,
		userID, int64(StaleAfter/time.Second),
	).Scan(&value.UserID, &value.RegionID, &value.LastSeenAt)
	if errors.Is(err, sql.ErrNoRows) {
		return Presence{}, ErrNotFound
	}
	if err != nil {
		return Presence{}, fmt.Errorf("get presence: %w", err)
	}
	return value, nil
}

func (s *PostgresStore) List(ctx context.Context) ([]Presence, error) {
	if _, err := s.db.ExecContext(ctx, "DELETE FROM presence WHERE last_seen_at <= now() - $1 * interval '1 second'", int64(StaleAfter/time.Second)); err != nil {
		return nil, fmt.Errorf("delete stale presence: %w", err)
	}
	rows, err := s.db.QueryContext(ctx, "SELECT user_id, region_id, last_seen_at FROM presence ORDER BY user_id")
	if err != nil {
		return nil, fmt.Errorf("list presence: %w", err)
	}
	defer rows.Close()
	values := make([]Presence, 0)
	for rows.Next() {
		var value Presence
		if err := rows.Scan(&value.UserID, &value.RegionID, &value.LastSeenAt); err != nil {
			return nil, fmt.Errorf("scan presence: %w", err)
		}
		values = append(values, value)
	}
	if err := rows.Err(); err != nil {
		return nil, fmt.Errorf("iterate presence: %w", err)
	}
	return values, nil
}
