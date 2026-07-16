package transit

import (
	"context"
	"database/sql"
	"errors"
	"os"
	"testing"
	"time"

	"github.com/homeworldz/homeworldz/grid/internal/identifier"
	_ "github.com/jackc/pgx/v5/stdlib"
)

func TestPostgresTransitLifecycle(t *testing.T) {
	databaseURL := os.Getenv("HOMEWORLDZ_TEST_DATABASE_URL")
	if databaseURL == "" {
		t.Skip("HOMEWORLDZ_TEST_DATABASE_URL is not configured")
	}
	db, err := sql.Open("pgx", databaseURL)
	if err != nil {
		t.Fatal(err)
	}
	defer db.Close()
	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()

	ids := make([]string, 5)
	for index := range ids {
		ids[index], err = identifier.NewUUID()
		if err != nil {
			t.Fatal(err)
		}
	}
	coordinate := int(time.Now().UnixNano() % 1_000_000_000)
	if _, err := db.ExecContext(ctx, `INSERT INTO users (id, username, password_hash)
		VALUES ($1, $2, 'integration')`, ids[1], "transit-"+ids[1]); err != nil {
		t.Fatal(err)
	}
	if _, err := db.ExecContext(ctx, `INSERT INTO regions
		(id, name, grid_x, grid_y, public_endpoint, viewer_port, lease_expires_at)
		VALUES ($1,'Transit Source',$3,$3,'http://127.0.0.1:1',1,now()+interval '1 minute'),
		       ($2,'Transit Destination',$3+1,$3,'http://127.0.0.1:2',2,now()+interval '1 minute')`,
		ids[3], ids[4], coordinate); err != nil {
		t.Fatal(err)
	}
	if _, err := db.ExecContext(ctx, `INSERT INTO sessions
		(id, user_id, expires_at, destination_region_id) VALUES ($1,$2,now()+interval '1 minute',$3)`,
		ids[2], ids[1], ids[3]); err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() {
		_, _ = db.Exec("DELETE FROM avatar_transits WHERE agent_id = $1", ids[1])
		_, _ = db.Exec("DELETE FROM sessions WHERE id = $1", ids[2])
		_, _ = db.Exec("DELETE FROM regions WHERE id IN ($1, $2)", ids[3], ids[4])
		_, _ = db.Exec("DELETE FROM users WHERE id = $1", ids[1])
	})
	store := NewPostgresStore(db)
	input := Prepare{
		ID: ids[0], AgentID: ids[1], SessionID: ids[2], SourceRegionID: ids[3],
		DestinationRegionID: ids[4], Position: Vector3{X: 128, Y: 64, Z: 30},
		LookAt: Vector3{X: 1}, Flying: true, Lifetime: time.Minute,
	}
	prepared, err := store.Prepare(ctx, input)
	if err != nil || prepared.State != Prepared || prepared.Generation != 1 {
		t.Fatalf("prepared = %#v, error = %v", prepared, err)
	}
	retry, err := store.Prepare(ctx, input)
	if err != nil || retry.ID != prepared.ID || retry.Generation != prepared.Generation {
		t.Fatalf("idempotent retry = %#v, error = %v", retry, err)
	}
	conflict := input
	conflict.ID, _ = identifier.NewUUID()
	if _, err := store.Prepare(ctx, conflict); !errors.Is(err, ErrConflict) {
		t.Fatalf("parallel transit error = %v, want conflict", err)
	}
	accepted, err := store.Accept(ctx, prepared.ID, input.DestinationRegionID)
	if err != nil || accepted.State != Accepted {
		t.Fatalf("accepted = %#v, error = %v", accepted, err)
	}
	if _, err := store.Accept(ctx, prepared.ID, input.DestinationRegionID); err != nil {
		t.Fatalf("idempotent accept: %v", err)
	}
	if _, err := store.Activate(ctx, prepared.ID, input.SourceRegionID); !errors.Is(err, ErrInvalidTransition) {
		t.Fatalf("wrong-region activation error = %v", err)
	}
	activated, err := store.Activate(ctx, prepared.ID, input.DestinationRegionID)
	if err != nil || activated.State != Activated {
		t.Fatalf("activated = %#v, error = %v", activated, err)
	}
	var destination string
	if err := db.QueryRowContext(ctx, "SELECT destination_region_id FROM sessions WHERE id = $1", ids[2]).Scan(&destination); err != nil || destination != ids[4] {
		t.Fatalf("session destination = %q, error = %v", destination, err)
	}
	second := input
	second.ID, _ = identifier.NewUUID()
	next, err := store.Prepare(ctx, second)
	if err != nil || next.Generation != 2 {
		t.Fatalf("next generation = %#v, error = %v", next, err)
	}
	rolledBack, err := store.Rollback(ctx, next.ID, input.SourceRegionID, "test cleanup")
	if err != nil || rolledBack.State != RolledBack || rolledBack.RollbackReason != "test cleanup" {
		t.Fatalf("rolled back = %#v, error = %v", rolledBack, err)
	}
}
