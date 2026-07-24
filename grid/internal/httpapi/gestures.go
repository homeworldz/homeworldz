package httpapi

import (
	"net/http"
	"strings"

	"github.com/homeworldz/homeworldz/grid/internal/gestures"
)

type gestureRequest struct {
	ItemID  string `json:"itemId"`
	AssetID string `json:"assetId"`
	Active  bool   `json:"active"`
}

// gesturesByUser manages a user's active-gesture set. The Region calls PUT with
// {itemId, assetId, active} on ActivateGestures/DeactivateGestures; the login
// flow reads the set with GET to seed the viewer's "gestures" list.
func (a *API) gesturesByUser(w http.ResponseWriter, r *http.Request) {
	if a.gestures == nil {
		writeJSON(w, http.StatusServiceUnavailable, Error{
			Code: "gesture_store_unavailable", Message: "gesture storage is unavailable"})
		return
	}
	userID := strings.TrimPrefix(r.URL.Path, "/api/v1/gestures/")
	if !validUUID(userID) {
		a.notFound(w, r)
		return
	}
	if r.Method == http.MethodGet {
		items, err := a.gestures.ListActive(r.Context(), userID)
		if err != nil {
			writeJSON(w, http.StatusInternalServerError, Error{
				Code: "gesture_store_error", Message: "gesture lookup failed"})
			return
		}
		if items == nil {
			items = []gestures.Gesture{}
		}
		writeJSON(w, http.StatusOK, items)
		return
	}
	if r.Method != http.MethodPut {
		w.Header().Set("Allow", "GET, PUT")
		writeJSON(w, http.StatusMethodNotAllowed, Error{
			Code: "method_not_allowed", Message: "only GET and PUT are supported"})
		return
	}
	var request gestureRequest
	if !decodeJSON(w, r, &request) {
		return
	}
	if !validUUID(request.ItemID) || (request.Active && !validUUID(request.AssetID)) {
		writeJSON(w, http.StatusBadRequest, Error{
			Code: "invalid_gesture", Message: "itemId (and assetId when active) must be valid UUIDs"})
		return
	}
	var err error
	if request.Active {
		err = a.gestures.Activate(r.Context(), userID, request.ItemID, request.AssetID)
	} else {
		err = a.gestures.Deactivate(r.Context(), userID, request.ItemID)
	}
	if err != nil {
		writeJSON(w, http.StatusInternalServerError, Error{
			Code: "gesture_store_error", Message: "gesture update failed"})
		return
	}
	w.WriteHeader(http.StatusNoContent)
}
