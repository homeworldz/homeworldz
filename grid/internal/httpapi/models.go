package httpapi

import (
	"github.com/homeworldz/homeworldz/grid/internal/inventory"
	"github.com/homeworldz/homeworldz/grid/internal/presence"
	"github.com/homeworldz/homeworldz/grid/internal/regions"
)

const APIVersion = "v1"

// Status is the response model for a successful operational status probe.
type Status struct {
	Status string `json:"status"`
}

// Version identifies a service build and its internal API compatibility level.
type Version struct {
	Service    string `json:"service"`
	Version    string `json:"version"`
	APIVersion string `json:"apiVersion"`
}

// Error is the common error response model.
type Error struct {
	Code    string `json:"code"`
	Message string `json:"message"`
}

type RegisterRegionRequest struct {
	Name           string `json:"name"`
	GridX          int    `json:"gridX"`
	GridY          int    `json:"gridY"`
	PublicEndpoint string `json:"publicEndpoint"`
	ViewerPort     int    `json:"viewerPort"`
	LeaseSeconds   int    `json:"leaseSeconds"`
}

type RenewRegionLeaseRequest struct {
	LeaseSeconds int `json:"leaseSeconds"`
}

type RegionList struct {
	Regions []regions.Region `json:"regions"`
}

type CreateUserRequest struct {
	Username string `json:"username"`
	Password string `json:"password"`
}

type CreateSessionRequest struct {
	Username       string `json:"username"`
	Password       string `json:"password"`
	SessionSeconds int    `json:"sessionSeconds"`
}

type UpdatePresenceRequest struct {
	RegionID string `json:"regionId"`
}

type PresenceList struct {
	Presence []presence.Presence `json:"presence"`
}

type InventoryFolderList struct {
	Folders []inventory.Folder `json:"folders"`
}

type CreateInventoryFolderRequest struct {
	ID          string `json:"id"`
	ParentID    string `json:"parentId"`
	Name        string `json:"name"`
	TypeDefault int    `json:"typeDefault"`
}
