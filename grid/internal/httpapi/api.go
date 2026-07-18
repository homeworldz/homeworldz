package httpapi

import (
	"context"
	"encoding/json"
	"errors"
	"io"
	"log/slog"
	"net/http"
	"net/url"
	"strings"
	"time"

	"github.com/homeworldz/homeworldz/grid/internal/assetmeta"
	"github.com/homeworldz/homeworldz/grid/internal/identity"
	"github.com/homeworldz/homeworldz/grid/internal/inventory"
	"github.com/homeworldz/homeworldz/grid/internal/locations"
	"github.com/homeworldz/homeworldz/grid/internal/presence"
	"github.com/homeworldz/homeworldz/grid/internal/provisioning"
	"github.com/homeworldz/homeworldz/grid/internal/regions"
	"github.com/homeworldz/homeworldz/grid/internal/tasktransfer"
	"github.com/homeworldz/homeworldz/grid/internal/transit"
)

type ReadinessChecker interface {
	PingContext(context.Context) error
}

type API struct {
	ready         ReadinessChecker
	version       string
	publicURL     string
	gridName      string
	regions       regions.Store
	identity      identity.Store
	presence      presence.Store
	inventory     inventory.Store
	assets        assetmeta.Store
	serviceToken  string
	provisioned   provisioning.Store
	terrainHTTP   *http.Client
	terrainCache  terrainTileCache
	transits      transit.Store
	taskTransfers tasktransfer.Store
	locations     locations.Store
}

type Options struct {
	ServiceToken      string
	GridPublicURL     string
	GridName          string
	Logger            *slog.Logger
	Regions           regions.Store
	Identity          identity.Store
	Presence          presence.Store
	Inventory         inventory.Store
	Assets            assetmeta.Store
	Provisioned       provisioning.Store
	TerrainHTTPClient *http.Client
	Transits          transit.Store
	TaskTransfers     tasktransfer.Store
	Locations         locations.Store
}

func New(ready ReadinessChecker, version string, options Options) http.Handler {
	a := &API{ready: ready, version: version, publicURL: strings.TrimRight(options.GridPublicURL, "/"),
		gridName: strings.TrimSpace(options.GridName),
		regions:  options.Regions, identity: options.Identity, presence: options.Presence,
		inventory: options.Inventory, assets: options.Assets, serviceToken: options.ServiceToken,
		provisioned: options.Provisioned, terrainHTTP: options.TerrainHTTPClient,
		terrainCache: newTerrainTileCache(), transits: options.Transits,
		taskTransfers: options.TaskTransfers, locations: options.Locations}
	if a.publicURL == "" {
		a.publicURL = "http://127.0.0.1:42000"
	}
	if a.gridName == "" {
		a.gridName = "HomeWorldz"
	}
	mux := http.NewServeMux()
	mux.HandleFunc("/get_grid_info", getOnly(a.gridInfo))
	mux.HandleFunc("/welcome", getOnly(a.welcome))
	mux.HandleFunc("/assets/homeworldz.svg", getOnly(a.logo))
	mux.HandleFunc("/map/", getOnly(a.mapTile))
	mux.HandleFunc("/ping", getOnly(a.ping))
	mux.HandleFunc("/ready", getOnly(a.readiness))
	mux.HandleFunc("/version", getOnly(a.buildVersion))
	mux.HandleFunc("/login", a.viewerLogin)
	mux.HandleFunc("/caps/inventory/descendents/", a.inventoryDescendentsCapability)
	mux.HandleFunc("/caps/inventory/items/", a.inventoryItemsCapability)
	mux.HandleFunc("/caps/inventory/create-folder/", a.createInventoryFolderCapability)
	mux.HandleFunc("/caps/inventory/ais/", a.inventoryAISCapability)
	mux.HandleFunc("/caps/inventory/library/", a.libraryAISCapability)
	mux.HandleFunc("/api/v1/regions", a.regionsRoot)
	mux.HandleFunc("/api/v1/regions/", a.regionByID)
	mux.HandleFunc("/api/v1/provisioned-regions", a.provisionedRegionsRoot)
	mux.HandleFunc("/api/v1/provisioned-regions/", a.provisionedRegionByID)
	mux.HandleFunc("/api/v1/region-runtime/", a.provisionedRegionRuntime)
	mux.HandleFunc("/api/v1/users", a.usersRoot)
	mux.HandleFunc("/api/v1/users/", a.userByID)
	mux.HandleFunc("/api/v1/sessions", a.sessionsRoot)
	mux.HandleFunc("/api/v1/sessions/", a.sessionByID)
	mux.HandleFunc("/api/v1/presence", a.presenceRoot)
	mux.HandleFunc("/api/v1/presence/", a.presenceByUser)
	mux.HandleFunc("/api/v1/locations/", a.locationByUser)
	mux.HandleFunc("/api/v1/inventory/", a.inventoryByUser)
	mux.HandleFunc("/api/v1/assets", a.assetsRoot)
	mux.HandleFunc("/api/v1/assets/", a.assetByID)
	mux.HandleFunc("/api/v1/transits", a.transitsRoot)
	mux.HandleFunc("/api/v1/transits/", a.transitByID)
	mux.HandleFunc("/api/v1/task-transfers", a.taskTransfersRoot)
	mux.HandleFunc("/api/v1/task-transfers/", a.taskTransferByID)
	mux.HandleFunc("/api/v1/task-extractions", a.taskExtractionsRoot)
	mux.HandleFunc("/api/v1/task-extractions/", a.taskExtractionByID)
	mux.HandleFunc("/api/v1/object-rezzes", a.objectRezzesRoot)
	mux.HandleFunc("/api/v1/object-rezzes/", a.objectRezByID)
	mux.HandleFunc("/", a.notFound)
	return withRequestID(withRequestLogging(
		authenticateInternal(mux, options.ServiceToken), options.Logger,
	))
}

func (a *API) provisionedRegionRuntime(w http.ResponseWriter, r *http.Request) {
	if a.regions == nil || a.provisioned == nil {
		writeJSON(w, http.StatusServiceUnavailable, Error{Code: "region_registration_unavailable", Message: "provisioned region registration is unavailable"})
		return
	}
	parts := strings.Split(strings.Trim(strings.TrimPrefix(r.URL.Path, "/api/v1/region-runtime/"), "/"), "/")
	if len(parts) == 0 || strings.TrimSpace(parts[0]) == "" || len(parts[0]) > 128 {
		a.notFound(w, r)
		return
	}
	scheme, accessKey, found := strings.Cut(r.Header.Get("Authorization"), " ")
	provisioned, authenticated := a.provisioned.Authenticate(r.Context(), parts[0], accessKey)
	if !found || !strings.EqualFold(scheme, "Bearer") || !authenticated {
		w.Header().Set("WWW-Authenticate", "Bearer")
		writeJSON(w, http.StatusUnauthorized, Error{Code: "unauthorized_region", Message: "the region UUID or access key is invalid"})
		return
	}
	id := provisioned.ID
	if len(parts) == 1 && r.Method == http.MethodPost {
		var request StartProvisionedRegionRequest
		if !decodeJSON(w, r, &request) {
			return
		}
		lease, ok := validateLease(w, request.LeaseSeconds)
		publicEndpoint := request.PublicEndpoint
		if provisioned.PublicEndpoint != "" {
			publicEndpoint = provisioned.PublicEndpoint
		}
		viewerPort := request.ViewerPort
		if provisioned.ViewerPort != 0 {
			viewerPort = provisioned.ViewerPort
		}
		validation := RegisterRegionRequest{Name: provisioned.Name, GridX: provisioned.MapX, GridY: provisioned.MapY,
			PublicEndpoint: publicEndpoint, ViewerPort: viewerPort}
		if !ok || !validateRegistration(w, validation) {
			return
		}
		region, err := a.regions.RegisterProvisioned(r.Context(), id, regions.Registration{
			Name: provisioned.Name, GridX: provisioned.MapX, GridY: provisioned.MapY,
			PublicEndpoint: publicEndpoint, ViewerPort: viewerPort, LeaseDuration: lease,
		})
		if errors.Is(err, regions.ErrConflict) {
			writeJSON(w, http.StatusConflict, Error{Code: "region_coordinates_in_use", Message: "region coordinates are already leased"})
		} else if err != nil {
			writeJSON(w, http.StatusInternalServerError, Error{Code: "region_store_error", Message: "region registration failed"})
		} else {
			writeJSON(w, http.StatusOK, ProvisionedRegionRuntimeResult{
				Region: region, GridName: a.gridName, GridPublicURL: a.publicURL})
		}
		return
	}
	if len(parts) == 2 && parts[1] == "lease" && r.Method == http.MethodPut {
		var request RenewRegionLeaseRequest
		if !decodeJSON(w, r, &request) {
			return
		}
		lease, ok := validateLease(w, request.LeaseSeconds)
		if !ok {
			return
		}
		region, err := a.regions.RenewProvisioned(r.Context(), id, lease)
		a.writeRegionResult(w, region, err)
		return
	}
	if len(parts) == 1 && r.Method == http.MethodDelete {
		err := a.regions.DeregisterProvisioned(r.Context(), id)
		if err != nil && !errors.Is(err, regions.ErrNotFound) {
			writeJSON(w, http.StatusInternalServerError, Error{Code: "region_store_error", Message: "region deregistration failed"})
		} else {
			w.WriteHeader(http.StatusNoContent)
		}
		return
	}
	a.notFound(w, r)
}

func getOnly(next http.HandlerFunc) http.HandlerFunc {
	return func(w http.ResponseWriter, r *http.Request) {
		if r.Method != http.MethodGet {
			w.Header().Set("Allow", http.MethodGet)
			writeJSON(w, http.StatusMethodNotAllowed, Error{
				Code: "method_not_allowed", Message: "only GET is supported",
			})
			return
		}
		next(w, r)
	}
}

func (a *API) ping(w http.ResponseWriter, _ *http.Request) {
	writeJSON(w, http.StatusOK, Status{Status: "ok"})
}

func (a *API) readiness(w http.ResponseWriter, r *http.Request) {
	if a.ready == nil {
		writeJSON(w, http.StatusServiceUnavailable, Error{
			Code: "database_unconfigured", Message: "database is not configured",
		})
		return
	}

	ctx, cancel := context.WithTimeout(r.Context(), 2*time.Second)
	defer cancel()
	if err := a.ready.PingContext(ctx); err != nil {
		writeJSON(w, http.StatusServiceUnavailable, Error{
			Code: "database_unavailable", Message: "database is unavailable",
		})
		return
	}
	writeJSON(w, http.StatusOK, Status{Status: "ready"})
}

func (a *API) buildVersion(w http.ResponseWriter, _ *http.Request) {
	writeJSON(w, http.StatusOK, Version{
		Service: "grid", Version: a.version, APIVersion: APIVersion,
	})
}

func (a *API) notFound(w http.ResponseWriter, _ *http.Request) {
	writeJSON(w, http.StatusNotFound, Error{Code: "not_found", Message: "route not found"})
}

func (a *API) regionsRoot(w http.ResponseWriter, r *http.Request) {
	if a.regions == nil {
		writeJSON(w, http.StatusServiceUnavailable, Error{Code: "region_store_unavailable", Message: "region storage is unavailable"})
		return
	}
	switch r.Method {
	case http.MethodGet:
		items, err := a.regions.List(r.Context())
		if err != nil {
			writeJSON(w, http.StatusInternalServerError, Error{Code: "region_store_error", Message: "region discovery failed"})
			return
		}
		writeJSON(w, http.StatusOK, RegionList{Regions: items})
	default:
		w.Header().Set("Allow", "GET")
		writeJSON(w, http.StatusMethodNotAllowed, Error{Code: "method_not_allowed", Message: "only GET is supported"})
	}
}

func (a *API) regionByID(w http.ResponseWriter, r *http.Request) {
	if a.regions == nil {
		writeJSON(w, http.StatusServiceUnavailable, Error{Code: "region_store_unavailable", Message: "region storage is unavailable"})
		return
	}
	parts := strings.Split(strings.TrimPrefix(r.URL.Path, "/api/v1/regions/"), "/")
	if len(parts) == 0 || !validUUID(parts[0]) {
		a.notFound(w, r)
		return
	}
	id := parts[0]
	if len(parts) == 1 && r.Method == http.MethodGet {
		region, err := a.regions.Get(r.Context(), id)
		a.writeRegionResult(w, region, err)
		return
	}
	if len(parts) == 2 && parts[1] == "neighbors" {
		if r.Method != http.MethodGet {
			w.Header().Set("Allow", http.MethodGet)
			writeJSON(w, http.StatusMethodNotAllowed, Error{Code: "method_not_allowed", Message: "only GET is supported"})
			return
		}
		a.regionNeighbors(w, r, id)
		return
	}
	if len(parts) == 1 {
		w.Header().Set("Allow", "GET")
		writeJSON(w, http.StatusMethodNotAllowed, Error{Code: "method_not_allowed", Message: "only GET is supported"})
		return
	}
	a.notFound(w, r)
}

func (a *API) regionNeighbors(w http.ResponseWriter, r *http.Request, id string) {
	source, err := a.regions.Get(r.Context(), id)
	if err != nil {
		a.writeRegionResult(w, regions.Region{}, err)
		return
	}
	liveItems, err := a.regions.List(r.Context())
	if err != nil {
		writeJSON(w, http.StatusInternalServerError, Error{Code: "region_store_error", Message: "region discovery failed"})
		return
	}

	directions := []struct {
		name string
		dx   int
		dy   int
	}{
		{name: "north", dy: 1},
		{name: "east", dx: 1},
		{name: "south", dy: -1},
		{name: "west", dx: -1},
	}
	liveByID := make(map[string]regions.Region, len(liveItems))
	for _, item := range liveItems {
		liveByID[item.ID] = item
	}
	topology := make([]RegionTopology, 0, len(liveItems))
	if a.provisioned != nil {
		provisioned, provisionErr := a.provisioned.List(r.Context())
		if provisionErr != nil {
			writeJSON(w, http.StatusInternalServerError, Error{Code: "region_store_error", Message: "region topology discovery failed"})
			return
		}
		for _, item := range provisioned {
			entry := RegionTopology{ID: item.ID, Name: item.Name, GridX: item.MapX, GridY: item.MapY,
				SizeX: 256, SizeY: 256, Maturity: 0, PublicEndpoint: item.PublicEndpoint,
				ViewerPort: item.ViewerPort}
			if live, online := liveByID[item.ID]; online {
				entry.PublicEndpoint, entry.ViewerPort, entry.Online = live.PublicEndpoint, live.ViewerPort, true
			}
			topology = append(topology, entry)
		}
	} else {
		for _, item := range liveItems {
			topology = append(topology, RegionTopology{ID: item.ID, Name: item.Name,
				GridX: item.GridX, GridY: item.GridY, SizeX: 256, SizeY: 256, Maturity: 0,
				PublicEndpoint: item.PublicEndpoint, ViewerPort: item.ViewerPort, Online: true})
		}
	}
	neighbors := make([]RegionNeighbor, 0, len(directions))
	for _, direction := range directions {
		for _, candidate := range topology {
			if candidate.GridX == source.GridX+direction.dx && candidate.GridY == source.GridY+direction.dy {
				neighbors = append(neighbors, RegionNeighbor{Direction: direction.name, Region: candidate})
				break
			}
		}
	}
	writeJSON(w, http.StatusOK, RegionNeighborList{Neighbors: neighbors})
}

func (a *API) writeRegionResult(w http.ResponseWriter, region regions.Region, err error) {
	if errors.Is(err, regions.ErrNotFound) {
		writeJSON(w, http.StatusNotFound, Error{Code: "region_not_found", Message: "region was not found or its lease expired"})
	} else if err != nil {
		writeJSON(w, http.StatusInternalServerError, Error{Code: "region_store_error", Message: "region lookup failed"})
	} else {
		writeJSON(w, http.StatusOK, region)
	}
}

func decodeJSON(w http.ResponseWriter, r *http.Request, target any) bool {
	decoder := json.NewDecoder(http.MaxBytesReader(w, r.Body, 64*1024))
	decoder.DisallowUnknownFields()
	if err := decoder.Decode(target); err != nil {
		writeJSON(w, http.StatusBadRequest, Error{Code: "invalid_json", Message: "request body must be valid JSON"})
		return false
	}
	if err := decoder.Decode(&struct{}{}); !errors.Is(err, io.EOF) {
		writeJSON(w, http.StatusBadRequest, Error{Code: "invalid_json", Message: "request body must contain one JSON object"})
		return false
	}
	return true
}

func validateRegistration(w http.ResponseWriter, request RegisterRegionRequest) bool {
	name := strings.TrimSpace(request.Name)
	endpoint, err := url.ParseRequestURI(request.PublicEndpoint)
	if request.ViewerPort == 0 {
		request.ViewerPort = 42002
	}
	if name == "" || len(name) > 128 || request.GridX < 0 || request.GridY < 0 ||
		request.ViewerPort < 1 || request.ViewerPort > 65535 || err != nil ||
		(endpoint.Scheme != "http" && endpoint.Scheme != "https") || endpoint.Host == "" {
		writeJSON(w, http.StatusBadRequest, Error{Code: "invalid_region", Message: "region name, coordinates, or public endpoint is invalid"})
		return false
	}
	return true
}

func validateLease(w http.ResponseWriter, seconds int) (time.Duration, bool) {
	if seconds == 0 {
		seconds = 60
	}
	if seconds < 10 || seconds > 300 {
		writeJSON(w, http.StatusBadRequest, Error{Code: "invalid_lease", Message: "leaseSeconds must be between 10 and 300"})
		return 0, false
	}
	return time.Duration(seconds) * time.Second, true
}

func validUUID(value string) bool {
	if len(value) != 36 || value[8] != '-' || value[13] != '-' || value[18] != '-' || value[23] != '-' {
		return false
	}
	for index, character := range value {
		if index == 8 || index == 13 || index == 18 || index == 23 {
			continue
		}
		if !((character >= '0' && character <= '9') || (character >= 'a' && character <= 'f') || (character >= 'A' && character <= 'F')) {
			return false
		}
	}
	return true
}

func writeJSON(w http.ResponseWriter, status int, value any) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(status)
	_ = json.NewEncoder(w).Encode(value)
}
