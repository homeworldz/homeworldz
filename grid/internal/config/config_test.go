package config

import (
	"os"
	"path/filepath"
	"testing"
)

func TestLoadGridFromINI(t *testing.T) {
	directory := t.TempDir()
	writeFile(t, directory, "grid.ini", "[server]\naddress=:43000\n[auth]\nservice_token=file-token\n")
	writeFile(t, directory, "db.ini", "[database]\nurl=postgres://user:semicolon;hash#password@file/database\n")

	got, err := LoadGrid(directory)
	if err != nil {
		t.Fatal(err)
	}
	if got.Address != ":43000" || got.PublicURL != "http://127.0.0.1:42000" || got.DatabaseURL != "postgres://user:semicolon;hash#password@file/database" || got.ServiceToken != "file-token" {
		t.Fatalf("unexpected configuration: %#v", got)
	}
}

func TestMissingFilesAreRejected(t *testing.T) {
	if _, err := LoadGrid(t.TempDir()); err == nil {
		t.Fatal("missing configuration files were accepted")
	}
}

func TestPublicURLOverrideIsValidated(t *testing.T) {
	directory := t.TempDir()
	writeFile(t, directory, "grid.ini", "[server]\npublic_url=http://grid.example:8002/\n")
	writeFile(t, directory, "db.ini", "[database]\nurl=postgres://file/database\n")

	got, err := LoadGrid(directory)
	if err != nil || got.PublicURL != "http://grid.example:8002" {
		t.Fatalf("public URL = %q, error = %v", got.PublicURL, err)
	}
	writeFile(t, directory, "grid.ini", "[server]\npublic_url=javascript:bad\n")
	if _, err := LoadGrid(directory); err == nil {
		t.Fatal("invalid public URL was accepted")
	}
}

func writeFile(t *testing.T, directory, name, contents string) {
	t.Helper()
	if err := os.WriteFile(filepath.Join(directory, name), []byte(contents), 0o600); err != nil {
		t.Fatal(err)
	}
}
