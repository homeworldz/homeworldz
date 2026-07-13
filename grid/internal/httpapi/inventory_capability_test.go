package httpapi

import (
	"bytes"
	"context"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
	"time"

	"github.com/homeworldz/homeworldz/grid/internal/inventory"
)

func TestInventoryDescendentsCapability(t *testing.T) {
	identities := newMemoryIdentityStore()
	user, err := identities.CreateUser(context.Background(), "inventory.user", "development-password")
	if err != nil {
		t.Fatal(err)
	}
	session, err := identities.CreateSession(context.Background(), "inventory.user", "development-password", time.Hour)
	if err != nil {
		t.Fatal(err)
	}
	if err := identities.AssignViewerDestination(context.Background(), session.ID, 123456,
		"30000000-0000-4000-8000-000000000001"); err != nil {
		t.Fatal(err)
	}
	inventories := &memoryInventoryStore{folders: make(map[string][]inventory.Folder)}
	folders, _ := inventories.EnsureSystemFolders(context.Background(), user.ID)
	handler := New(checker{}, "test", Options{Identity: identities, Inventory: inventories})
	body := `<?xml version="1.0"?><llsd><map><key>folders</key><array><map>` +
		`<key>folder_id</key><uuid>` + folders[0].ID + `</uuid><key>owner_id</key><uuid>` + user.ID +
		`</uuid><key>fetch_folders</key><boolean>true</boolean><key>fetch_items</key><boolean>true</boolean>` +
		`</map></array></map></llsd>`
	r := httptest.NewRequest(http.MethodPost, "/caps/inventory/descendents/"+session.ID, bytes.NewBufferString(body))
	w := httptest.NewRecorder()
	handler.ServeHTTP(w, r)
	if w.Code != http.StatusOK || !strings.HasPrefix(w.Header().Get("Content-Type"), "application/llsd+xml") {
		t.Fatalf("status = %d, content type = %q, body = %s", w.Code, w.Header().Get("Content-Type"), w.Body.String())
	}
	content := w.Body.String()
	if !strings.Contains(content, "<key>descendents</key><integer>20</integer>") ||
		!strings.Contains(content, "<key>type_default</key><integer>13</integer>") ||
		!strings.Contains(content, "<key>items</key><array/>") {
		t.Fatalf("inventory response = %s", content)
	}
}

func TestInventoryDescendentsCapabilityRejectsBadRequest(t *testing.T) {
	handler := New(checker{}, "test", Options{})
	r := httptest.NewRequest(http.MethodGet, "/caps/inventory/descendents/not-a-session", nil)
	w := httptest.NewRecorder()
	handler.ServeHTTP(w, r)
	if w.Code != http.StatusMethodNotAllowed {
		t.Fatalf("status = %d, want %d", w.Code, http.StatusMethodNotAllowed)
	}
}
