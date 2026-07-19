// Command dbmigrate applies any db/migrations/*.up.sql whose version is newer
// than the database's current schema_metadata version. It mirrors what CI does
// with psql, for local development where psql may be unavailable. Temporary dev
// helper.
package main

import (
	"database/sql"
	"flag"
	"fmt"
	"os"
	"path/filepath"
	"regexp"
	"sort"
	"strconv"

	"github.com/homeworldz/homeworldz/grid/internal/config"
	_ "github.com/jackc/pgx/v5/stdlib"
)

var versionPrefix = regexp.MustCompile(`^(\d+)_`)

func main() {
	configDirectory := flag.String("config", "config", "directory containing db.ini")
	migrationsDir := flag.String("migrations", "db/migrations", "directory of *.up.sql files")
	flag.Parse()

	settings, err := config.LoadGrid(*configDirectory)
	if err != nil {
		fail("load configuration: %v", err)
	}
	if settings.DatabaseURL == "" {
		fail("no database url configured in %s/db.ini", *configDirectory)
	}

	db, err := sql.Open("pgx", settings.DatabaseURL)
	if err != nil {
		fail("open database: %v", err)
	}
	defer db.Close()

	current := 0
	if err := db.QueryRow(`SELECT COALESCE(MAX(version), 0) FROM schema_metadata`).Scan(&current); err != nil {
		fail("read schema_metadata (is the database initialized?): %v", err)
	}
	fmt.Printf("current schema version: %d\n", current)

	entries, err := filepath.Glob(filepath.Join(*migrationsDir, "*.up.sql"))
	if err != nil {
		fail("list migrations: %v", err)
	}
	sort.Strings(entries)

	applied := 0
	for _, path := range entries {
		match := versionPrefix.FindStringSubmatch(filepath.Base(path))
		if match == nil {
			continue
		}
		version, _ := strconv.Atoi(match[1])
		if version <= current {
			continue
		}
		sqlBytes, readErr := os.ReadFile(path)
		if readErr != nil {
			fail("read %s: %v", path, readErr)
		}
		if _, execErr := db.Exec(string(sqlBytes)); execErr != nil {
			fail("apply %s: %v", filepath.Base(path), execErr)
		}
		fmt.Printf("applied %s\n", filepath.Base(path))
		applied++
	}

	if applied == 0 {
		fmt.Println("already up to date")
	} else {
		fmt.Printf("applied %d migration(s)\n", applied)
	}
}

func fail(format string, args ...any) {
	fmt.Fprintf(os.Stderr, "dbmigrate: "+format+"\n", args...)
	os.Exit(1)
}
