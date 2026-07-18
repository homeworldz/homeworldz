package provisioning

import (
	"bytes"
	"context"
	"crypto/subtle"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"regexp"
	"sort"
	"strings"
	"sync"
)

var uuidPattern = regexp.MustCompile(`^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[1-5][0-9a-fA-F]{3}-[89abAB][0-9a-fA-F]{3}-[0-9a-fA-F]{12}$`)

var (
	ErrNotFound = errors.New("provisioned region not found")
	ErrConflict = errors.New("provisioned region conflicts with an existing region")
	ErrInvalid  = errors.New("provisioned region is invalid")
)

type Region struct {
	ID          string `json:"id"`
	Name        string `json:"name"`
	OwnerUserID string `json:"ownerUserId,omitempty"`
	MapX        int    `json:"mapX"`
	MapY        int    `json:"mapY"`
	Enabled     bool   `json:"enabled"`
	AccessKey   string `json:"-"`
}

type Update struct {
	Name        *string
	OwnerUserID *string
	MapX        *int
	MapY        *int
	Enabled     *bool
}

type fileRegion struct {
	ID          string `json:"id"`
	Name        string `json:"name"`
	OwnerUserID string `json:"ownerUserId,omitempty"`
	MapX        int    `json:"mapX"`
	MapY        int    `json:"mapY"`
	Enabled     *bool  `json:"enabled,omitempty"`
	AccessKey   string `json:"accessKey"`
}

type Registry struct {
	mu   sync.RWMutex
	path string
	byID map[string]Region
}

type Store interface {
	Authenticate(context.Context, string, string) (Region, bool)
	List(context.Context) ([]Region, error)
	Get(context.Context, string) (Region, error)
	Create(context.Context, Region) (Region, error)
	Update(context.Context, string, Update) (Region, error)
	RotateAccessKey(context.Context, string, string) (Region, error)
	Delete(context.Context, string) error
}

func Load(path string) (*Registry, error) {
	content, err := os.ReadFile(path)
	if err != nil {
		return nil, fmt.Errorf("read provisioned regions: %w", err)
	}
	var stored []fileRegion
	decoder := json.NewDecoder(bytes.NewReader(content))
	decoder.DisallowUnknownFields()
	if err := decoder.Decode(&stored); err != nil {
		return nil, fmt.Errorf("decode provisioned regions: %w", err)
	}
	if err := decoder.Decode(&struct{}{}); err != io.EOF {
		return nil, fmt.Errorf("decode provisioned regions: file must contain one JSON array")
	}
	items := make(map[string]Region, len(stored))
	for index, item := range stored {
		enabled := true
		if item.Enabled != nil {
			enabled = *item.Enabled
		}
		region := Region{ID: item.ID, Name: item.Name, OwnerUserID: item.OwnerUserID,
			MapX: item.MapX, MapY: item.MapY, Enabled: enabled, AccessKey: item.AccessKey}
		if err := validate(region); err != nil {
			return nil, fmt.Errorf("invalid provisioned region at index %d: %w", index, err)
		}
		if _, exists := items[region.ID]; exists {
			return nil, fmt.Errorf("duplicate provisioned region id %q", region.ID)
		}
		items[region.ID] = region
	}
	if err := validateUnique(items, ""); err != nil {
		return nil, err
	}
	return &Registry{path: path, byID: items}, nil
}

func (r *Registry) Authenticate(_ context.Context, id, accessKey string) (Region, bool) {
	if r == nil {
		return Region{}, false
	}
	r.mu.RLock()
	defer r.mu.RUnlock()
	region, found := r.byID[id]
	if !found || !region.Enabled || subtle.ConstantTimeCompare([]byte(region.AccessKey), []byte(accessKey)) != 1 {
		return Region{}, false
	}
	return region, true
}

func (r *Registry) List(_ context.Context) ([]Region, error) {
	if r == nil {
		return nil, nil
	}
	r.mu.RLock()
	defer r.mu.RUnlock()
	items := make([]Region, 0, len(r.byID))
	for _, item := range r.byID {
		items = append(items, item)
	}
	sort.Slice(items, func(i, j int) bool {
		if items[i].MapY != items[j].MapY {
			return items[i].MapY < items[j].MapY
		}
		if items[i].MapX != items[j].MapX {
			return items[i].MapX < items[j].MapX
		}
		return items[i].ID < items[j].ID
	})
	return items, nil
}

func (r *Registry) Get(_ context.Context, id string) (Region, error) {
	if r == nil {
		return Region{}, ErrNotFound
	}
	r.mu.RLock()
	defer r.mu.RUnlock()
	item, found := r.byID[id]
	if !found {
		return Region{}, ErrNotFound
	}
	return item, nil
}

func (r *Registry) Create(_ context.Context, item Region) (Region, error) {
	if r == nil {
		return Region{}, ErrNotFound
	}
	item.Name = strings.TrimSpace(item.Name)
	item.OwnerUserID = strings.TrimSpace(item.OwnerUserID)
	if err := validate(item); err != nil {
		return Region{}, err
	}
	r.mu.Lock()
	defer r.mu.Unlock()
	if _, found := r.byID[item.ID]; found {
		return Region{}, ErrConflict
	}
	next := clone(r.byID)
	next[item.ID] = item
	if err := validateUnique(next, item.ID); err != nil {
		return Region{}, err
	}
	if err := r.persist(next); err != nil {
		return Region{}, err
	}
	r.byID = next
	return item, nil
}

func (r *Registry) Update(_ context.Context, id string, update Update) (Region, error) {
	if r == nil {
		return Region{}, ErrNotFound
	}
	r.mu.Lock()
	defer r.mu.Unlock()
	item, found := r.byID[id]
	if !found {
		return Region{}, ErrNotFound
	}
	if update.Name != nil {
		item.Name = strings.TrimSpace(*update.Name)
	}
	if update.OwnerUserID != nil {
		item.OwnerUserID = strings.TrimSpace(*update.OwnerUserID)
	}
	if update.MapX != nil {
		item.MapX = *update.MapX
	}
	if update.MapY != nil {
		item.MapY = *update.MapY
	}
	if update.Enabled != nil {
		item.Enabled = *update.Enabled
	}
	if err := validate(item); err != nil {
		return Region{}, err
	}
	next := clone(r.byID)
	next[id] = item
	if err := validateUnique(next, id); err != nil {
		return Region{}, err
	}
	if err := r.persist(next); err != nil {
		return Region{}, err
	}
	r.byID = next
	return item, nil
}

func (r *Registry) RotateAccessKey(_ context.Context, id, accessKey string) (Region, error) {
	if r == nil {
		return Region{}, ErrNotFound
	}
	if strings.TrimSpace(accessKey) == "" {
		return Region{}, fmt.Errorf("%w: access key is empty", ErrInvalid)
	}
	r.mu.Lock()
	defer r.mu.Unlock()
	item, found := r.byID[id]
	if !found {
		return Region{}, ErrNotFound
	}
	item.AccessKey = accessKey
	next := clone(r.byID)
	next[id] = item
	if err := r.persist(next); err != nil {
		return Region{}, err
	}
	r.byID = next
	return item, nil
}

func (r *Registry) Delete(_ context.Context, id string) error {
	if r == nil {
		return ErrNotFound
	}
	r.mu.Lock()
	defer r.mu.Unlock()
	if _, found := r.byID[id]; !found {
		return ErrNotFound
	}
	next := clone(r.byID)
	delete(next, id)
	if err := r.persist(next); err != nil {
		return err
	}
	r.byID = next
	return nil
}

func (r *Registry) persist(items map[string]Region) error {
	stored := make([]fileRegion, 0, len(items))
	for _, item := range items {
		enabled := item.Enabled
		stored = append(stored, fileRegion{ID: item.ID, Name: item.Name, OwnerUserID: item.OwnerUserID,
			MapX: item.MapX, MapY: item.MapY, Enabled: &enabled, AccessKey: item.AccessKey})
	}
	sort.Slice(stored, func(i, j int) bool {
		if stored[i].MapY != stored[j].MapY {
			return stored[i].MapY < stored[j].MapY
		}
		if stored[i].MapX != stored[j].MapX {
			return stored[i].MapX < stored[j].MapX
		}
		return stored[i].ID < stored[j].ID
	})
	content, err := json.MarshalIndent(stored, "", "  ")
	if err != nil {
		return fmt.Errorf("encode provisioned regions: %w", err)
	}
	content = append(content, '\n')
	temporary, err := os.CreateTemp(filepath.Dir(r.path), ".regions-*.json")
	if err != nil {
		return fmt.Errorf("create provisioned regions update: %w", err)
	}
	temporaryPath := temporary.Name()
	defer os.Remove(temporaryPath)
	if err := temporary.Chmod(0600); err != nil {
		temporary.Close()
		return fmt.Errorf("protect provisioned regions update: %w", err)
	}
	if _, err := temporary.Write(content); err != nil {
		temporary.Close()
		return fmt.Errorf("write provisioned regions update: %w", err)
	}
	if err := temporary.Sync(); err != nil {
		temporary.Close()
		return fmt.Errorf("sync provisioned regions update: %w", err)
	}
	if err := temporary.Close(); err != nil {
		return fmt.Errorf("close provisioned regions update: %w", err)
	}
	if err := os.Rename(temporaryPath, r.path); err != nil {
		return fmt.Errorf("replace provisioned regions: %w", err)
	}
	return nil
}

func validate(item Region) error {
	if !uuidPattern.MatchString(item.ID) || strings.TrimSpace(item.Name) == "" || len(item.Name) > 128 ||
		item.MapX < 0 || item.MapY < 0 || strings.TrimSpace(item.AccessKey) == "" {
		return fmt.Errorf("%w: UUID, name, coordinates, or access key is invalid", ErrInvalid)
	}
	if item.OwnerUserID != "" && !uuidPattern.MatchString(item.OwnerUserID) {
		return fmt.Errorf("%w: owner user UUID is invalid", ErrInvalid)
	}
	return nil
}

func validateUnique(items map[string]Region, changedID string) error {
	coordinates := make(map[[2]int]string, len(items))
	names := make(map[string]string, len(items))
	for id, item := range items {
		name := strings.ToLower(item.Name)
		if other, exists := names[name]; exists && other != id {
			return fmt.Errorf("%w: region %q shares a name with %q", ErrConflict, id, other)
		}
		coordinate := [2]int{item.MapX, item.MapY}
		if other, exists := coordinates[coordinate]; exists && other != id {
			return fmt.Errorf("%w: region %q shares map coordinates with %q", ErrConflict, id, other)
		}
		names[name] = id
		coordinates[coordinate] = id
	}
	_ = changedID
	return nil
}

func clone(items map[string]Region) map[string]Region {
	result := make(map[string]Region, len(items))
	for id, item := range items {
		result[id] = item
	}
	return result
}
