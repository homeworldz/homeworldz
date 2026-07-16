package regions

import (
	"context"
	"database/sql"
	"errors"
	"fmt"
	"time"

	"github.com/homeworldz/homeworldz/grid/internal/identifier"
	"github.com/jackc/pgx/v5/pgconn"
)

var (
	ErrNotFound = errors.New("region not found")
	ErrConflict = errors.New("region coordinates are already leased")
)

type Region struct {
	ID             string    `json:"id"`
	Name           string    `json:"name"`
	GridX          int       `json:"gridX"`
	GridY          int       `json:"gridY"`
	PublicEndpoint string    `json:"publicEndpoint"`
	ViewerPort     int       `json:"viewerPort"`
	LeaseExpiresAt time.Time `json:"leaseExpiresAt"`
}

type Registration struct {
	Name           string
	GridX          int
	GridY          int
	PublicEndpoint string
	ViewerPort     int
	LeaseDuration  time.Duration
}

type Store interface {
	Register(context.Context, Registration) (Region, error)
	RegisterProvisioned(context.Context, string, Registration) (Region, error)
	Renew(context.Context, string, time.Duration) (Region, error)
	Deregister(context.Context, string) error
	Get(context.Context, string) (Region, error)
	List(context.Context) ([]Region, error)
}

func (s *PostgresStore) RegisterProvisioned(ctx context.Context, id string, input Registration) (Region, error) {
	if input.ViewerPort == 0 {
		input.ViewerPort = 42002
	}
	tx, err := s.db.BeginTx(ctx, nil)
	if err != nil {
		return Region{}, fmt.Errorf("begin provisioned region registration: %w", err)
	}
	defer tx.Rollback()
	var region Region
	err = tx.QueryRowContext(ctx, `
        INSERT INTO regions (id, name, grid_x, grid_y, public_endpoint, viewer_port,
                             lease_expires_at, provisioned)
        VALUES ($1, $2, $3, $4, $5, $6, now() + $7 * interval '1 second', true)
        ON CONFLICT (id) DO UPDATE
        SET name = EXCLUDED.name, grid_x = EXCLUDED.grid_x, grid_y = EXCLUDED.grid_y,
            public_endpoint = EXCLUDED.public_endpoint, viewer_port = EXCLUDED.viewer_port,
            lease_expires_at = EXCLUDED.lease_expires_at, provisioned = true, updated_at = now()
        RETURNING id, name, grid_x, grid_y, public_endpoint, viewer_port, lease_expires_at`,
		id, input.Name, input.GridX, input.GridY, input.PublicEndpoint, input.ViewerPort,
		int64(input.LeaseDuration/time.Second),
	).Scan(&region.ID, &region.Name, &region.GridX, &region.GridY,
		&region.PublicEndpoint, &region.ViewerPort, &region.LeaseExpiresAt)
	if err != nil {
		var databaseError *pgconn.PgError
		if errors.As(err, &databaseError) && databaseError.Code == "23505" {
			return Region{}, ErrConflict
		}
		return Region{}, fmt.Errorf("insert provisioned region: %w", err)
	}
	if err := tx.Commit(); err != nil {
		return Region{}, fmt.Errorf("commit provisioned region registration: %w", err)
	}
	return region, nil
}

type PostgresStore struct {
	db *sql.DB
}

func NewPostgresStore(db *sql.DB) *PostgresStore {
	return &PostgresStore{db: db}
}

func (s *PostgresStore) Register(ctx context.Context, input Registration) (Region, error) {
	if input.ViewerPort == 0 {
		input.ViewerPort = 42002
	}
	id, err := identifier.NewUUID()
	if err != nil {
		return Region{}, err
	}
	tx, err := s.db.BeginTx(ctx, nil)
	if err != nil {
		return Region{}, fmt.Errorf("begin region registration: %w", err)
	}
	defer tx.Rollback()
	if _, err := tx.ExecContext(ctx,
		"DELETE FROM regions WHERE lease_expires_at <= now() AND NOT provisioned"); err != nil {
		return Region{}, fmt.Errorf("expire region leases: %w", err)
	}
	region := Region{ID: id}
	err = tx.QueryRowContext(ctx, `
        INSERT INTO regions (id, name, grid_x, grid_y, public_endpoint, viewer_port, lease_expires_at)
        VALUES ($1, $2, $3, $4, $5, $6, now() + $7 * interval '1 second')
        ON CONFLICT (grid_x, grid_y) DO UPDATE
        SET lease_expires_at = EXCLUDED.lease_expires_at, updated_at = now()
        WHERE regions.name = EXCLUDED.name
          AND regions.public_endpoint = EXCLUDED.public_endpoint
          AND regions.viewer_port = EXCLUDED.viewer_port
        RETURNING id, name, grid_x, grid_y, public_endpoint, viewer_port, lease_expires_at`,
		region.ID, input.Name, input.GridX, input.GridY, input.PublicEndpoint, input.ViewerPort,
		int64(input.LeaseDuration/time.Second),
	).Scan(&region.ID, &region.Name, &region.GridX, &region.GridY,
		&region.PublicEndpoint, &region.ViewerPort, &region.LeaseExpiresAt)
	if errors.Is(err, sql.ErrNoRows) {
		return Region{}, ErrConflict
	}
	if err != nil {
		return Region{}, fmt.Errorf("insert region: %w", err)
	}
	if err := tx.Commit(); err != nil {
		return Region{}, fmt.Errorf("commit region registration: %w", err)
	}
	return region, nil
}

func (s *PostgresStore) Renew(ctx context.Context, id string, duration time.Duration) (Region, error) {
	var region Region
	err := s.db.QueryRowContext(ctx, `
        UPDATE regions
        SET lease_expires_at = now() + $2 * interval '1 second', updated_at = now()
        WHERE id = $1 AND lease_expires_at > now()
        RETURNING id, name, grid_x, grid_y, public_endpoint, viewer_port, lease_expires_at`,
		id, int64(duration/time.Second),
	).Scan(&region.ID, &region.Name, &region.GridX, &region.GridY,
		&region.PublicEndpoint, &region.ViewerPort, &region.LeaseExpiresAt)
	if errors.Is(err, sql.ErrNoRows) {
		return Region{}, ErrNotFound
	}
	if err != nil {
		return Region{}, fmt.Errorf("renew region lease: %w", err)
	}
	return region, nil
}

func (s *PostgresStore) Deregister(ctx context.Context, id string) error {
	result, err := s.db.ExecContext(ctx, "DELETE FROM regions WHERE id = $1", id)
	if err != nil {
		return fmt.Errorf("deregister region: %w", err)
	}
	count, err := result.RowsAffected()
	if err != nil {
		return fmt.Errorf("count deregistered regions: %w", err)
	}
	if count == 0 {
		return ErrNotFound
	}
	return nil
}

func (s *PostgresStore) Get(ctx context.Context, id string) (Region, error) {
	var region Region
	err := s.db.QueryRowContext(ctx, `
        SELECT id, name, grid_x, grid_y, public_endpoint, viewer_port, lease_expires_at
        FROM regions WHERE id = $1 AND lease_expires_at > now()`, id,
	).Scan(&region.ID, &region.Name, &region.GridX, &region.GridY,
		&region.PublicEndpoint, &region.ViewerPort, &region.LeaseExpiresAt)
	if errors.Is(err, sql.ErrNoRows) {
		return Region{}, ErrNotFound
	}
	if err != nil {
		return Region{}, fmt.Errorf("get region: %w", err)
	}
	return region, nil
}

func (s *PostgresStore) List(ctx context.Context) ([]Region, error) {
	rows, err := s.db.QueryContext(ctx, `
        SELECT id, name, grid_x, grid_y, public_endpoint, viewer_port, lease_expires_at
        FROM regions WHERE lease_expires_at > now() ORDER BY grid_y, grid_x`)
	if err != nil {
		return nil, fmt.Errorf("list regions: %w", err)
	}
	defer rows.Close()
	regions := make([]Region, 0)
	for rows.Next() {
		var region Region
		if err := rows.Scan(&region.ID, &region.Name, &region.GridX, &region.GridY,
			&region.PublicEndpoint, &region.ViewerPort, &region.LeaseExpiresAt); err != nil {
			return nil, fmt.Errorf("scan region: %w", err)
		}
		regions = append(regions, region)
	}
	if err := rows.Err(); err != nil {
		return nil, fmt.Errorf("iterate regions: %w", err)
	}
	return regions, nil
}
