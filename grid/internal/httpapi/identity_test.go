package httpapi

import (
	"context"
	"crypto/md5"
	"encoding/hex"
	"fmt"
	"net/http"
	"testing"
	"time"

	"github.com/homeworldz/homeworldz/grid/internal/identity"
)

type memoryIdentityStore struct {
	now      time.Time
	nextID   int
	users    map[string]memoryUser
	sessions map[string]identity.Session
}

type memoryUser struct {
	user     identity.User
	password string
}

func newMemoryIdentityStore() *memoryIdentityStore {
	return &memoryIdentityStore{
		now:   time.Date(2026, 7, 13, 12, 0, 0, 0, time.UTC),
		users: make(map[string]memoryUser), sessions: make(map[string]identity.Session),
	}
}

func (s *memoryIdentityStore) id() string {
	s.nextID++
	return fmt.Sprintf("10000000-0000-4000-8000-%012d", s.nextID)
}

func (s *memoryIdentityStore) CreateUser(_ context.Context, username, password string) (identity.User, error) {
	if _, ok := s.users[username]; ok {
		return identity.User{}, identity.ErrConflict
	}
	user := identity.User{ID: s.id(), Username: username, CreatedAt: s.now}
	s.users[username] = memoryUser{user: user, password: password}
	return user, nil
}

func (s *memoryIdentityStore) CreateSession(_ context.Context, username, password string, duration time.Duration) (identity.Session, error) {
	stored, ok := s.users[username]
	if !ok || stored.password != password {
		return identity.Session{}, identity.ErrInvalidCredentials
	}
	session := identity.Session{ID: s.id(), UserID: stored.user.ID, ExpiresAt: s.now.Add(duration), SecureID: s.id()}
	s.sessions[session.ID] = session
	return session, nil
}

func (s *memoryIdentityStore) CreateViewerSession(ctx context.Context, username, passwordHash string, duration time.Duration) (identity.Session, error) {
	stored, ok := s.users[username]
	if !ok {
		return identity.Session{}, identity.ErrInvalidCredentials
	}
	digest := md5.Sum([]byte(stored.password))
	if hex.EncodeToString(digest[:]) != passwordHash {
		return identity.Session{}, identity.ErrInvalidCredentials
	}
	return s.CreateSession(ctx, username, stored.password, duration)
}

func (s *memoryIdentityStore) ValidateSession(_ context.Context, id string) (identity.Session, error) {
	session, ok := s.sessions[id]
	if !ok || !session.ExpiresAt.After(s.now) {
		return identity.Session{}, identity.ErrSessionNotFound
	}
	return session, nil
}

func (s *memoryIdentityStore) RevokeSession(_ context.Context, id string) error {
	if _, ok := s.sessions[id]; !ok {
		return identity.ErrSessionNotFound
	}
	delete(s.sessions, id)
	return nil
}

func TestDevelopmentUserAndSessionLifecycle(t *testing.T) {
	store := newMemoryIdentityStore()
	handler := New(checker{}, "test", Options{ServiceToken: "secret", Identity: store})

	user := requestRegion[identity.User](t, handler, http.MethodPost, "/api/v1/users",
		`{"username":"Test.User","password":"development-password"}`, http.StatusCreated)
	if user.Username != "test.user" {
		t.Fatalf("normalized username = %q", user.Username)
	}
	conflict := requestRegion[Error](t, handler, http.MethodPost, "/api/v1/users",
		`{"username":"test.user","password":"development-password"}`, http.StatusConflict)
	if conflict.Code != "username_in_use" {
		t.Fatalf("conflict code = %q", conflict.Code)
	}
	invalid := requestRegion[Error](t, handler, http.MethodPost, "/api/v1/sessions",
		`{"username":"test.user","password":"wrong-password"}`, http.StatusUnauthorized)
	if invalid.Code != "invalid_credentials" {
		t.Fatalf("login error = %q", invalid.Code)
	}
	session := requestRegion[identity.Session](t, handler, http.MethodPost, "/api/v1/sessions",
		`{"username":"TEST.USER","password":"development-password","sessionSeconds":600}`, http.StatusCreated)
	if session.UserID != user.ID || !session.ExpiresAt.Equal(store.now.Add(10*time.Minute)) {
		t.Fatalf("unexpected session: %#v", session)
	}
	validated := requestRegion[identity.Session](t, handler, http.MethodGet,
		"/api/v1/sessions/"+session.ID, "", http.StatusOK)
	if validated.ID != session.ID {
		t.Fatalf("validated session = %#v", validated)
	}
	store.now = store.now.Add(11 * time.Minute)
	expired := requestRegion[Error](t, handler, http.MethodGet,
		"/api/v1/sessions/"+session.ID, "", http.StatusNotFound)
	if expired.Code != "session_not_found" {
		t.Fatalf("expired session code = %q", expired.Code)
	}

	store.now = store.now.Add(-11 * time.Minute)
	second := requestRegion[identity.Session](t, handler, http.MethodPost, "/api/v1/sessions",
		`{"username":"test.user","password":"development-password"}`, http.StatusCreated)
	requestRegion[any](t, handler, http.MethodDelete, "/api/v1/sessions/"+second.ID, "", http.StatusNoContent)
	requestRegion[Error](t, handler, http.MethodGet, "/api/v1/sessions/"+second.ID, "", http.StatusNotFound)
}

func TestDevelopmentUserValidation(t *testing.T) {
	handler := New(checker{}, "test", Options{ServiceToken: "secret", Identity: newMemoryIdentityStore()})
	response := requestRegion[Error](t, handler, http.MethodPost, "/api/v1/users",
		`{"username":"bad name","password":"short"}`, http.StatusBadRequest)
	if response.Code != "invalid_user" {
		t.Fatalf("validation code = %q", response.Code)
	}
	response = requestRegion[Error](t, handler, http.MethodPost, "/api/v1/sessions",
		`{"username":"test","password":"password","sessionSeconds":10}`, http.StatusBadRequest)
	if response.Code != "invalid_session_duration" {
		t.Fatalf("duration code = %q", response.Code)
	}
}
