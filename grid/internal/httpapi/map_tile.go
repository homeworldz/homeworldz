package httpapi

import (
	_ "embed"
	"net/http"
	"strconv"
	"strings"
)

//go:embed assets/default-map.jpg
var defaultMapTile []byte

func (a *API) mapTile(w http.ResponseWriter, r *http.Request) {
	if a.regions == nil {
		a.notFound(w, r)
		return
	}
	name := strings.TrimPrefix(r.URL.Path, "/map/")
	const prefix = "map-1-"
	const suffix = "-objects.jpg"
	if !strings.HasPrefix(name, prefix) || !strings.HasSuffix(name, suffix) {
		a.notFound(w, r)
		return
	}
	coordinates := strings.Split(strings.TrimSuffix(strings.TrimPrefix(name, prefix), suffix), "-")
	if len(coordinates) != 2 {
		a.notFound(w, r)
		return
	}
	x, xErr := strconv.Atoi(coordinates[0])
	y, yErr := strconv.Atoi(coordinates[1])
	if xErr != nil || yErr != nil || x < 0 || x > 65535 || y < 0 || y > 65535 {
		a.notFound(w, r)
		return
	}
	regions, err := a.regions.List(r.Context())
	if err != nil {
		writeJSON(w, http.StatusInternalServerError, Error{Code: "region_store_error", Message: "map tile lookup failed"})
		return
	}
	for _, region := range regions {
		if region.GridX != x || region.GridY != y {
			continue
		}
		w.Header().Set("Content-Type", "image/jpeg")
		w.Header().Set("Cache-Control", "public, max-age=60")
		w.WriteHeader(http.StatusOK)
		_, _ = w.Write(defaultMapTile)
		return
	}
	a.notFound(w, r)
}
