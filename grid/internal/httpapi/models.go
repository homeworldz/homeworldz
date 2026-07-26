package httpapi

import (
	"github.com/homeworldz/homeworldz/grid/internal/estate"
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
	// RegionProtocol is the region software's grid-region protocol version
	// (docs/CLIENT2.md, "the region protocol version"). Zero means the region
	// predates the handshake and is accepted; a non-zero value must match.
	RegionProtocol int `json:"regionProtocol,omitempty"`
}

type StartProvisionedRegionRequest struct {
	PublicEndpoint string `json:"publicEndpoint"`
	ViewerPort     int    `json:"viewerPort"`
	LeaseSeconds   int    `json:"leaseSeconds"`
	// RegionProtocol, as on RenewRegionLeaseRequest.
	RegionProtocol int `json:"regionProtocol,omitempty"`
}

type ProvisionedRegionRuntimeResult struct {
	regions.Region
	GridName      string         `json:"gridName"`
	GridPublicURL string         `json:"gridPublicUrl"`
	SizeX         int            `json:"sizeX"`
	SizeY         int            `json:"sizeY"`
	Maturity      int            `json:"maturity"`
	OwnerUserID   string         `json:"ownerUserId"`
	Estate        *estate.Estate `json:"estate,omitempty"`
	// RegionProtocol is the grid's current grid-region protocol version, so a
	// region that is behind can warn its operator before an increment is
	// enforced against it.
	RegionProtocol int `json:"regionProtocol"`
}

// EstateResult wraps an estate for the region-runtime estate endpoints.
type EstateResult struct {
	Estate estate.Estate `json:"estate"`
}

// EstateSettingsRequest updates estate scalar/flag fields; nil fields are unchanged.
type EstateSettingsRequest struct {
	Name           *string  `json:"name,omitempty"`
	OwnerUserID    *string  `json:"ownerUserId,omitempty"`
	Flags          *uint64  `json:"flags,omitempty"`
	PublicAccess   *bool    `json:"publicAccess,omitempty"`
	SunHour        *float64 `json:"sunHour,omitempty"`
	UseGlobalTime  *bool    `json:"useGlobalTime,omitempty"`
	FixedSun       *bool    `json:"fixedSun,omitempty"`
	BillableFactor *float64 `json:"billableFactor,omitempty"`
	PricePerMeter  *int     `json:"pricePerMeter,omitempty"`
	RedirectGridX  *int     `json:"redirectGridX,omitempty"`
	RedirectGridY  *int     `json:"redirectGridY,omitempty"`
	AbuseEmail     *string  `json:"abuseEmail,omitempty"`
}

func (r EstateSettingsRequest) toUpdate() estate.SettingsUpdate {
	return estate.SettingsUpdate{Name: r.Name, OwnerUserID: r.OwnerUserID, Flags: r.Flags,
		PublicAccess: r.PublicAccess, SunHour: r.SunHour, UseGlobalTime: r.UseGlobalTime,
		FixedSun: r.FixedSun, BillableFactor: r.BillableFactor, PricePerMeter: r.PricePerMeter,
		RedirectGridX: r.RedirectGridX, RedirectGridY: r.RedirectGridY, AbuseEmail: r.AbuseEmail}
}

// EstateMemberRequest adds or removes one access-list member (role 0=manager,
// 1=allowed user, 2=allowed group, 3=ban).
type EstateMemberRequest struct {
	MemberID string `json:"memberId"`
	Role     int    `json:"role"`
	Present  bool   `json:"present"`
}

type RegionList struct {
	Regions []regions.Region `json:"regions"`
}

type RegionNeighbor struct {
	Direction string         `json:"direction"`
	Region    RegionTopology `json:"region"`
}

type RegionTopology struct {
	ID             string `json:"id"`
	Name           string `json:"name"`
	GridX          int    `json:"gridX"`
	GridY          int    `json:"gridY"`
	SizeX          int    `json:"sizeX"`
	SizeY          int    `json:"sizeY"`
	Maturity       int    `json:"maturity"`
	PublicEndpoint string `json:"publicEndpoint,omitempty"`
	ViewerPort     int    `json:"viewerPort,omitempty"`
	Online         bool   `json:"online"`
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
	Size           int    `json:"size,omitempty"`
	Maturity       int    `json:"maturity,omitempty"`
	PublicEndpoint string `json:"publicEndpoint,omitempty"`
	ViewerPort     int    `json:"viewerPort,omitempty"`
	Enabled        *bool  `json:"enabled,omitempty"`
}

type UpdateProvisionedRegionRequest struct {
	Name           *string `json:"name,omitempty"`
	OwnerUserID    *string `json:"ownerUserId,omitempty"`
	MapX           *int    `json:"mapX,omitempty"`
	MapY           *int    `json:"mapY,omitempty"`
	Size           *int    `json:"size,omitempty"`
	Maturity       *int    `json:"maturity,omitempty"`
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
