package locations

import (
	"context"
	"database/sql"
	"os"
	"testing"

	_ "github.com/jackc/pgx/v5/stdlib"
)

func TestPostgresGet(t *testing.T) {
	dsn := os.Getenv("HOMEWORLDZ_TEST_DATABASE_URL")
	if dsn == "" {
		t.Skip("HOMEWORLDZ_TEST_DATABASE_URL is not set")
	}
	db, err := sql.Open("pgx", dsn)
	if err != nil {
		t.Fatal(err)
	}
	defer db.Close()
	const user = "99999999-9999-4999-8999-999999999991"
	const region = "99999999-9999-4999-8999-999999999992"
	if _, err := db.Exec(`INSERT INTO users(id,username,password_hash) VALUES($1,'location.test','x') ON CONFLICT DO NOTHING`, user); err != nil {
		t.Fatal(err)
	}
	if _, err := db.Exec(`INSERT INTO regions(id,name,grid_x,grid_y,public_endpoint,viewer_port,lease_expires_at) VALUES($1,'Location Test',60000,60000,'http://127.0.0.1:1',1,now()+interval '1 minute') ON CONFLICT DO NOTHING`, region); err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() {
		_, _ = db.Exec("DELETE FROM users WHERE id=$1", user)
		_, _ = db.Exec("DELETE FROM regions WHERE id=$1", region)
	})
	store := NewPostgresStore(db)
	updated, err := store.Update(context.Background(), Location{
		UserID: user, RegionID: region, Position: [3]float32{128, 64, 30},
		LookAt: [3]float32{1, 0, 0}, Flying: true,
	})
	if err != nil || updated.UpdatedAt.IsZero() {
		t.Fatalf("updated location=%#v err=%v", updated, err)
	}
	value, err := store.Get(context.Background(), user)
	if err != nil || value.RegionID != region || value.Position != [3]float32{128, 64, 30} || !value.Flying {
		t.Fatalf("location=%#v err=%v", value, err)
	}
}
