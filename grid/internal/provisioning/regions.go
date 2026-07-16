package provisioning

import (
	"bytes"
	"crypto/subtle"
	"encoding/json"
	"fmt"
	"io"
	"os"
	"regexp"
	"strings"
)

var uuidPattern = regexp.MustCompile(`^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[1-5][0-9a-fA-F]{3}-[89abAB][0-9a-fA-F]{3}-[0-9a-fA-F]{12}$`)

type Region struct {
	ID        string `json:"id"`
	Name      string `json:"name"`
	MapX      int    `json:"mapX"`
	MapY      int    `json:"mapY"`
	AccessKey string `json:"accessKey"`
}

type Registry struct {
	byID map[string]Region
}

func Load(path string) (*Registry, error) {
	content, err := os.ReadFile(path)
	if err != nil {
		return nil, fmt.Errorf("read provisioned regions: %w", err)
	}
	var items []Region
	decoder := json.NewDecoder(bytes.NewReader(content))
	decoder.DisallowUnknownFields()
	if err := decoder.Decode(&items); err != nil {
		return nil, fmt.Errorf("decode provisioned regions: %w", err)
	}
	if err := decoder.Decode(&struct{}{}); err != io.EOF {
		return nil, fmt.Errorf("decode provisioned regions: file must contain one JSON array")
	}
	registry := &Registry{byID: make(map[string]Region, len(items))}
	coordinates := make(map[[2]int]string, len(items))
	names := make(map[string]string, len(items))
	for index, item := range items {
		item.Name = strings.TrimSpace(item.Name)
		if !uuidPattern.MatchString(item.ID) || item.Name == "" || item.MapX < 0 || item.MapY < 0 || item.AccessKey == "" {
			return nil, fmt.Errorf("invalid provisioned region at index %d", index)
		}
		if _, exists := registry.byID[item.ID]; exists {
			return nil, fmt.Errorf("duplicate provisioned region id %q", item.ID)
		}
		if other, exists := names[item.Name]; exists {
			return nil, fmt.Errorf("region %q shares name with %q", item.ID, other)
		}
		coordinate := [2]int{item.MapX, item.MapY}
		if other, exists := coordinates[coordinate]; exists {
			return nil, fmt.Errorf("region %q shares map coordinates with %q", item.ID, other)
		}
		registry.byID[item.ID] = item
		coordinates[coordinate] = item.ID
		names[item.Name] = item.ID
	}
	return registry, nil
}

func (r *Registry) Authenticate(id, accessKey string) (Region, bool) {
	if r == nil {
		return Region{}, false
	}
	region, found := r.byID[id]
	if !found || subtle.ConstantTimeCompare([]byte(region.AccessKey), []byte(accessKey)) != 1 {
		return Region{}, false
	}
	return region, true
}
