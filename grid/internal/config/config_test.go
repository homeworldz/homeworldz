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
	t.Setenv("HOMEWORLDZ_CONFIG_DIR", directory)
	unsetEnv(t, "HOMEWORLDZ_DATABASE_URL")
	unsetEnv(t, "HOMEWORLDZ_GRID_ADDR")
	unsetEnv(t, "HOMEWORLDZ_GRID_SERVICE_TOKEN")

	got, err := LoadGrid()
	if err != nil {
		t.Fatal(err)
	}
	if got.Address != ":43000" || got.DatabaseURL != "postgres://user:semicolon;hash#password@file/database" || got.ServiceToken != "file-token" {
		t.Fatalf("unexpected configuration: %#v", got)
	}
}

func TestEnvironmentOverridesINI(t *testing.T) {
	directory := t.TempDir()
	writeFile(t, directory, "grid.ini", "[server]\naddress=:43000\n")
	t.Setenv("HOMEWORLDZ_CONFIG_DIR", directory)
	t.Setenv("HOMEWORLDZ_GRID_ADDR", ":44000")

	got, err := LoadGrid()
	if err != nil {
		t.Fatal(err)
	}
	if got.Address != ":44000" {
		t.Fatalf("address = %q, want :44000", got.Address)
	}
}

func TestMissingFilesUseDefaults(t *testing.T) {
	t.Setenv("HOMEWORLDZ_CONFIG_DIR", t.TempDir())
	got, err := LoadGrid()
	if err != nil {
		t.Fatal(err)
	}
	if got.Address != "127.0.0.1:42000" {
		t.Fatalf("address = %q, want 127.0.0.1:42000", got.Address)
	}
}

func writeFile(t *testing.T, directory, name, contents string) {
	t.Helper()
	if err := os.WriteFile(filepath.Join(directory, name), []byte(contents), 0o600); err != nil {
		t.Fatal(err)
	}
}

func unsetEnv(t *testing.T, name string) {
	t.Helper()
	value, present := os.LookupEnv(name)
	if err := os.Unsetenv(name); err != nil {
		t.Fatalf("unset %s: %v", name, err)
	}
	t.Cleanup(func() {
		if present {
			if err := os.Setenv(name, value); err != nil {
				t.Errorf("restore %s: %v", name, err)
			}
		} else if err := os.Unsetenv(name); err != nil {
			t.Errorf("clear %s: %v", name, err)
		}
	})
}
