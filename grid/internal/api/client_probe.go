package api

import (
	"net/http"
	"strings"
)

// The compatibility probe (docs/CLIENT2.md, "The compatibility document"): the
// first request the Homeworldz client makes, and the only one before deciding
// whether this grid is usable at all. Everything in it must be universal across
// the grid; per-region facts belong to the session-open response.
//
// clientProtocol is the client-facing protocol version this grid speaks, and
// clientMinimumProtocol the oldest it still accepts. They gate the whole modern
// path: a client compares its declared minimum and proceeds or reports the grid
// incompatible, with nothing negotiated.
const (
	clientProtocol        = 1
	clientMinimumProtocol = 1
)

// Version is the probe document. The fields outside Client match the internal
// tier's /version shape so a monitoring check reads both the same way.
type Version struct {
	Service    string        `json:"service"`
	Version    string        `json:"version"`
	APIVersion string        `json:"apiVersion"`
	Client     ClientSupport `json:"client"`
}

// ClientSupport is what the Homeworldz client reads. Grid and Regions are
// split because only grid-served capabilities are universally guaranteed by
// the software answering this request; region capabilities are guaranteed by
// the registration handshake instead, and only to its protocol floor.
type ClientSupport struct {
	Protocol        int            `json:"protocol"`
	MinimumProtocol int            `json:"minimumProtocol"`
	Grid            GridSupport    `json:"grid"`
	Regions         RegionSupport  `json:"regions"`
	Welcome         *WelcomeRegion `json:"welcome,omitempty"`
}

// GridSupport describes capabilities the grid services themselves serve.
// Channel and ChannelURL are absent until the grid channel exists: a probe
// advertises implemented behavior only.
type GridSupport struct {
	Name       string `json:"name"`
	Channel    string `json:"channel,omitempty"`
	ChannelURL string `json:"channelURL,omitempty"`
}

// RegionSupport describes what every leased region serves. Empty lists are
// deliberate honesty while the region transport and modern asset formats are
// unbuilt, and each entry is added only when the region protocol that
// introduces it becomes the registration requirement.
type RegionSupport struct {
	Transports   []string `json:"transports"`
	AssetFormats []string `json:"assetFormats"`
	MeshedPrims  bool     `json:"meshedPrims"`
}

// WelcomeRegion names the default landing region for a login screen: no
// endpoint, deliberately, so the probe does not become a region directory.
// Coordinates are present when the region resolves to a provisioned record.
type WelcomeRegion struct {
	Name  string `json:"name"`
	GridX *int   `json:"gridX,omitempty"`
	GridY *int   `json:"gridY,omitempty"`
}

// clientVersion serves GET /v1/version.
func (a *API) clientVersion(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		w.Header().Set("Allow", "GET")
		writeError(w, http.StatusMethodNotAllowed, Error{Code: "method_not_allowed", Message: "only GET is supported"})
		return
	}
	if a.rateLimit(w, r) {
		return
	}
	document := Version{
		Service:    "homeworldz-api",
		Version:    a.version,
		APIVersion: "v1",
		Client: ClientSupport{
			Protocol:        clientProtocol,
			MinimumProtocol: clientMinimumProtocol,
			Grid:            GridSupport{Name: a.gridName},
			Regions: RegionSupport{
				Transports:   []string{},
				AssetFormats: []string{},
			},
			Welcome: a.welcomeRegion(r),
		},
	}
	writeJSON(w, http.StatusOK, document)
}

// welcomeRegion derives the probe's welcome field from the first configured
// arrival point. With none configured the field is omitted rather than
// invented: a fabricated default would re-create the undefined items[0]
// landing this setting exists to remove.
func (a *API) welcomeRegion(r *http.Request) *WelcomeRegion {
	if len(a.welcome) == 0 {
		return nil
	}
	point := a.welcome[0]
	welcome := &WelcomeRegion{Name: point.Region}
	if a.regions == nil {
		return welcome
	}
	items, err := a.regions.List(r.Context())
	if err != nil {
		return welcome
	}
	for _, region := range items {
		if strings.EqualFold(region.Name, point.Region) {
			x, y := region.MapX, region.MapY
			welcome.GridX, welcome.GridY = &x, &y
			return welcome
		}
	}
	return welcome
}
