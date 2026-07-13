package main

import (
	"os"
	"strings"
	"testing"
)

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
