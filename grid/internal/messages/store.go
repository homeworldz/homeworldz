// Package messages persists instant messages between users: the first
// store-and-forward notification kind the grid channel carries
// (docs/CLIENT2.md, "What the grid channel carries today"). A message is
// stored before any delivery is attempted, so a recipient with no open
// channel receives it from the backlog on their next connection.
package messages

import (
	"context"
	"database/sql"
	"fmt"
	"strings"
	"time"

	"github.com/homeworldz/server/grid/internal/identifier"
)

type Message struct {
	ID          string     `json:"id"`
	FromUserID  string     `json:"fromUserId"`
	ToUserID    string     `json:"toUserId"`
	Message     string     `json:"message"`
	SentAt      time.Time  `json:"sentAt"`
	DeliveredAt *time.Time `json:"deliveredAt,omitempty"`
}

type Store interface {
	Create(ctx context.Context, fromUserID, toUserID, text string) (Message, error)
	// Undelivered returns the oldest undelivered messages for a user, in
	// sent order, up to limit.
	Undelivered(ctx context.Context, toUserID string, limit int) ([]Message, error)
	// MarkDelivered stamps messages as delivered. Marking happens when a
	// message is handed to a connection, which is best-effort by design: a
	// connection that dies mid-write loses the message, exactly as it would
	// have live.
	MarkDelivered(ctx context.Context, ids []string) error
}

type PostgresStore struct{ db *sql.DB }

func NewPostgresStore(db *sql.DB) *PostgresStore { return &PostgresStore{db: db} }

func (s *PostgresStore) Create(ctx context.Context, fromUserID, toUserID, text string) (Message, error) {
	id, err := identifier.NewUUID()
	if err != nil {
		return Message{}, err
	}
	message := Message{ID: id, FromUserID: fromUserID, ToUserID: toUserID, Message: text}
	err = s.db.QueryRowContext(ctx, `
        INSERT INTO instant_messages (id, from_user_id, to_user_id, message)
        VALUES ($1, $2, $3, $4)
        RETURNING sent_at`,
		id, fromUserID, toUserID, text,
	).Scan(&message.SentAt)
	if err != nil {
		return Message{}, fmt.Errorf("create instant message: %w", err)
	}
	return message, nil
}

func (s *PostgresStore) Undelivered(ctx context.Context, toUserID string, limit int) ([]Message, error) {
	rows, err := s.db.QueryContext(ctx, `
        SELECT id, from_user_id, to_user_id, message, sent_at
        FROM instant_messages
        WHERE to_user_id = $1 AND delivered_at IS NULL
        ORDER BY sent_at
        LIMIT $2`, toUserID, limit)
	if err != nil {
		return nil, fmt.Errorf("list undelivered messages: %w", err)
	}
	defer rows.Close()
	result := make([]Message, 0)
	for rows.Next() {
		var message Message
		if err := rows.Scan(&message.ID, &message.FromUserID, &message.ToUserID,
			&message.Message, &message.SentAt); err != nil {
			return nil, fmt.Errorf("scan instant message: %w", err)
		}
		result = append(result, message)
	}
	if err := rows.Err(); err != nil {
		return nil, fmt.Errorf("iterate instant messages: %w", err)
	}
	return result, nil
}

func (s *PostgresStore) MarkDelivered(ctx context.Context, ids []string) error {
	if len(ids) == 0 {
		return nil
	}
	// Encoded as a Postgres array literal: the ids are store-generated UUIDs,
	// which contain no characters needing quoting.
	if _, err := s.db.ExecContext(ctx, `
        UPDATE instant_messages SET delivered_at = now()
        WHERE id = ANY($1::uuid[]) AND delivered_at IS NULL`,
		"{"+strings.Join(ids, ",")+"}"); err != nil {
		return fmt.Errorf("mark instant messages delivered: %w", err)
	}
	return nil
}
