package estate

import (
	"context"
	"database/sql"
	"errors"
	"fmt"
	"strings"
)

type PostgresStore struct{ db *sql.DB }

func NewPostgresStore(db *sql.DB) *PostgresStore { return &PostgresStore{db: db} }

func nullableOwner(owner string) any {
	if strings.TrimSpace(owner) == "" {
		return nil
	}
	return owner
}

func scanEstate(row interface{ Scan(...any) error }) (Estate, error) {
	var estate Estate
	var owner sql.NullString
	var abuse sql.NullString
	err := row.Scan(&estate.ID, &estate.Name, &owner, &estate.ParentEstateID, &estate.Flags,
		&estate.PublicAccess, &estate.SunHour, &estate.UseGlobalTime, &estate.FixedSun,
		&estate.BillableFactor, &estate.PricePerMeter, &estate.RedirectGridX, &estate.RedirectGridY,
		&abuse)
	if owner.Valid {
		estate.OwnerUserID = owner.String
	}
	if abuse.Valid {
		estate.AbuseEmail = abuse.String
	}
	estate.Managers = []string{}
	estate.AllowedUsers = []string{}
	estate.AllowedGroups = []string{}
	estate.Bans = []string{}
	return estate, err
}

const estateColumns = `id,name,owner_user_id,parent_estate_id,flags,public_access,sun_hour,` +
	`use_global_time,fixed_sun,billable_factor,price_per_meter,redirect_grid_x,redirect_grid_y,abuse_email`

func (s *PostgresStore) loadMembers(ctx context.Context, q interface {
	QueryContext(context.Context, string, ...any) (*sql.Rows, error)
}, estate *Estate) error {
	rows, err := q.QueryContext(ctx,
		`SELECT member_id::text, role FROM estate_members WHERE estate_id=$1 ORDER BY member_id`, estate.ID)
	if err != nil {
		return fmt.Errorf("load estate members: %w", err)
	}
	defer rows.Close()
	for rows.Next() {
		var member string
		var role int
		if err := rows.Scan(&member, &role); err != nil {
			return fmt.Errorf("scan estate member: %w", err)
		}
		if list := roleList(estate, role); list != nil {
			*list = append(*list, member)
		}
	}
	return rows.Err()
}

func (s *PostgresStore) Get(ctx context.Context, id int) (Estate, error) {
	row := s.db.QueryRowContext(ctx, `SELECT `+estateColumns+` FROM estates WHERE id=$1`, id)
	estate, err := scanEstate(row)
	if errors.Is(err, sql.ErrNoRows) {
		return Estate{}, ErrNotFound
	}
	if err != nil {
		return Estate{}, fmt.Errorf("get estate: %w", err)
	}
	if err := s.loadMembers(ctx, s.db, &estate); err != nil {
		return Estate{}, err
	}
	return estate, nil
}

func (s *PostgresStore) ForRegion(ctx context.Context, regionID, defaultOwner string) (Estate, error) {
	if strings.TrimSpace(regionID) == "" {
		return Estate{}, ErrInvalid
	}
	tx, err := s.db.BeginTx(ctx, nil)
	if err != nil {
		return Estate{}, fmt.Errorf("begin estate transaction: %w", err)
	}
	defer func() { _ = tx.Rollback() }()

	var estateID int
	err = tx.QueryRowContext(ctx, `SELECT estate_id FROM estate_regions WHERE region_id=$1`,
		regionID).Scan(&estateID)
	if errors.Is(err, sql.ErrNoRows) {
		// Reuse a single default estate per owner so co-owned regions share it.
		if strings.TrimSpace(defaultOwner) != "" {
			err = tx.QueryRowContext(ctx,
				`SELECT id FROM estates WHERE owner_user_id=$1 ORDER BY id LIMIT 1`,
				defaultOwner).Scan(&estateID)
		} else {
			err = sql.ErrNoRows
		}
		if errors.Is(err, sql.ErrNoRows) {
			if err = tx.QueryRowContext(ctx,
				`INSERT INTO estates (id,name,owner_user_id,parent_estate_id,public_access,use_global_time)
				 VALUES (COALESCE((SELECT max(id) FROM estates WHERE id >= $1), $1 - 1) + 1, $2, $3, $4, true, true)
				 RETURNING id`,
				DefaultEstateID, DefaultEstateName, nullableOwner(defaultOwner),
				MainlandEstateID).Scan(&estateID); err != nil {
				return Estate{}, fmt.Errorf("create default estate: %w", err)
			}
		} else if err != nil {
			return Estate{}, fmt.Errorf("find owner estate: %w", err)
		}
		if _, err = tx.ExecContext(ctx,
			`INSERT INTO estate_regions (region_id,estate_id) VALUES ($1,$2)
			 ON CONFLICT (region_id) DO UPDATE SET estate_id=excluded.estate_id`,
			regionID, estateID); err != nil {
			return Estate{}, fmt.Errorf("map region to estate: %w", err)
		}
	} else if err != nil {
		return Estate{}, fmt.Errorf("look up region estate: %w", err)
	}

	row := tx.QueryRowContext(ctx, `SELECT `+estateColumns+` FROM estates WHERE id=$1`, estateID)
	estate, err := scanEstate(row)
	if err != nil {
		return Estate{}, fmt.Errorf("load region estate: %w", err)
	}
	if err := s.loadMembers(ctx, tx, &estate); err != nil {
		return Estate{}, err
	}
	if err := tx.Commit(); err != nil {
		return Estate{}, fmt.Errorf("commit estate transaction: %w", err)
	}
	return estate, nil
}

func (s *PostgresStore) UpdateSettings(ctx context.Context, id int, update SettingsUpdate) (Estate, error) {
	current, err := s.Get(ctx, id)
	if err != nil {
		return Estate{}, err
	}
	applySettings(&current, update)
	_, err = s.db.ExecContext(ctx, `UPDATE estates SET
		name=$2, owner_user_id=$3, flags=$4, public_access=$5, sun_hour=$6, use_global_time=$7,
		fixed_sun=$8, billable_factor=$9, price_per_meter=$10, redirect_grid_x=$11,
		redirect_grid_y=$12, abuse_email=$13, updated_at=now() WHERE id=$1`,
		id, current.Name, nullableOwner(current.OwnerUserID), current.Flags, current.PublicAccess,
		current.SunHour, current.UseGlobalTime, current.FixedSun, current.BillableFactor,
		current.PricePerMeter, current.RedirectGridX, current.RedirectGridY, current.AbuseEmail)
	if err != nil {
		return Estate{}, fmt.Errorf("update estate settings: %w", err)
	}
	return s.Get(ctx, id)
}

func (s *PostgresStore) SetMember(ctx context.Context, id int, memberID string, role int, present bool) (Estate, error) {
	if role < RoleManager || role > RoleBannedUser {
		return Estate{}, ErrInvalid
	}
	if _, err := s.Get(ctx, id); err != nil {
		return Estate{}, err
	}
	if present {
		if _, err := s.db.ExecContext(ctx,
			`INSERT INTO estate_members (estate_id,member_id,role) VALUES ($1,$2,$3)
			 ON CONFLICT DO NOTHING`, id, memberID, role); err != nil {
			return Estate{}, fmt.Errorf("add estate member: %w", err)
		}
	} else {
		if _, err := s.db.ExecContext(ctx,
			`DELETE FROM estate_members WHERE estate_id=$1 AND member_id=$2 AND role=$3`,
			id, memberID, role); err != nil {
			return Estate{}, fmt.Errorf("remove estate member: %w", err)
		}
	}
	return s.Get(ctx, id)
}
