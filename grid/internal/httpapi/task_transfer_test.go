package httpapi

import (
	"context"
	"testing"

	"github.com/homeworldz/homeworldz/grid/internal/inventory"
	"github.com/homeworldz/homeworldz/grid/internal/tasktransfer"
)

type memoryTaskTransferStore struct{ value tasktransfer.Transfer }

func (s *memoryTaskTransferStore) Prepare(_ context.Context, input tasktransfer.Prepare) (tasktransfer.Transfer, error) {
	s.value = tasktransfer.Transfer{ID: input.ID, UserID: input.UserID,
		SourceItemID: input.SourceItemID, RegionID: input.RegionID, ObjectID: input.ObjectID,
		TaskItemID: input.TaskItemID, State: tasktransfer.Prepared,
		Item: inventory.Item{ID: input.SourceItemID, Name: "No Copy"}}
	return s.value, nil
}

func (s *memoryTaskTransferStore) Pending(_ context.Context, regionID string) ([]tasktransfer.Transfer, error) {
	if s.value.RegionID == regionID && s.value.State == tasktransfer.Prepared {
		return []tasktransfer.Transfer{s.value}, nil
	}
	return nil, nil
}

func (s *memoryTaskTransferStore) Finalize(_ context.Context, id, regionID string) (tasktransfer.Transfer, error) {
	if s.value.ID != id || s.value.RegionID != regionID {
		return tasktransfer.Transfer{}, tasktransfer.ErrConflict
	}
	s.value.State = tasktransfer.Finalized
	return s.value, nil
}

func (s *memoryTaskTransferStore) PrepareExtraction(_ context.Context, input tasktransfer.PrepareExtraction) (tasktransfer.Extraction, error) {
	return tasktransfer.Extraction{ID: input.ID, UserID: input.UserID, RegionID: input.RegionID,
		ObjectID: input.ObjectID, SourceTaskItemID: input.SourceTaskItemID,
		DestinationFolderID: input.DestinationFolderID, PersonalItemID: input.PersonalItemID,
		Item: input.Item, State: tasktransfer.Prepared}, nil
}

func (s *memoryTaskTransferStore) PendingExtractions(_ context.Context, regionID string) ([]tasktransfer.Extraction, error) {
	return []tasktransfer.Extraction{{ID: "70000000-0000-4000-8000-000000000007",
		RegionID: regionID, State: tasktransfer.Prepared}}, nil
}

func (s *memoryTaskTransferStore) FinalizeExtraction(_ context.Context, id, regionID string) (tasktransfer.Extraction, error) {
	return tasktransfer.Extraction{ID: id, RegionID: regionID, State: tasktransfer.Finalized}, nil
}

func (s *memoryTaskTransferStore) PrepareObjectRez(_ context.Context, input tasktransfer.PrepareObjectRez) (tasktransfer.ObjectRez, error) {
	return tasktransfer.ObjectRez{ID: input.ID, UserID: input.UserID,
		SourceItemID: input.SourceItemID, RegionID: input.RegionID, ObjectID: input.ObjectID,
		Item: inventory.Item{ID: input.SourceItemID, Name: "No Copy Object"}, State: tasktransfer.Prepared}, nil
}

func (s *memoryTaskTransferStore) PendingObjectRezzes(_ context.Context, regionID string) ([]tasktransfer.ObjectRez, error) {
	return []tasktransfer.ObjectRez{{ID: "a0000000-0000-4000-8000-00000000000a",
		RegionID: regionID, State: tasktransfer.Prepared}}, nil
}

func (s *memoryTaskTransferStore) FinalizeObjectRez(_ context.Context, id, regionID string) (tasktransfer.ObjectRez, error) {
	return tasktransfer.ObjectRez{ID: id, RegionID: regionID, State: tasktransfer.Finalized}, nil
}

func (s *memoryTaskTransferStore) RollbackObjectRez(_ context.Context, id, regionID string) (tasktransfer.ObjectRez, error) {
	return tasktransfer.ObjectRez{ID: id, RegionID: regionID, State: tasktransfer.RolledBack}, nil
}

func TestTaskTransferAPI(t *testing.T) {
	store := &memoryTaskTransferStore{}
	handler := New(nil, "test", Options{ServiceToken: "secret", TaskTransfers: store})
	const transferID = "10000000-0000-4000-8000-000000000001"
	const userID = "20000000-0000-4000-8000-000000000002"
	const sourceID = "30000000-0000-4000-8000-000000000003"
	const regionID = "40000000-0000-4000-8000-000000000004"
	const objectID = "50000000-0000-4000-8000-000000000005"
	const taskID = "60000000-0000-4000-8000-000000000006"
	body := `{"id":"` + transferID + `","userId":"` + userID +
		`","sourceItemId":"` + sourceID + `","regionId":"` + regionID +
		`","objectId":"` + objectID + `","taskItemId":"` + taskID + `"}`
	prepared := requestRegion[tasktransfer.Transfer](t, handler, "POST",
		"/api/v1/task-transfers", body, 200)
	if prepared.State != tasktransfer.Prepared || prepared.Item.Name != "No Copy" {
		t.Fatalf("prepared = %#v", prepared)
	}
	pending := requestRegion[[]tasktransfer.Transfer](t, handler, "GET",
		"/api/v1/task-transfers?regionId="+regionID, "", 200)
	if len(pending) != 1 || pending[0].ID != transferID {
		t.Fatalf("pending = %#v", pending)
	}
	finalized := requestRegion[tasktransfer.Transfer](t, handler, "POST",
		"/api/v1/task-transfers/"+transferID+"/finalize",
		`{"regionId":"`+regionID+`"}`, 200)
	if finalized.State != tasktransfer.Finalized {
		t.Fatalf("finalized = %#v", finalized)
	}
}

func TestTaskExtractionAPI(t *testing.T) {
	store := &memoryTaskTransferStore{}
	handler := New(nil, "test", Options{ServiceToken: "secret", TaskTransfers: store})
	const extractionID = "70000000-0000-4000-8000-000000000007"
	const userID = "20000000-0000-4000-8000-000000000002"
	const regionID = "40000000-0000-4000-8000-000000000004"
	const objectID = "50000000-0000-4000-8000-000000000005"
	const taskID = "60000000-0000-4000-8000-000000000006"
	const folderID = "80000000-0000-4000-8000-000000000008"
	const personalID = "90000000-0000-4000-8000-000000000009"
	body := `{"id":"` + extractionID + `","userId":"` + userID +
		`","regionId":"` + regionID + `","objectId":"` + objectID +
		`","sourceTaskItemId":"` + taskID + `","destinationFolderId":"` + folderID +
		`","personalItemId":"` + personalID + `","item":{"creatorUserId":"` + userID +
		`","ownerUserId":"` + userID + `","assetId":"30000000-0000-4000-8000-000000000003",` +
		`"assetType":0,"inventoryType":0,"name":"No Copy","description":"",` +
		`"flags":0,"basePermissions":647168,"currentPermissions":614400,` +
		`"everyonePermissions":0,"nextPermissions":565248,"saleType":0,"salePrice":0}}`
	prepared := requestRegion[tasktransfer.Extraction](t, handler, "POST",
		"/api/v1/task-extractions", body, 200)
	if prepared.State != tasktransfer.Prepared || prepared.Item.Name != "No Copy" {
		t.Fatalf("prepared extraction = %#v", prepared)
	}
	pending := requestRegion[[]tasktransfer.Extraction](t, handler, "GET",
		"/api/v1/task-extractions?regionId="+regionID, "", 200)
	if len(pending) != 1 || pending[0].ID != extractionID {
		t.Fatalf("pending extractions = %#v", pending)
	}
	finalized := requestRegion[tasktransfer.Extraction](t, handler, "POST",
		"/api/v1/task-extractions/"+extractionID+"/finalize",
		`{"regionId":"`+regionID+`"}`, 200)
	if finalized.State != tasktransfer.Finalized {
		t.Fatalf("finalized extraction = %#v", finalized)
	}
}

func TestObjectRezAPI(t *testing.T) {
	store := &memoryTaskTransferStore{}
	handler := New(nil, "test", Options{ServiceToken: "secret", TaskTransfers: store})
	const rezID = "a0000000-0000-4000-8000-00000000000a"
	const userID = "20000000-0000-4000-8000-000000000002"
	const sourceID = "30000000-0000-4000-8000-000000000003"
	const regionID = "40000000-0000-4000-8000-000000000004"
	const objectID = "50000000-0000-4000-8000-000000000005"
	body := `{"id":"` + rezID + `","userId":"` + userID +
		`","sourceItemId":"` + sourceID + `","regionId":"` + regionID +
		`","objectId":"` + objectID + `"}`
	prepared := requestRegion[tasktransfer.ObjectRez](t, handler, "POST",
		"/api/v1/object-rezzes", body, 200)
	if prepared.State != tasktransfer.Prepared || prepared.Item.Name != "No Copy Object" {
		t.Fatalf("prepared object rez = %#v", prepared)
	}
	pending := requestRegion[[]tasktransfer.ObjectRez](t, handler, "GET",
		"/api/v1/object-rezzes?regionId="+regionID, "", 200)
	if len(pending) != 1 || pending[0].ID != rezID {
		t.Fatalf("pending object rezzes = %#v", pending)
	}
	finalized := requestRegion[tasktransfer.ObjectRez](t, handler, "POST",
		"/api/v1/object-rezzes/"+rezID+"/finalize",
		`{"regionId":"`+regionID+`"}`, 200)
	if finalized.State != tasktransfer.Finalized {
		t.Fatalf("finalized object rez = %#v", finalized)
	}
}
