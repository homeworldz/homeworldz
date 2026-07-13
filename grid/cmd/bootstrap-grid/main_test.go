package main

import (
	"net/url"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func TestConnectionURLPreservesCredentials(t *testing.T) {
	connection := connectionURL("localhost", 5432, "homeworldz", "p@ss:/?# word", "homeworldz")
	parsed, err := url.Parse(connection)
	if err != nil {
		t.Fatal(err)
	}
	password, ok := parsed.User.Password()
	if !ok || parsed.User.Username() != "homeworldz" || password != "p@ss:/?# word" {
		t.Fatalf("credentials were not preserved in %q", connection)
	}
	if parsed.Path != "/homeworldz" || parsed.Query().Get("sslmode") != "disable" {
		t.Fatalf("unexpected connection URL: %q", connection)
	}
}

func TestWriteDatabaseConfig(t *testing.T) {
	directory := filepath.Join(t.TempDir(), "config")
	if err := writeDatabaseConfig(directory, "postgres://example"); err != nil {
		t.Fatal(err)
	}
	contents, err := os.ReadFile(filepath.Join(directory, "db.ini"))
	if err != nil {
		t.Fatal(err)
	}
	if !strings.Contains(string(contents), "[database]\nurl = postgres://example") {
		t.Fatalf("unexpected config: %q", contents)
	}
}
