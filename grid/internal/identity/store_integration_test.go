package identity

import (
	"context"
	"crypto/md5"
	"database/sql"
	"encoding/hex"
	"errors"
	"fmt"
	"os"
	"strings"
	"testing"
	"time"

	_ "github.com/jackc/pgx/v5/stdlib"
)

func TestPostgresIdentityLifecycle(t *testing.T) {
	databaseURL := os.Getenv("HOMEWORLDZ_TEST_DATABASE_URL")
	if databaseURL == "" {
		t.Skip("HOMEWORLDZ_TEST_DATABASE_URL is not configured")
	}
	db, err := sql.Open("pgx", databaseURL)
	if err != nil {
		t.Fatal(err)
	}
	defer db.Close()
	ctx, cancel := context.WithTimeout(context.Background(), 15*time.Second)
	defer cancel()
	store := NewPostgresStore(db)
	username := fmt.Sprintf("integration.%d", time.Now().UnixNano())
	user, err := store.CreateUser(ctx, username, "integration-password")
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() { _, _ = db.Exec("DELETE FROM users WHERE id = $1", user.ID) })
	if _, err := store.CreateUser(ctx, username, "integration-password"); !errors.Is(err, ErrConflict) {
		t.Fatalf("duplicate user error = %v, want ErrConflict", err)
	}
	if _, err := store.CreateSession(ctx, username, "wrong-password", time.Hour); !errors.Is(err, ErrInvalidCredentials) {
		t.Fatalf("invalid login error = %v, want ErrInvalidCredentials", err)
	}
	session, err := store.CreateSession(ctx, username, "integration-password", time.Hour)
	if err != nil {
		t.Fatal(err)
	}
	if validated, err := store.ValidateSession(ctx, session.ID); err != nil || validated.UserID != user.ID || validated.SecureID == "" {
		t.Fatalf("validated session = %#v, error = %v", validated, err)
	}
	viewerDigest := md5.Sum([]byte("integration-password"))
	viewerSession, err := store.CreateViewerSession(ctx, username, hex.EncodeToString(viewerDigest[:]), time.Hour)
	if err != nil || viewerSession.UserID != user.ID || viewerSession.SecureID == "" {
		t.Fatalf("viewer session = %#v, error = %v", viewerSession, err)
	}
	if _, err := store.CreateViewerSession(ctx, username, strings.Repeat("0", 32), time.Hour); !errors.Is(err, ErrInvalidCredentials) {
		t.Fatalf("invalid viewer login error = %v, want ErrInvalidCredentials", err)
	}
	if _, err := db.ExecContext(ctx, "UPDATE sessions SET expires_at = now() - interval '1 second' WHERE id = $1", session.ID); err != nil {
		t.Fatal(err)
	}
	if _, err := store.ValidateSession(ctx, session.ID); !errors.Is(err, ErrSessionNotFound) {
		t.Fatalf("expired session error = %v, want ErrSessionNotFound", err)
	}
	second, err := store.CreateSession(ctx, username, "integration-password", time.Hour)
	if err != nil {
		t.Fatal(err)
	}
	if err := store.RevokeSession(ctx, second.ID); err != nil {
		t.Fatal(err)
	}
	if _, err := store.ValidateSession(ctx, second.ID); !errors.Is(err, ErrSessionNotFound) {
		t.Fatalf("revoked session error = %v, want ErrSessionNotFound", err)
	}
}
