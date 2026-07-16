package main

import (
	"context"
	"crypto/sha256"
	"encoding/json"
	"fmt"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"reflect"
	"strings"
	"testing"
)

func TestValidateAssetFederation(t *testing.T) {
	content := []byte("federated asset")
	hash := sha256.Sum256(content)
	const assetID = "11111111-1111-4111-8111-111111111111"
	const token = "test-service-token"
	var server *httptest.Server
	server = httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.Header.Get("Authorization") != "Bearer "+token {
			if r.Header.Get("Authorization") == "" && strings.HasPrefix(r.URL.Path, "/region/") {
				http.Error(w, "unauthorized", http.StatusUnauthorized)
				return
			}
			t.Errorf("authorization = %q", r.Header.Get("Authorization"))
			http.Error(w, "unauthorized", http.StatusUnauthorized)
			return
		}
		switch r.URL.Path {
		case "/grid/api/v1/assets/" + assetID:
			_ = json.NewEncoder(w).Encode(assetFederationMetadata{
				ID: assetID, SHA256: fmt.Sprintf("%x", hash), Size: int64(len(content)),
				Locations: []struct {
					Endpoint string `json:"endpoint"`
					Origin   bool   `json:"origin"`
				}{{Endpoint: server.URL + "/region", Origin: true}},
			})
		case "/region/api/v1/assets/" + assetID:
			_, _ = w.Write(content)
		default:
			http.NotFound(w, r)
		}
	}))
	defer server.Close()
	if err := validateAssetFederation(
		context.Background(), server.URL+"/grid", server.URL+"/region", token, assetID); err != nil {
		t.Fatal(err)
	}
}

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

func TestRuntimeConfigurationUsesFiles(t *testing.T) {
	directory := t.TempDir()
	regionPath := filepath.Join(directory, "region.ini")
	err := writeRuntimeConfiguration(directory, "postgres://file/database", "file-token", regionPath,
		runtimeRegionSettings{name: "Test Region", gridX: 2, gridY: 3,
			publicEndpoint: regionURL, httpPort: 42001, viewerPort: 42002,
			dataPath: "data", assetPath: "assets", terrainPath: "terrain.raw"})
	if err != nil {
		t.Fatal(err)
	}
	for _, path := range []string{filepath.Join(directory, "grid.ini"), filepath.Join(directory, "db.ini"), regionPath} {
		if info, err := os.Stat(path); err != nil || info.Size() == 0 {
			t.Fatalf("runtime configuration %q was not written: %v", path, err)
		}
	}
	contents, err := os.ReadFile(regionPath)
	if err != nil || !strings.Contains(string(contents), "name = Test Region") || strings.Contains(string(contents), "HOMEWORLDZ_") {
		t.Fatalf("unexpected region configuration: %q, %v", contents, err)
	}
}
