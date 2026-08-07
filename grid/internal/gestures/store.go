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
	// Joined to inventory rather than read alone: active_gestures has no
	// foreign key to inventory_items, so deleting a gesture leaves its
	// activation behind. The login reply would keep naming an item the user no
	// longer has, and the viewer reports "Unable to load gesture <name>" at
	// every login with nothing left to delete to stop it. Filtering here fixes
	// the rows already orphaned as well as the ones deletion will orphan next,
	// which a cascade added now would not.
	rows, err := s.db.QueryContext(ctx,
		`SELECT g.item_id, g.asset_id FROM active_gestures AS g
		   JOIN inventory_items AS i ON i.id = g.item_id AND i.owner_user_id = g.user_id
		  WHERE g.user_id = $1 ORDER BY g.activated_at`, userID)
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
