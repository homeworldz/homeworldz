package regions

import "testing"

func TestNewUUID(t *testing.T) {
	first, err := newUUID()
	if err != nil {
		t.Fatal(err)
	}
	second, err := newUUID()
	if err != nil {
		t.Fatal(err)
	}
	if len(first) != 36 || first[14] != '4' || (first[19] != '8' && first[19] != '9' && first[19] != 'a' && first[19] != 'b') {
		t.Fatalf("invalid UUID v4 %q", first)
	}
	if first == second {
		t.Fatal("generated duplicate UUIDs")
	}
}
