package httpapi

import (
	"errors"
	"net/http"
	"strings"

	"github.com/homeworldz/homeworldz/grid/internal/inventory"
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

type prepareTaskExtractionRequest struct {
	ID                  string                  `json:"id"`
	UserID              string                  `json:"userId"`
	RegionID            string                  `json:"regionId"`
	ObjectID            string                  `json:"objectId"`
	SourceTaskItemID    string                  `json:"sourceTaskItemId"`
	DestinationFolderID string                  `json:"destinationFolderId"`
	PersonalItemID      string                  `json:"personalItemId"`
	Item                tasktransferItemRequest `json:"item"`
}

type tasktransferItemRequest struct {
	CreatorUserID       string `json:"creatorUserId"`
	OwnerUserID         string `json:"ownerUserId"`
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
	SaleType            int    `json:"saleType"`
	SalePrice           int    `json:"salePrice"`
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

func (a *API) taskExtractionsRoot(w http.ResponseWriter, r *http.Request) {
	if a.taskTransfers == nil {
		writeJSON(w, http.StatusServiceUnavailable, Error{Code: "task_transfer_unavailable", Message: "task inventory transfer coordination is unavailable"})
		return
	}
	if r.Method == http.MethodGet {
		regionID := r.URL.Query().Get("regionId")
		if !validUUID(regionID) {
			writeJSON(w, http.StatusBadRequest, Error{Code: "invalid_task_extraction", Message: "regionId must be a UUID"})
			return
		}
		values, err := a.taskTransfers.PendingExtractions(r.Context(), regionID)
		if err != nil {
			writeJSON(w, http.StatusInternalServerError, Error{Code: "task_transfer_store_error", Message: "pending task inventory extractions could not be loaded"})
			return
		}
		if values == nil {
			values = []tasktransfer.Extraction{}
		}
		writeJSON(w, http.StatusOK, values)
		return
	}
	if r.Method != http.MethodPost {
		w.Header().Set("Allow", "GET, POST")
		writeJSON(w, http.StatusMethodNotAllowed, Error{Code: "method_not_allowed", Message: "only GET and POST are supported"})
		return
	}
	var request prepareTaskExtractionRequest
	if !decodeJSON(w, r, &request) {
		return
	}
	if !validUUID(request.ID) || !validUUID(request.UserID) || !validUUID(request.RegionID) ||
		!validUUID(request.ObjectID) || !validUUID(request.SourceTaskItemID) ||
		!validUUID(request.DestinationFolderID) || !validUUID(request.PersonalItemID) ||
		!validUUID(request.Item.OwnerUserID) || !validUUID(request.Item.CreatorUserID) ||
		!validUUID(request.Item.AssetID) {
		writeJSON(w, http.StatusBadRequest, Error{Code: "invalid_task_extraction", Message: "task inventory extraction identity is invalid"})
		return
	}
	value, err := a.taskTransfers.PrepareExtraction(r.Context(), tasktransfer.PrepareExtraction{
		ID: request.ID, UserID: request.UserID, RegionID: request.RegionID,
		ObjectID: request.ObjectID, SourceTaskItemID: request.SourceTaskItemID,
		DestinationFolderID: request.DestinationFolderID, PersonalItemID: request.PersonalItemID,
		Item: inventory.Item{CreatorUserID: request.Item.CreatorUserID,
			OwnerUserID: request.Item.OwnerUserID, AssetID: request.Item.AssetID,
			AssetType: request.Item.AssetType, InventoryType: request.Item.InventoryType,
			Name: request.Item.Name, Description: request.Item.Description, Flags: request.Item.Flags,
			BasePermissions:     request.Item.BasePermissions,
			CurrentPermissions:  request.Item.CurrentPermissions,
			EveryonePermissions: request.Item.EveryonePermissions,
			NextPermissions:     request.Item.NextPermissions, SaleType: request.Item.SaleType,
			SalePrice: request.Item.SalePrice}})
	a.writeTaskExtractionResult(w, value, err)
}

func (a *API) taskExtractionByID(w http.ResponseWriter, r *http.Request) {
	if a.taskTransfers == nil {
		writeJSON(w, http.StatusServiceUnavailable, Error{Code: "task_transfer_unavailable", Message: "task inventory transfer coordination is unavailable"})
		return
	}
	parts := strings.Split(strings.Trim(strings.TrimPrefix(r.URL.Path, "/api/v1/task-extractions/"), "/"), "/")
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
	value, err := a.taskTransfers.FinalizeExtraction(r.Context(), parts[0], request.RegionID)
	a.writeTaskExtractionResult(w, value, err)
}

func (a *API) writeTaskExtractionResult(w http.ResponseWriter, value tasktransfer.Extraction, err error) {
	switch {
	case err == nil:
		writeJSON(w, http.StatusOK, value)
	case errors.Is(err, tasktransfer.ErrNotFound):
		writeJSON(w, http.StatusNotFound, Error{Code: "task_extraction_not_found", Message: "task inventory extraction or destination folder was not found"})
	case errors.Is(err, tasktransfer.ErrConflict):
		writeJSON(w, http.StatusConflict, Error{Code: "task_extraction_conflict", Message: "task inventory extraction conflicts with durable state"})
	case errors.Is(err, tasktransfer.ErrInvalid):
		writeJSON(w, http.StatusBadRequest, Error{Code: "invalid_task_extraction", Message: "only owned no-copy task inventory can leave through this transfer"})
	default:
		writeJSON(w, http.StatusInternalServerError, Error{Code: "task_transfer_store_error", Message: "task inventory extraction failed"})
	}
}
