package api

import (
	"net/http"
	"time"

	"github.com/homeworldz/homeworldz/grid/internal/webaccount"
)

// SystemRegionSummary counts provisioned regions by derived state.
type SystemRegionSummary struct {
	Total      int `json:"total"`
	Online     int `json:"online"`
	Offline    int `json:"offline"`
	Undeployed int `json:"undeployed"`
}

// SystemSessionSummary counts active viewer presences.
type SystemSessionSummary struct {
	Total int `json:"total"`
}

// SystemRegionStatus is a per-region status row for the system overview.
type SystemRegionStatus struct {
	ID             string     `json:"id"`
	Name           string     `json:"name"`
	Kind           string     `json:"kind"`
	State          string     `json:"state"`
	Sessions       int        `json:"sessions"`
	LeaseExpiresAt *time.Time `json:"leaseExpiresAt,omitempty"`
}

// SystemStatus is the read-only grid overview.
type SystemStatus struct {
	Regions      SystemRegionSummary  `json:"regions"`
	Sessions     SystemSessionSummary `json:"sessions"`
	RegionStatus []SystemRegionStatus `json:"regionStatus"`
}

// systemStatus handles GET /v1/admin/system/status: a read-only overview of
// provisioned regions (by state) and active sessions, aggregated from the
// provisioning, lease, and presence stores.
func (a *API) systemStatus(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		methodNotAllowed(w, http.MethodGet)
		return
	}
	account, ok := a.requireAuth(w, r)
	if !ok || !a.requirePrivilege(w, account, webaccount.PrivSystem) {
		return
	}
	if a.regions == nil {
		writeError(w, http.StatusServiceUnavailable, Error{Code: "region_store_unavailable", Message: "region storage is unavailable"})
		return
	}
	items, err := a.regions.List(r.Context())
	if err != nil {
		a.internalError(w, r, "system status regions", err)
		return
	}

	// Active sessions per region (and total) from presence, when available.
	sessionsByRegion := map[string]int{}
	totalSessions := 0
	if a.presence != nil {
		presences, err := a.presence.List(r.Context())
		if err != nil {
			a.internalError(w, r, "system status presence", err)
			return
		}
		for _, entry := range presences {
			sessionsByRegion[entry.RegionID]++
			totalSessions++
		}
	}

	summary := SystemRegionSummary{Total: len(items)}
	detail := make([]SystemRegionStatus, 0, len(items))
	for _, item := range items {
		managed := a.managedRegionOf(r.Context(), item)
		switch managed.State {
		case regionOnline:
			summary.Online++
		case regionOffline:
			summary.Offline++
		case regionUndeployed:
			summary.Undeployed++
		}
		detail = append(detail, SystemRegionStatus{
			ID: managed.ID, Name: managed.Name, Kind: managed.Kind,
			State: managed.State, Sessions: sessionsByRegion[managed.ID],
			LeaseExpiresAt: managed.LeaseExpiresAt,
		})
	}

	writeJSON(w, http.StatusOK, SystemStatus{
		Regions:      summary,
		Sessions:     SystemSessionSummary{Total: totalSessions},
		RegionStatus: detail,
	})
}
