package main

import (
	"os"
	"path/filepath"
	"reflect"
	"strings"
	"testing"
)

func TestLoadSmokeConfig(t *testing.T) {
	path := filepath.Join(t.TempDir(), "smoke-test.ini")
	if err := os.WriteFile(path, []byte("[user]\nfirst_name = Configured\nlast_name = Tester\npassword = configured-password\n"), 0o600); err != nil {
		t.Fatal(err)
	}

	got, err := loadSmokeConfig(path)
	if err != nil {
		t.Fatal(err)
	}
	want := smokeConfig{firstName: "Configured", lastName: "Tester", password: "configured-password"}
	if !reflect.DeepEqual(got, want) {
		t.Fatalf("smoke config = %#v, want %#v", got, want)
	}
}

func TestLoadSmokeConfigAllowsMissingFile(t *testing.T) {
	got, err := loadSmokeConfig(filepath.Join(t.TempDir(), "missing.ini"))
	if err != nil {
		t.Fatal(err)
	}
	if got != (smokeConfig{}) {
		t.Fatalf("smoke config = %#v, want empty", got)
	}
}

func TestViewerArgumentsDisableVoice(t *testing.T) {
	got := viewerArguments("http://127.0.0.1:42000")
	want := []string{"--grid", "http://127.0.0.1:42000", "--novoice"}
	if !reflect.DeepEqual(got, want) {
		t.Fatalf("viewer arguments = %#v, want %#v", got, want)
	}
}

func TestValidUsername(t *testing.T) {
	for _, value := range []string{"smoke.user", "test_123", "a-b"} {
		if !validUsername(value) {
			t.Errorf("validUsername(%q) = false", value)
		}
	}
	for _, value := range []string{"ab", "Smoke.User", "bad name", strings.Repeat("a", 33)} {
		if validUsername(value) {
			t.Errorf("validUsername(%q) = true", value)
		}
	}
}

func TestEnvironmentWithReplacesStaleValue(t *testing.T) {
	t.Setenv("HOMEWORLDZ_DATABASE_URL", "stale")
	result := environmentWith(map[string]string{"HOMEWORLDZ_DATABASE_URL": "current"})
	count := 0
	for _, entry := range result {
		if strings.EqualFold(strings.SplitN(entry, "=", 2)[0], "HOMEWORLDZ_DATABASE_URL") {
			count++
			if entry != "HOMEWORLDZ_DATABASE_URL=current" {
				t.Fatalf("database environment = %q", entry)
			}
		}
	}
	if count != 1 {
		t.Fatalf("database environment count = %d, want 1; parent has %d entries", count, len(os.Environ()))
	}
}
