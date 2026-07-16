package provisioning

import (
	"os"
	"path/filepath"
	"testing"
)

func TestLoadAndAuthenticate(t *testing.T) {
	path := filepath.Join(t.TempDir(), "regions.json")
	content := `[
  {"id":"11111111-1111-4111-8111-111111111111","name":"Welcome","mapX":1000,"mapY":1000,"accessKey":"welcome-key"},
  {"id":"22222222-2222-4222-8222-222222222222","name":"Sandbox","mapX":1001,"mapY":1000,"accessKey":"sandbox-key"}
]`
	if err := os.WriteFile(path, []byte(content), 0600); err != nil {
		t.Fatal(err)
	}
	registry, err := Load(path)
	if err != nil {
		t.Fatal(err)
	}
	region, ok := registry.Authenticate("22222222-2222-4222-8222-222222222222", "sandbox-key")
	if !ok || region.Name != "Sandbox" || region.MapX != 1001 {
		t.Fatalf("unexpected provisioned region: %#v, %v", region, ok)
	}
	if _, ok := registry.Authenticate(region.ID, "wrong"); ok {
		t.Fatal("wrong access key authenticated")
	}
}

func TestRejectsDuplicateCoordinates(t *testing.T) {
	path := filepath.Join(t.TempDir(), "regions.json")
	content := `[
  {"id":"11111111-1111-4111-8111-111111111111","name":"One","mapX":5,"mapY":6,"accessKey":"one"},
  {"id":"22222222-2222-4222-8222-222222222222","name":"Two","mapX":5,"mapY":6,"accessKey":"two"}
]`
	if err := os.WriteFile(path, []byte(content), 0600); err != nil {
		t.Fatal(err)
	}
	if _, err := Load(path); err == nil {
		t.Fatal("duplicate coordinates were accepted")
	}
}
