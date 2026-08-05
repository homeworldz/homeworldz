package schema

import (
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"regexp"
	"strconv"
	"strings"
	"testing"

	"github.com/jackc/pgx/v5/pgconn"
)

var versionPrefix = regexp.MustCompile(`^(\d+)_`)

// The constant is what a deployed binary asserts, and the directory is what a
// migration lands in. If they can drift apart, the check is worse than useless:
// it would pass while the database is missing the very migration the build
// needs. This is the test that makes the constant trustworthy.
func TestRequiredMatchesMigrations(t *testing.T) {
	root := filepath.Join("..", "..", "..", "db", "migrations")
	entries, err := filepath.Glob(filepath.Join(root, "*.up.sql"))
	if err != nil {
		t.Fatalf("list migrations: %v", err)
	}
	if len(entries) == 0 {
		t.Fatalf("no migrations found under %s — this test is measuring nothing", root)
	}

	highest, from := 0, ""
	for _, path := range entries {
		name := filepath.Base(path)
		match := versionPrefix.FindStringSubmatch(name)
		if match == nil {
			t.Errorf("migration %s does not start with a version number", name)
			continue
		}
		version, convErr := strconv.Atoi(match[1])
		if convErr != nil {
			t.Errorf("migration %s has an unparseable version: %v", name, convErr)
			continue
		}
		if version > highest {
			highest, from = version, name
		}
	}

	if highest != Required {
		t.Errorf("Required = %d but the newest migration is %d (%s).\n"+
			"Bump Required in schema.go in the same commit that adds a migration, "+
			"or the startup check will pass against a database that is missing it.",
			Required, highest, from)
	}
}

// Every *.up.sql needs its *.down.sql, since a version we can reach but not
// leave is a one-way deployment.
func TestEveryMigrationHasADownFile(t *testing.T) {
	root := filepath.Join("..", "..", "..", "db", "migrations")
	entries, err := filepath.Glob(filepath.Join(root, "*.up.sql"))
	if err != nil {
		t.Fatalf("list migrations: %v", err)
	}
	for _, path := range entries {
		down := path[:len(path)-len(".up.sql")] + ".down.sql"
		if _, statErr := os.Stat(down); statErr != nil {
			t.Errorf("%s has no matching .down.sql", filepath.Base(path))
		}
	}
}

// Nothing applies these files but psql and the dbmigrate helper, and neither
// writes the version row — each migration stamps itself. So a migration that
// forgets leaves a database that is functionally current and reports otherwise,
// which is exactly what happened to 30 and 31: both were applied, the recorded
// version stayed at 29 for five days, and the gap was read as an unapplied
// migration rather than a missing stamp. Without this test the startup check
// above would inherit that mistake and refuse to start a correct database.
func TestEveryMigrationStampsItsOwnVersion(t *testing.T) {
	root := filepath.Join("..", "..", "..", "db", "migrations")
	entries, err := filepath.Glob(filepath.Join(root, "*.sql"))
	if err != nil {
		t.Fatalf("list migrations: %v", err)
	}
	if len(entries) == 0 {
		t.Fatalf("no migrations found under %s — this test is measuring nothing", root)
	}

	for _, path := range entries {
		name := filepath.Base(path)
		match := versionPrefix.FindStringSubmatch(name)
		if match == nil {
			continue
		}
		body, readErr := os.ReadFile(path)
		if readErr != nil {
			t.Errorf("read %s: %v", name, readErr)
			continue
		}
		// The stamp carries the number, not the zero-padded filename prefix.
		version, convErr := strconv.Atoi(match[1])
		if convErr != nil {
			t.Errorf("migration %s has an unparseable version: %v", name, convErr)
			continue
		}
		// The up file records its version; the down file removes it, or a
		// rolled-back database would claim a version it no longer has.
		want := fmt.Sprintf("INSERT INTO schema_metadata (version) VALUES (%d);", version)
		if strings.HasSuffix(name, ".down.sql") {
			// Version 1 is the exception, and only this one: its down file drops
			// schema_metadata itself, so there is no row left to delete.
			if version == 1 {
				continue
			}
			want = fmt.Sprintf("DELETE FROM schema_metadata WHERE version = %d;", version)
		}
		if !strings.Contains(string(body), want) {
			t.Errorf("%s does not carry its version stamp — expected a line:\n  %s", name, want)
		}
	}
}

func TestCheckRefusesAnOlderDatabase(t *testing.T) {
	err := check(Required-1, nil)
	if err == nil {
		t.Fatalf("a database one version behind was accepted")
	}
	// The operator reads this line in a service that just refused to start, so it
	// has to carry both numbers and the way out.
	for _, want := range []string{
		strconv.Itoa(Required - 1), strconv.Itoa(Required), "dbmigrate",
	} {
		if !contains(err.Error(), want) {
			t.Errorf("error message omits %q: %s", want, err)
		}
	}
}

func TestCheckAcceptsMatchingAndNewerDatabases(t *testing.T) {
	if err := check(Required, nil); err != nil {
		t.Errorf("matching version rejected: %v", err)
	}
	// Ahead means a rolled-back binary against additive schema. Refusing here
	// would make a rollback impossible, which is worse than the risk it guards.
	if err := check(Required+1, nil); err != nil {
		t.Errorf("newer database rejected: %v", err)
	}
}

// The one branch that decides whether a fresh database gets a useful message or
// a raw driver error. Exercised against a real *pgconn.PgError, which also
// proves the behavioural match in classify holds for the driver in use — the
// interface assertion would silently never fire if pgx stopped exposing
// SQLState.
func TestClassifyNamesAnUnmigratedDatabase(t *testing.T) {
	err := classify(&pgconn.PgError{Code: undefinedTable, Message: `relation "schema_metadata" does not exist`})
	if !errors.Is(err, ErrNotInitialized) {
		t.Errorf("an undefined schema_metadata was not reported as uninitialized: %v", err)
	}
}

func TestClassifyPassesOtherFailuresThrough(t *testing.T) {
	underlying := errors.New("connection refused")
	err := classify(underlying)
	if errors.Is(err, ErrNotInitialized) {
		t.Errorf("a connection failure was misreported as an uninitialized database")
	}
	// Wrapped, not replaced: whoever reads the log needs the driver's own words.
	if !errors.Is(err, underlying) {
		t.Errorf("the underlying error was discarded: %v", err)
	}
}

func contains(haystack, needle string) bool {
	for i := 0; i+len(needle) <= len(haystack); i++ {
		if haystack[i:i+len(needle)] == needle {
			return true
		}
	}
	return false
}
