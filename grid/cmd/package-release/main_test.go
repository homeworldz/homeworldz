package main

import (
	"archive/tar"
	"compress/gzip"
	"io"
	"os"
	"path/filepath"
	"testing"
	"time"
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

func TestWriteTarGZUsesStablePathsAndExecutableModes(t *testing.T) {
	directory := t.TempDir()
	binary := filepath.Join(directory, "homeworldz-grid")
	config := filepath.Join(directory, "grid.ini")
	if err := os.WriteFile(binary, []byte("binary"), 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(config, []byte("config"), 0o644); err != nil {
		t.Fatal(err)
	}
	path := filepath.Join(directory, "release.tar.gz")
	if err := writeTarGZ(path, "homeworldz-grid", []archiveEntry{
		{source: config, name: "config/examples/grid.ini"},
		{source: binary, name: "homeworldz-grid"},
	}); err != nil {
		t.Fatal(err)
	}
	file, err := os.Open(path)
	if err != nil {
		t.Fatal(err)
	}
	defer file.Close()
	compressed, err := gzip.NewReader(file)
	if err != nil {
		t.Fatal(err)
	}
	archive := tar.NewReader(compressed)
	want := map[string]int64{
		"homeworldz-grid/config/examples/grid.ini": 0o644,
		"homeworldz-grid/homeworldz-grid":          0o755,
	}
	for {
		header, err := archive.Next()
		if err == io.EOF {
			break
		}
		if err != nil {
			t.Fatal(err)
		}
		mode, found := want[header.Name]
		if !found {
			t.Fatalf("unexpected archive entry %q", header.Name)
		}
		if header.Mode != mode {
			t.Fatalf("%s mode = %o, want %o", header.Name, header.Mode, mode)
		}
		if !header.ModTime.Equal(time.Unix(0, 0).UTC()) {
			t.Fatalf("%s has unstable timestamp %s", header.Name, header.ModTime)
		}
		delete(want, header.Name)
	}
	if len(want) != 0 {
		t.Fatalf("missing archive entries: %#v", want)
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
