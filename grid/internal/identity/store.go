package identity

import (
	"context"
	"database/sql"
	"errors"
	"fmt"
	"time"

	"github.com/homeworldz/homeworldz/grid/internal/identifier"
	"github.com/jackc/pgx/v5/pgconn"
	"golang.org/x/crypto/bcrypt"
)

var (
	ErrConflict           = errors.New("username is already registered")
	ErrInvalidCredentials = errors.New("invalid credentials")
	ErrSessionNotFound    = errors.New("session not found")
)

type User struct {
	ID        string    `json:"id"`
	Username  string    `json:"username"`
	CreatedAt time.Time `json:"createdAt"`
}

type Session struct {
	ID        string    `json:"id"`
	UserID    string    `json:"userId"`
	ExpiresAt time.Time `json:"expiresAt"`
}

type Store interface {
	CreateUser(context.Context, string, string) (User, error)
	CreateSession(context.Context, string, string, time.Duration) (Session, error)
	ValidateSession(context.Context, string) (Session, error)
	RevokeSession(context.Context, string) error
}

type PostgresStore struct {
	db *sql.DB
}

func NewPostgresStore(db *sql.DB) *PostgresStore { return &PostgresStore{db: db} }

func (s *PostgresStore) CreateUser(ctx context.Context, username, password string) (User, error) {
	hash, err := bcrypt.GenerateFromPassword([]byte(password), bcrypt.DefaultCost)
	if err != nil {
		return User{}, fmt.Errorf("hash password: %w", err)
	}
	id, err := identifier.NewUUID()
	if err != nil {
		return User{}, err
	}
	var user User
	err = s.db.QueryRowContext(ctx, `
        INSERT INTO users (id, username, password_hash) VALUES ($1, $2, $3)
        RETURNING id, username, created_at`, id, username, string(hash),
	).Scan(&user.ID, &user.Username, &user.CreatedAt)
	var postgresError *pgconn.PgError
	if errors.As(err, &postgresError) && postgresError.Code == "23505" {
		return User{}, ErrConflict
	}
	if err != nil {
		return User{}, fmt.Errorf("create user: %w", err)
	}
	return user, nil
}

func (s *PostgresStore) CreateSession(ctx context.Context, username, password string, duration time.Duration) (Session, error) {
	var userID, passwordHash string
	err := s.db.QueryRowContext(ctx, "SELECT id, password_hash FROM users WHERE username = $1", username).
		Scan(&userID, &passwordHash)
	if errors.Is(err, sql.ErrNoRows) {
		return Session{}, ErrInvalidCredentials
	}
	if err != nil {
		return Session{}, fmt.Errorf("find user credentials: %w", err)
	}
	if bcrypt.CompareHashAndPassword([]byte(passwordHash), []byte(password)) != nil {
		return Session{}, ErrInvalidCredentials
	}
	id, err := identifier.NewUUID()
	if err != nil {
		return Session{}, err
	}
	var session Session
	err = s.db.QueryRowContext(ctx, `
        INSERT INTO sessions (id, user_id, expires_at)
        VALUES ($1, $2, now() + $3 * interval '1 second')
        RETURNING id, user_id, expires_at`, id, userID, int64(duration/time.Second),
	).Scan(&session.ID, &session.UserID, &session.ExpiresAt)
	if err != nil {
		return Session{}, fmt.Errorf("create session: %w", err)
	}
	return session, nil
}

func (s *PostgresStore) ValidateSession(ctx context.Context, id string) (Session, error) {
	var session Session
	err := s.db.QueryRowContext(ctx, `
        SELECT id, user_id, expires_at FROM sessions
        WHERE id = $1 AND expires_at > now()`, id,
	).Scan(&session.ID, &session.UserID, &session.ExpiresAt)
	if errors.Is(err, sql.ErrNoRows) {
		return Session{}, ErrSessionNotFound
	}
	if err != nil {
		return Session{}, fmt.Errorf("validate session: %w", err)
	}
	return session, nil
}

func (s *PostgresStore) RevokeSession(ctx context.Context, id string) error {
	result, err := s.db.ExecContext(ctx, "DELETE FROM sessions WHERE id = $1", id)
	if err != nil {
		return fmt.Errorf("revoke session: %w", err)
	}
	count, err := result.RowsAffected()
	if err != nil {
		return fmt.Errorf("count revoked sessions: %w", err)
	}
	if count == 0 {
		return ErrSessionNotFound
	}
	return nil
}
