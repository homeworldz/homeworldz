package transit

import "testing"

func TestSamePrepareRequiresImmutablePayload(t *testing.T) {
	input := Prepare{
		ID: "transit", AgentID: "agent", SessionID: "session",
		SourceRegionID: "source", DestinationRegionID: "destination",
		Position: Vector3{X: 10, Y: 20, Z: 30}, LookAt: Vector3{X: 1}, Flying: true,
	}
	value := Transit{
		ID: input.ID, AgentID: input.AgentID, SessionID: input.SessionID,
		SourceRegionID: input.SourceRegionID, DestinationRegionID: input.DestinationRegionID,
		Position: input.Position, LookAt: input.LookAt, Flying: input.Flying, State: Prepared,
	}
	if !samePrepare(value, input) {
		t.Fatal("identical retry was not idempotent")
	}
	input.Position.Z++
	if samePrepare(value, input) {
		t.Fatal("changed arrival state was accepted as the same transit")
	}
}
