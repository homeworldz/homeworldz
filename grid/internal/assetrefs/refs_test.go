package assetrefs

import (
	"encoding/hex"
	"reflect"
	"testing"
)

// buildTextureEntry packs a viewer TextureEntry texture section: default
// texture, then one exception for faces given by bits.
func buildTextureEntry(defaultID, exceptionID string, bits byte) string {
	raw, _ := hex.DecodeString(defaultID)
	entry := append([]byte{}, raw...)
	entry = append(entry, bits) // single-byte face bitfield, high bit clear
	raw, _ = hex.DecodeString(exceptionID)
	entry = append(entry, raw...)
	entry = append(entry, 0) // terminator
	return hex.EncodeToString(entry)
}

func TestObjectReferences(t *testing.T) {
	// The user's own example: a box whose faces wear two textures and whose
	// contents hold a script and a texture — four references from one prim.
	entry := buildTextureEntry(
		"11111111222233334444555555555555",
		"aaaaaaaabbbbccccdddd111111111111", 0x02)
	object := `{"format":"homeworldz-object-v1","name":"box","textureEntry":"` + entry + `",` +
		`"taskInventory":[` +
		`{"itemId":"i1","assetId":"99999999-8888-4777-8666-555555555555","assetType":10},` +
		`{"itemId":"i2","assetId":"77777777-6666-4555-8444-333333333333","assetType":0}]}`
	want := []Reference{
		{ID: "11111111-2222-3333-4444-555555555555", Type: TypeTexture},
		{ID: "aaaaaaaa-bbbb-cccc-dddd-111111111111", Type: TypeTexture},
		{ID: "99999999-8888-4777-8666-555555555555", Type: 10},
		{ID: "77777777-6666-4555-8444-333333333333", Type: TypeTexture},
	}
	if got := Gather(TypeObject, []byte(object)); !reflect.DeepEqual(got, want) {
		t.Fatalf("object references = %#v", got)
	}
}

func TestLinksetReferencesSpanParts(t *testing.T) {
	// Two linked prims: references from every part count, duplicates collapse.
	entry := buildTextureEntry(
		"11111111222233334444555555555555",
		"aaaaaaaabbbbccccdddd111111111111", 0x01)
	part := `{"format":"homeworldz-object-v1","textureEntry":"` + entry + `",` +
		`"taskInventory":[{"assetId":"99999999-8888-4777-8666-555555555555","assetType":6}]}`
	linkset := `{"format":"homeworldz-linkset-v1","parts":[` + part + `,` + part + `]}`
	got := Gather(TypeObject, []byte(linkset))
	want := []Reference{
		{ID: "11111111-2222-3333-4444-555555555555", Type: TypeTexture},
		{ID: "aaaaaaaa-bbbb-cccc-dddd-111111111111", Type: TypeTexture},
		{ID: "99999999-8888-4777-8666-555555555555", Type: TypeObject},
	}
	if !reflect.DeepEqual(got, want) {
		t.Fatalf("linkset references = %#v", got)
	}
}

func TestObjectReferencesIgnoreZeroAndMalformed(t *testing.T) {
	object := `{"format":"homeworldz-object-v1","textureEntry":"zz",` +
		`"taskInventory":[{"assetId":"00000000-0000-0000-0000-000000000000","assetType":0},` +
		`{"assetId":"not-a-uuid","assetType":0}]}`
	if got := Gather(TypeObject, []byte(object)); got != nil {
		t.Fatalf("references = %#v, want none", got)
	}
	if got := Gather(TypeObject, []byte(`{"format":"something-else"}`)); got != nil {
		t.Fatalf("foreign format references = %#v, want none", got)
	}
}

func TestWearableReferences(t *testing.T) {
	wearable := "LLWearable version 22\nJim Shirt\n\n\tpermissions 0\n\t{\n\t}\n" +
		"type 4\nparameters 2\n781 .5\n150 0\n" +
		"textures 2\n5 aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee\n" +
		"7 11111111-2222-4333-8444-555555555555\n"
	want := []Reference{
		{ID: "aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee", Type: TypeTexture},
		{ID: "11111111-2222-4333-8444-555555555555", Type: TypeTexture},
	}
	if got := Gather(TypeClothing, []byte(wearable)); !reflect.DeepEqual(got, want) {
		t.Fatalf("wearable references = %#v", got)
	}
	if got := Gather(TypeBodypart, []byte("not a wearable")); got != nil {
		t.Fatalf("non-wearable references = %#v", got)
	}
}

func TestGestureReferences(t *testing.T) {
	// version, key, mask, trigger, replace, count, then steps. The chat step's
	// text is a lone "0" — exactly what a scanning parser would misread as an
	// animation step.
	gesture := "2\n0\n0\nhello\n\n4\n" +
		"0\nwave\naaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee\n1\n" +
		"2\n0\n0\n" +
		"1\nchime\n11111111-2222-4333-8444-555555555555\n0\n" +
		"3\n1.5\n0\n"
	want := []Reference{
		{ID: "aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee", Type: TypeAnimation},
		{ID: "11111111-2222-4333-8444-555555555555", Type: TypeSound},
	}
	if got := Gather(TypeGesture, []byte(gesture)); !reflect.DeepEqual(got, want) {
		t.Fatalf("gesture references = %#v", got)
	}
}

func TestNotecardReferences(t *testing.T) {
	notecard := "Linden text version 2\n{\nLLEmbeddedItems version 1\n{\ncount 1\n" +
		"{\next char index 0\ninv_item\t0\n{\n" +
		"\titem_id\t33333333-4444-4555-8666-777777777777\n" +
		"\tasset_id\taaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee\n" +
		"\ttype\ttexture\n\tinv_type\ttexture\n}\n}\n}\n" +
		"Text length 52\n" +
		"body text mentioning asset_id 11111111-2222-4333-8444-555555555555\n}\n"
	want := []Reference{{ID: "aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee", Type: TypeTexture}}
	if got := Gather(TypeNotecard, []byte(notecard)); !reflect.DeepEqual(got, want) {
		t.Fatalf("notecard references = %#v", got)
	}
}

func TestNonBearingTypesYieldNothing(t *testing.T) {
	for _, assetType := range []int{TypeTexture, TypeSound, 10, TypeAnimation, TypeUnknown} {
		if Bearing(assetType) {
			t.Fatalf("type %d reported bearing", assetType)
		}
		if got := Gather(assetType, []byte("anything")); got != nil {
			t.Fatalf("type %d references = %#v", assetType, got)
		}
	}
	for _, assetType := range []int{TypeObject, TypeClothing, TypeBodypart, TypeGesture, TypeNotecard} {
		if !Bearing(assetType) {
			t.Fatalf("type %d not reported bearing", assetType)
		}
	}
}
