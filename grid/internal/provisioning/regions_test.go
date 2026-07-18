package provisioning

import (
	"errors"
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

func TestManagementMutationsPersistAtomically(t *testing.T) {
	path := filepath.Join(t.TempDir(), "regions.json")
	if err := os.WriteFile(path, []byte("[]\n"), 0600); err != nil {
		t.Fatal(err)
	}
	registry, err := Load(path)
	if err != nil {
		t.Fatal(err)
	}
	id := "11111111-1111-4111-8111-111111111111"
	created, err := registry.Create(Region{ID: id, Name: "Welcome", MapX: 1000, MapY: 1000,
		Enabled: true, AccessKey: "initial-key"})
	if err != nil || created.Name != "Welcome" {
		t.Fatalf("create = %#v, %v", created, err)
	}
	name, x, enabled := "Welcome Region", 1002, false
	updated, err := registry.Update(id, Update{Name: &name, MapX: &x, Enabled: &enabled})
	if err != nil || updated.Name != name || updated.MapX != x || updated.Enabled {
		t.Fatalf("update = %#v, %v", updated, err)
	}
	if _, ok := registry.Authenticate(id, "initial-key"); ok {
		t.Fatal("disabled region authenticated")
	}
	if _, err := registry.RotateAccessKey(id, "rotated-key"); err != nil {
		t.Fatal(err)
	}
	enabled = true
	if _, err := registry.Update(id, Update{Enabled: &enabled}); err != nil {
		t.Fatal(err)
	}
	if _, ok := registry.Authenticate(id, "initial-key"); ok {
		t.Fatal("old access key authenticated after rotation")
	}
	if _, ok := registry.Authenticate(id, "rotated-key"); !ok {
		t.Fatal("rotated access key did not authenticate")
	}

	reloaded, err := Load(path)
	if err != nil {
		t.Fatal(err)
	}
	retained, err := reloaded.Get(id)
	if err != nil || retained.Name != name || retained.MapX != x || !retained.Enabled {
		t.Fatalf("reloaded = %#v, %v", retained, err)
	}
	if _, ok := reloaded.Authenticate(id, "rotated-key"); !ok {
		t.Fatal("persisted rotated key did not authenticate")
	}
	if err := reloaded.Delete(id); err != nil {
		t.Fatal(err)
	}
	if _, err := reloaded.Get(id); !errors.Is(err, ErrNotFound) {
		t.Fatalf("get deleted region error = %v", err)
	}
	empty, err := Load(path)
	if err != nil || len(empty.List()) != 0 {
		t.Fatalf("deleted registry reload = %#v, %v", empty.List(), err)
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
