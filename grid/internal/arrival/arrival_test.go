package arrival

import "testing"

func TestParsePoint(t *testing.T) {
	point, err := ParsePoint("Welcome/127/129/23")
	if err != nil {
		t.Fatalf("parse: %v", err)
	}
	if point.Region != "Welcome" || point.X != 127 || point.Y != 129 || point.Z != 23 {
		t.Fatalf("unexpected point: %+v", point)
	}
	if point.String() != "Welcome/127/129/23" {
		t.Fatalf("round trip: %q", point.String())
	}
}

func TestParsePointAcceptsSpacedNames(t *testing.T) {
	point, err := ParsePoint("Welcome Region/1/2/3")
	if err != nil || point.Region != "Welcome Region" {
		t.Fatalf("spaced name: %+v %v", point, err)
	}
}

func TestParsePointRejectsMalformedEntries(t *testing.T) {
	for _, value := range []string{
		"", "Welcome", "Welcome/1/2", "Welcome/1/2/3/4", "/1/2/3",
		"Welcome/x/2/3", "Welcome/1/2/-3", "Welcome/1.5/2/3",
	} {
		if _, err := ParsePoint(value); err == nil {
			t.Fatalf("expected error for %q", value)
		}
	}
}

func TestParseListFailsFast(t *testing.T) {
	points, err := ParseList([]string{"Welcome/127/127/23", "Welcome/129/129/23"})
	if err != nil || len(points) != 2 {
		t.Fatalf("list: %v %v", points, err)
	}
	if _, err := ParseList([]string{"Welcome/127/127/23", "broken"}); err == nil {
		t.Fatal("expected error for invalid second entry")
	}
}
