package transit

import (
	"context"
	"database/sql"
	"errors"
	"fmt"
	"time"

	"github.com/jackc/pgx/v5/pgconn"
)

var (
	ErrNotFound          = errors.New("transit not found")
	ErrConflict          = errors.New("transit conflicts with active state")
	ErrInvalidTransition = errors.New("invalid transit state transition")
	ErrExpired           = errors.New("transit expired")
)

type State string

const (
	Prepared   State = "prepared"
	Accepted   State = "accepted"
	Activated  State = "activated"
	RolledBack State = "rolled_back"
)

type Vector3 struct {
	X float32 `json:"x"`
	Y float32 `json:"y"`
	Z float32 `json:"z"`
}

type Transit struct {
	ID                  string    `json:"id"`
	Generation          int64     `json:"generation"`
	AgentID             string    `json:"agentId"`
	SessionID           string    `json:"sessionId"`
	SourceRegionID      string    `json:"sourceRegionId"`
	DestinationRegionID string    `json:"destinationRegionId"`
	Position            Vector3   `json:"position"`
	LookAt              Vector3   `json:"lookAt"`
	Flying              bool      `json:"flying"`
	State               State     `json:"state"`
	RollbackReason      string    `json:"rollbackReason,omitempty"`
	ExpiresAt           time.Time `json:"expiresAt"`
	CreatedAt           time.Time `json:"createdAt"`
	UpdatedAt           time.Time `json:"updatedAt"`
}

type Prepare struct {
	ID                  string
	AgentID             string
	SessionID           string
	SourceRegionID      string
	DestinationRegionID string
	Position            Vector3
	LookAt              Vector3
	Flying              bool
	Lifetime            time.Duration
}

type Store interface {
	Prepare(context.Context, Prepare) (Transit, error)
	Get(context.Context, string) (Transit, error)
	Accept(context.Context, string, string) (Transit, error)
	Activate(context.Context, string, string) (Transit, error)
	Rollback(context.Context, string, string, string) (Transit, error)
}

type PostgresStore struct{ db *sql.DB }

func NewPostgresStore(db *sql.DB) *PostgresStore { return &PostgresStore{db: db} }

const transitColumns = `id, generation, agent_id, session_id, source_region_id,
destination_region_id, position_x, position_y, position_z, look_x, look_y, look_z,
flying, state, rollback_reason, expires_at, created_at, updated_at`

type scanner interface{ Scan(...any) error }

func scanTransit(row scanner) (Transit, error) {
	var value Transit
	err := row.Scan(&value.ID, &value.Generation, &value.AgentID, &value.SessionID,
		&value.SourceRegionID, &value.DestinationRegionID,
		&value.Position.X, &value.Position.Y, &value.Position.Z,
		&value.LookAt.X, &value.LookAt.Y, &value.LookAt.Z,
		&value.Flying, &value.State, &value.RollbackReason, &value.ExpiresAt,
		&value.CreatedAt, &value.UpdatedAt)
	return value, err
}

func (s *PostgresStore) Prepare(ctx context.Context, input Prepare) (Transit, error) {
	if input.Lifetime <= 0 {
		input.Lifetime = 30 * time.Second
	}
	tx, err := s.db.BeginTx(ctx, nil)
	if err != nil {
		return Transit{}, fmt.Errorf("begin transit preparation: %w", err)
	}
	defer tx.Rollback()
	if _, err := tx.ExecContext(ctx, "SELECT pg_advisory_xact_lock(hashtext($1))", input.AgentID); err != nil {
		return Transit{}, fmt.Errorf("lock avatar transit: %w", err)
	}
	existing, err := scanTransit(tx.QueryRowContext(ctx,
		"SELECT "+transitColumns+" FROM avatar_transits WHERE id = $1 FOR UPDATE", input.ID))
	if err == nil {
		if !samePrepare(existing, input) {
			return Transit{}, ErrConflict
		}
		return existing, tx.Commit()
	}
	if !errors.Is(err, sql.ErrNoRows) {
		return Transit{}, fmt.Errorf("find idempotent transit: %w", err)
	}
	if _, err := tx.ExecContext(ctx, `UPDATE avatar_transits
		SET state = 'rolled_back', rollback_reason = 'expired', updated_at = now()
		WHERE agent_id = $1 AND state IN ('prepared', 'accepted') AND expires_at <= now()`, input.AgentID); err != nil {
		return Transit{}, fmt.Errorf("expire avatar transits: %w", err)
	}
	var active bool
	if err := tx.QueryRowContext(ctx, `SELECT EXISTS(SELECT 1 FROM avatar_transits
		WHERE agent_id = $1 AND state IN ('prepared', 'accepted'))`, input.AgentID).Scan(&active); err != nil {
		return Transit{}, fmt.Errorf("find active avatar transit: %w", err)
	}
	if active {
		return Transit{}, ErrConflict
	}
	var generation int64
	if err := tx.QueryRowContext(ctx,
		"SELECT COALESCE(MAX(generation), 0) + 1 FROM avatar_transits WHERE agent_id = $1", input.AgentID).
		Scan(&generation); err != nil {
		return Transit{}, fmt.Errorf("allocate transit generation: %w", err)
	}
	value, err := scanTransit(tx.QueryRowContext(ctx, `INSERT INTO avatar_transits
		(id, generation, agent_id, session_id, source_region_id, destination_region_id,
		 position_x, position_y, position_z, look_x, look_y, look_z, flying, state, expires_at)
		VALUES ($1,$2,$3,$4,$5,$6,$7,$8,$9,$10,$11,$12,$13,'prepared',now()+$14*interval '1 second')
		RETURNING `+transitColumns,
		input.ID, generation, input.AgentID, input.SessionID, input.SourceRegionID,
		input.DestinationRegionID, input.Position.X, input.Position.Y, input.Position.Z,
		input.LookAt.X, input.LookAt.Y, input.LookAt.Z, input.Flying,
		int64(input.Lifetime/time.Second)))
	if err != nil {
		var postgresError *pgconn.PgError
		if errors.As(err, &postgresError) && postgresError.Code == "23505" {
			return Transit{}, ErrConflict
		}
		return Transit{}, fmt.Errorf("insert avatar transit: %w", err)
	}
	if err := tx.Commit(); err != nil {
		return Transit{}, fmt.Errorf("commit avatar transit: %w", err)
	}
	return value, nil
}

func samePrepare(value Transit, input Prepare) bool {
	return value.AgentID == input.AgentID && value.SessionID == input.SessionID &&
		value.SourceRegionID == input.SourceRegionID && value.DestinationRegionID == input.DestinationRegionID &&
		value.Position == input.Position && value.LookAt == input.LookAt && value.Flying == input.Flying
}

func (s *PostgresStore) Get(ctx context.Context, id string) (Transit, error) {
	value, err := scanTransit(s.db.QueryRowContext(ctx,
		"SELECT "+transitColumns+" FROM avatar_transits WHERE id = $1", id))
	if errors.Is(err, sql.ErrNoRows) {
		return Transit{}, ErrNotFound
	}
	if err != nil {
		return Transit{}, fmt.Errorf("get avatar transit: %w", err)
	}
	return value, nil
}

func (s *PostgresStore) Accept(ctx context.Context, id, destinationRegionID string) (Transit, error) {
	return s.transition(ctx, id, destinationRegionID, Prepared, Accepted, false, "")
}

func (s *PostgresStore) Activate(ctx context.Context, id, destinationRegionID string) (Transit, error) {
	tx, err := s.db.BeginTx(ctx, nil)
	if err != nil {
		return Transit{}, fmt.Errorf("begin transit activation: %w", err)
	}
	defer tx.Rollback()
	current, err := scanTransit(tx.QueryRowContext(ctx,
		"SELECT "+transitColumns+" FROM avatar_transits WHERE id = $1 FOR UPDATE", id))
	if errors.Is(err, sql.ErrNoRows) {
		return Transit{}, ErrNotFound
	}
	if err != nil {
		return Transit{}, fmt.Errorf("lock avatar transit: %w", err)
	}
	if current.DestinationRegionID != destinationRegionID {
		return Transit{}, ErrInvalidTransition
	}
	if current.State == Activated {
		return current, tx.Commit()
	}
	if current.State != Accepted {
		return Transit{}, ErrInvalidTransition
	}
	if time.Now().After(current.ExpiresAt) {
		return Transit{}, ErrExpired
	}
	result, err := tx.ExecContext(ctx, `UPDATE sessions SET destination_region_id = $3
		WHERE id = $1 AND user_id = $2 AND expires_at > now()`,
		current.SessionID, current.AgentID, current.DestinationRegionID)
	if err != nil {
		return Transit{}, fmt.Errorf("move viewer session destination: %w", err)
	}
	updated, err := result.RowsAffected()
	if err != nil {
		return Transit{}, fmt.Errorf("count moved viewer sessions: %w", err)
	}
	if updated != 1 {
		return Transit{}, ErrInvalidTransition
	}
	_, err = tx.ExecContext(ctx, `UPDATE users SET
		last_region_id=$2, last_position_x=$3, last_position_y=$4, last_position_z=$5,
		last_look_x=$6, last_look_y=$7, last_look_z=$8, last_flying=$9,
		last_location_updated_at=now() WHERE id=$1`, current.AgentID, current.DestinationRegionID,
		current.Position.X, current.Position.Y, current.Position.Z,
		current.LookAt.X, current.LookAt.Y, current.LookAt.Z, current.Flying)
	if err != nil {
		return Transit{}, fmt.Errorf("record transit last location: %w", err)
	}
	value, err := scanTransit(tx.QueryRowContext(ctx, `UPDATE avatar_transits
		SET state = 'activated', updated_at = now() WHERE id = $1 RETURNING `+transitColumns, id))
	if err != nil {
		return Transit{}, fmt.Errorf("activate avatar transit: %w", err)
	}
	if err := tx.Commit(); err != nil {
		return Transit{}, fmt.Errorf("commit transit activation: %w", err)
	}
	return value, nil
}

func (s *PostgresStore) Rollback(ctx context.Context, id, regionID, reason string) (Transit, error) {
	return s.transition(ctx, id, regionID, "", RolledBack, true, reason)
}

func (s *PostgresStore) transition(ctx context.Context, id, regionID string, from, to State,
	rollback bool, reason string) (Transit, error) {
	where := "destination_region_id = $3 AND state = $4"
	statePlaceholder := "$5"
	args := []any{id, reason, regionID, from, to}
	if rollback {
		where = "(source_region_id = $3 OR destination_region_id = $3) AND state IN ('prepared','accepted')"
		statePlaceholder = "$4"
		args = []any{id, reason, regionID, to}
	}
	value, err := scanTransit(s.db.QueryRowContext(ctx, `UPDATE avatar_transits SET state = `+statePlaceholder+`,
		rollback_reason = $2, updated_at = now() WHERE id = $1 AND `+where+` AND expires_at > now()
		RETURNING `+transitColumns, args...))
	if err == nil {
		return value, nil
	}
	if !errors.Is(err, sql.ErrNoRows) {
		return Transit{}, fmt.Errorf("transition avatar transit: %w", err)
	}
	existing, getErr := s.Get(ctx, id)
	if getErr != nil {
		return Transit{}, getErr
	}
	actorMatches := existing.DestinationRegionID == regionID || (rollback && existing.SourceRegionID == regionID)
	if actorMatches && existing.State == to {
		return existing, nil
	}
	if time.Now().After(existing.ExpiresAt) && (existing.State == Prepared || existing.State == Accepted) {
		return Transit{}, ErrExpired
	}
	return Transit{}, ErrInvalidTransition
}
