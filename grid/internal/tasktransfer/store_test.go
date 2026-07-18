package tasktransfer

import (
	"testing"

	"github.com/homeworldz/homeworldz/grid/internal/inventory"
)

func TestSameExtractionRequiresStableCustodyIdentities(t *testing.T) {
	input := PrepareExtraction{
		ID: "extraction", UserID: "user", RegionID: "region", ObjectID: "object",
		SourceTaskItemID: "task", DestinationFolderID: "folder", PersonalItemID: "personal",
		Item: inventory.Item{AssetID: "asset"},
	}
	value := Extraction{
		ID: input.ID, UserID: input.UserID, RegionID: input.RegionID, ObjectID: input.ObjectID,
		SourceTaskItemID: input.SourceTaskItemID, DestinationFolderID: input.DestinationFolderID,
		PersonalItemID: input.PersonalItemID,
	}
	if !sameExtraction(value, input) {
		t.Fatal("identical extraction retry was not idempotent")
	}
	input.PersonalItemID = "different"
	if sameExtraction(value, input) {
		t.Fatal("changed personal item identity was accepted as the same extraction")
	}
}
