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
