package main

import (
	"os"
	"path/filepath"
	"testing"
)

func TestSafeVersion(t *testing.T) {
	for _, value := range []string{"dev", "0.1.0", "preview-2", "build_3"} {
		if got, err := safeVersion(value); err != nil || got != value {
			t.Fatalf("safeVersion(%q) = %q, %v", value, got, err)
		}
	}
	for _, value := range []string{"", "../release", "release candidate", "v1/2"} {
		if _, err := safeVersion(value); err == nil {
			t.Fatalf("safeVersion(%q) unexpectedly succeeded", value)
		}
	}
}

func TestVisualCRuntimeEntriesUnder(t *testing.T) {
	installation := t.TempDir()
	older := filepath.Join(installation, "VC", "Redist", "MSVC", "14.40", "x64", "Microsoft.VC143.CRT")
	newer := filepath.Join(installation, "VC", "Redist", "MSVC", "14.51", "x64", "Microsoft.VC145.CRT")
	for _, directory := range []string{older, newer} {
		if err := os.MkdirAll(directory, 0o755); err != nil {
			t.Fatal(err)
		}
		for _, name := range []string{"msvcp140.dll", "vcruntime140.dll", "vcruntime140_1.dll"} {
			if err := os.WriteFile(filepath.Join(directory, name), []byte(name), 0o644); err != nil {
				t.Fatal(err)
			}
		}
	}
	entries, err := visualCRuntimeEntriesUnder(installation)
	if err != nil || len(entries) != 3 {
		t.Fatalf("runtime entries = %#v, %v", entries, err)
	}
	for _, entry := range entries {
		if filepath.Dir(entry.source) != newer || entry.name != filepath.Base(entry.source) {
			t.Fatalf("runtime entry = %#v", entry)
		}
	}
}
