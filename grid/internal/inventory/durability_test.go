package inventory

import "testing"

// referencesBytes decides which inventory writes the vault has to answer for.
// Getting it wrong is not a small error in either direction: too narrow and an
// item outlives the bytes behind it, too wide and the keeper is asked about an
// id that names something other than an asset and refuses every time.
//
// The calling card case is the second kind and it was live. A card's asset_id
// is the avatar it names — a user id with no bytes anywhere — and Firestorm
// creates the agent's own card on any login where Friends/All does not already
// hold one, so the refusal met every avatar's first login and surfaced as an
// alert naming them.
func TestReferencesBytesExcludesIdsThatAreNotAssets(t *testing.T) {
	const asset = "66c41e39-38f9-f75a-024e-585989bfab73"
	for _, testCase := range []struct {
		name string
		item Item
		want bool
	}{
		{"texture", Item{AssetID: asset, AssetType: 0}, true},
		{"bodypart", Item{AssetID: asset, AssetType: 13}, true},
		{"clothing", Item{AssetID: asset, AssetType: 5}, true},
		// asset_id names an avatar, not bytes.
		{"calling card", Item{AssetID: "efa3f54c-9be7-47c1-b6f3-197d778f32b3", AssetType: 2}, false},
		// asset_id names another inventory item.
		{"link", Item{AssetID: asset, AssetType: 24}, false},
		{"folder link", Item{AssetID: asset, AssetType: 25}, false},
		{"absent asset", Item{AssetID: "", AssetType: 0}, false},
		{"zero asset", Item{AssetID: zeroUUID, AssetType: 0}, false},
	} {
		if got := referencesBytes(testCase.item); got != testCase.want {
			t.Errorf("%s: referencesBytes = %v, want %v", testCase.name, got, testCase.want)
		}
	}
}
