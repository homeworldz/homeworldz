package httpapi

import (
	"errors"
	"net/http"
	"strings"

	"github.com/homeworldz/homeworldz/grid/internal/tasktransfer"
)

type prepareTaskTransferRequest struct {
	ID           string `json:"id"`
	UserID       string `json:"userId"`
	SourceItemID string `json:"sourceItemId"`
	RegionID     string `json:"regionId"`
	ObjectID     string `json:"objectId"`
	TaskItemID   string `json:"taskItemId"`
}

func (a *API) taskTransfersRoot(w http.ResponseWriter, r *http.Request) {
	if a.taskTransfers == nil {
		writeJSON(w, http.StatusServiceUnavailable, Error{Code: "task_transfer_unavailable", Message: "task inventory transfer coordination is unavailable"})
		return
	}
	if r.Method == http.MethodGet {
		regionID := r.URL.Query().Get("regionId")
		if !validUUID(regionID) {
			writeJSON(w, http.StatusBadRequest, Error{Code: "invalid_task_transfer", Message: "regionId must be a UUID"})
			return
		}
		values, err := a.taskTransfers.Pending(r.Context(), regionID)
		if err != nil {
			writeJSON(w, http.StatusInternalServerError, Error{Code: "task_transfer_store_error", Message: "pending task inventory transfers could not be loaded"})
			return
		}
		if values == nil {
			values = []tasktransfer.Transfer{}
		}
		writeJSON(w, http.StatusOK, values)
		return
	}
	if r.Method != http.MethodPost {
		w.Header().Set("Allow", "GET, POST")
		writeJSON(w, http.StatusMethodNotAllowed, Error{Code: "method_not_allowed", Message: "only GET and POST are supported"})
		return
	}
	var request prepareTaskTransferRequest
	if !decodeJSON(w, r, &request) {
		return
	}
	if !validUUID(request.ID) || !validUUID(request.UserID) ||
		!validUUID(request.SourceItemID) || !validUUID(request.RegionID) ||
		!validUUID(request.ObjectID) || !validUUID(request.TaskItemID) {
		writeJSON(w, http.StatusBadRequest, Error{Code: "invalid_task_transfer", Message: "task inventory transfer identity is invalid"})
		return
	}
	if a.regions != nil {
		if _, err := a.regions.Get(r.Context(), request.RegionID); err != nil {
			writeJSON(w, http.StatusConflict, Error{Code: "task_transfer_region_offline", Message: "the destination Region is not online"})
			return
		}
	}
	value, err := a.taskTransfers.Prepare(r.Context(), tasktransfer.Prepare{
		ID: request.ID, UserID: request.UserID, SourceItemID: request.SourceItemID,
		RegionID: request.RegionID, ObjectID: request.ObjectID, TaskItemID: request.TaskItemID})
	a.writeTaskTransferResult(w, value, err)
}

func (a *API) taskTransferByID(w http.ResponseWriter, r *http.Request) {
	if a.taskTransfers == nil {
		writeJSON(w, http.StatusServiceUnavailable, Error{Code: "task_transfer_unavailable", Message: "task inventory transfer coordination is unavailable"})
		return
	}
	parts := strings.Split(strings.Trim(strings.TrimPrefix(r.URL.Path, "/api/v1/task-transfers/"), "/"), "/")
	if len(parts) != 2 || !validUUID(parts[0]) || parts[1] != "finalize" || r.Method != http.MethodPost {
		a.notFound(w, r)
		return
	}
	var request struct {
		RegionID string `json:"regionId"`
	}
	if !decodeJSON(w, r, &request) || !validUUID(request.RegionID) {
		return
	}
	value, err := a.taskTransfers.Finalize(r.Context(), parts[0], request.RegionID)
	a.writeTaskTransferResult(w, value, err)
}

func (a *API) writeTaskTransferResult(w http.ResponseWriter, value tasktransfer.Transfer, err error) {
	switch {
	case err == nil:
		writeJSON(w, http.StatusOK, value)
	case errors.Is(err, tasktransfer.ErrNotFound):
		writeJSON(w, http.StatusNotFound, Error{Code: "task_transfer_not_found", Message: "task inventory transfer or source item was not found"})
	case errors.Is(err, tasktransfer.ErrConflict):
		writeJSON(w, http.StatusConflict, Error{Code: "task_transfer_conflict", Message: "task inventory transfer conflicts with durable state"})
	case errors.Is(err, tasktransfer.ErrInvalid):
		writeJSON(w, http.StatusBadRequest, Error{Code: "invalid_task_transfer", Message: "only no-copy inventory can enter this transfer"})
	default:
		writeJSON(w, http.StatusInternalServerError, Error{Code: "task_transfer_store_error", Message: "task inventory transfer failed"})
	}
}
