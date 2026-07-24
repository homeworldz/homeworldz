package gestures

import (
	"context"
	"database/sql"
	"fmt"
)

// Gesture is one active gesture in a user's set: the inventory item plus the
// gesture asset it points at. The viewer needs both at login to preload the
// gesture and arm its trigger words.
type Gesture struct {
	ItemID  string `json:"itemId"`
	AssetID string `json:"assetId"`
}

type Store interface {
	ListActive(ctx context.Context, userID string) ([]Gesture, error)
	Activate(ctx context.Context, userID, itemID, assetID string) error
	Deactivate(ctx context.Context, userID, itemID string) error
}

type PostgresStore struct{ db *sql.DB }

func NewPostgresStore(db *sql.DB) *PostgresStore { return &PostgresStore{db: db} }

func (s *PostgresStore) ListActive(ctx context.Context, userID string) ([]Gesture, error) {
	rows, err := s.db.QueryContext(ctx,
		`SELECT item_id, asset_id FROM active_gestures WHERE user_id = $1 ORDER BY activated_at`, userID)
	if err != nil {
		return nil, fmt.Errorf("list active gestures: %w", err)
	}
	defer rows.Close()
	var result []Gesture
	for rows.Next() {
		var g Gesture
		if err := rows.Scan(&g.ItemID, &g.AssetID); err != nil {
			return nil, fmt.Errorf("scan active gesture: %w", err)
		}
		result = append(result, g)
	}
	if err := rows.Err(); err != nil {
		return nil, fmt.Errorf("iterate active gestures: %w", err)
	}
	return result, nil
}

func (s *PostgresStore) Activate(ctx context.Context, userID, itemID, assetID string) error {
	_, err := s.db.ExecContext(ctx,
		`INSERT INTO active_gestures (user_id, item_id, asset_id) VALUES ($1, $2, $3)
		 ON CONFLICT (user_id, item_id) DO UPDATE SET asset_id = EXCLUDED.asset_id, activated_at = now()`,
		userID, itemID, assetID)
	if err != nil {
		return fmt.Errorf("activate gesture: %w", err)
	}
	return nil
}

func (s *PostgresStore) Deactivate(ctx context.Context, userID, itemID string) error {
	_, err := s.db.ExecContext(ctx,
		`DELETE FROM active_gestures WHERE user_id = $1 AND item_id = $2`, userID, itemID)
	if err != nil {
		return fmt.Errorf("deactivate gesture: %w", err)
	}
	return nil
}
