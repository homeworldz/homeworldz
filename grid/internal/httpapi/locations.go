package httpapi

import (
	"errors"
	"math"
	"net/http"
	"strings"

	"github.com/homeworldz/homeworldz/grid/internal/locations"
)

func (a *API) locationByUser(w http.ResponseWriter, r *http.Request) {
	if a.locations == nil {
		writeJSON(w, http.StatusServiceUnavailable, Error{
			Code: "location_store_unavailable", Message: "last-location storage is unavailable"})
		return
	}
	userID := strings.TrimPrefix(r.URL.Path, "/api/v1/locations/")
	if !validUUID(userID) {
		a.notFound(w, r)
		return
	}
	if r.Method != http.MethodPut {
		w.Header().Set("Allow", http.MethodPut)
		writeJSON(w, http.StatusMethodNotAllowed, Error{
			Code: "method_not_allowed", Message: "only PUT is supported"})
		return
	}
	var request UpdateLocationRequest
	if !decodeJSON(w, r, &request) {
		return
	}
	finite := func(value float32) bool { return !float32NaNOrInf(value) }
	if !validUUID(request.RegionID) ||
		!finite(request.Position.X) || !finite(request.Position.Y) || !finite(request.Position.Z) ||
		!finite(request.LookAt.X) || !finite(request.LookAt.Y) || !finite(request.LookAt.Z) ||
		request.Position.X < 0 || request.Position.X > 256 ||
		request.Position.Y < 0 || request.Position.Y > 256 ||
		math.Hypot(float64(request.LookAt.X), float64(request.LookAt.Y)) < 0.001 {
		writeJSON(w, http.StatusBadRequest, Error{
			Code: "invalid_location", Message: "regionId, position, and lookAt must describe a valid Region location"})
		return
	}
	value, err := a.locations.Update(r.Context(), locations.Location{
		UserID: userID, RegionID: request.RegionID,
		Position: [3]float32{request.Position.X, request.Position.Y, request.Position.Z},
		LookAt:   [3]float32{request.LookAt.X, request.LookAt.Y, request.LookAt.Z},
		Flying:   request.Flying,
	})
	if errors.Is(err, locations.ErrNotFound) {
		writeJSON(w, http.StatusNotFound, Error{
			Code: "location_subject_not_found", Message: "user or Region was not found"})
	} else if err != nil {
		writeJSON(w, http.StatusInternalServerError, Error{
			Code: "location_store_error", Message: "last-location update failed"})
	} else {
		writeJSON(w, http.StatusOK, value)
	}
}
