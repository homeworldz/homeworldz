package httpapi

import (
	"github.com/homeworldz/homeworldz/grid/internal/inventory"
	"github.com/homeworldz/homeworldz/grid/internal/presence"
	"github.com/homeworldz/homeworldz/grid/internal/provisioning"
	"github.com/homeworldz/homeworldz/grid/internal/regions"
	"github.com/homeworldz/homeworldz/grid/internal/transit"
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

type StartProvisionedRegionRequest struct {
	PublicEndpoint string `json:"publicEndpoint"`
	ViewerPort     int    `json:"viewerPort"`
	LeaseSeconds   int    `json:"leaseSeconds"`
}

type ProvisionedRegionRuntimeResult struct {
	regions.Region
	GridName      string `json:"gridName"`
	GridPublicURL string `json:"gridPublicUrl"`
}

type RegionList struct {
	Regions []regions.Region `json:"regions"`
}

type RegionNeighbor struct {
	Direction string         `json:"direction"`
	Region    regions.Region `json:"region"`
}

type RegionNeighborList struct {
	Neighbors []RegionNeighbor `json:"neighbors"`
}

type CreateProvisionedRegionRequest struct {
	ID             string `json:"id,omitempty"`
	Name           string `json:"name"`
	OwnerUserID    string `json:"ownerUserId,omitempty"`
	MapX           int    `json:"mapX"`
	MapY           int    `json:"mapY"`
	PublicEndpoint string `json:"publicEndpoint,omitempty"`
	ViewerPort     int    `json:"viewerPort,omitempty"`
	Enabled        *bool  `json:"enabled,omitempty"`
}

type UpdateProvisionedRegionRequest struct {
	Name           *string `json:"name,omitempty"`
	OwnerUserID    *string `json:"ownerUserId,omitempty"`
	MapX           *int    `json:"mapX,omitempty"`
	MapY           *int    `json:"mapY,omitempty"`
	PublicEndpoint *string `json:"publicEndpoint,omitempty"`
	ViewerPort     *int    `json:"viewerPort,omitempty"`
	Enabled        *bool   `json:"enabled,omitempty"`
}

type ProvisionedRegionResult struct {
	Region    provisioning.Region `json:"region"`
	AccessKey string              `json:"accessKey,omitempty"`
}

type ProvisionedRegionList struct {
	Regions []provisioning.Region `json:"regions"`
}

type PrepareTransitRequest struct {
	ID                  string          `json:"id"`
	AgentID             string          `json:"agentId"`
	SessionID           string          `json:"sessionId"`
	SourceRegionID      string          `json:"sourceRegionId"`
	DestinationRegionID string          `json:"destinationRegionId"`
	Position            transit.Vector3 `json:"position"`
	LookAt              transit.Vector3 `json:"lookAt"`
	Flying              bool            `json:"flying"`
	LifetimeSeconds     int             `json:"lifetimeSeconds"`
}

type TransitActionRequest struct {
	RegionID string `json:"regionId"`
	Reason   string `json:"reason,omitempty"`
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

type UpdateLocationRequest struct {
	RegionID string          `json:"regionId"`
	Position transit.Vector3 `json:"position"`
	LookAt   transit.Vector3 `json:"lookAt"`
	Flying   bool            `json:"flying"`
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

type CreateInventoryItemRequest struct {
	ID                  string `json:"id"`
	CreatorUserID       string `json:"creatorUserId"`
	FolderID            string `json:"folderId"`
	AssetID             string `json:"assetId"`
	AssetType           int    `json:"assetType"`
	InventoryType       int    `json:"inventoryType"`
	Name                string `json:"name"`
	Description         string `json:"description"`
	Flags               uint32 `json:"flags"`
	BasePermissions     uint32 `json:"basePermissions"`
	CurrentPermissions  uint32 `json:"currentPermissions"`
	EveryonePermissions uint32 `json:"everyonePermissions"`
	NextPermissions     uint32 `json:"nextPermissions"`
}

type CopyLibraryInventoryItemRequest struct {
	SourceItemID        string `json:"sourceItemId"`
	DestinationFolderID string `json:"destinationFolderId"`
	Name                string `json:"name"`
}

type CopyInventoryItemRequest struct {
	SourceItemID        string `json:"sourceItemId"`
	DestinationFolderID string `json:"destinationFolderId"`
	Name                string `json:"name"`
}

type MoveInventoryFolderRequest struct {
	ParentID string `json:"parentId"`
}

type MoveInventoryItemRequest struct {
	FolderID string `json:"folderId"`
	Name     string `json:"name"`
}

type RegisterAssetRequest struct {
	ID            string `json:"id"`
	CreatorUserID string `json:"creatorUserId"`
	SHA256        string `json:"sha256"`
	Size          int64  `json:"size"`
	Endpoint      string `json:"endpoint"`
	Origin        bool   `json:"origin"`
}
