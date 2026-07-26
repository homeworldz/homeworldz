// Package arrival parses and selects grid arrival points: the places a user
// lands when they have no stored location, and the places they are diverted to
// when their stored location cannot be reached (docs/CLIENT2.md, "Default and
// fallback arrival points").
//
// An arrival point is written the way an operator already knows from SLURLs,
// and the way Halcyon's defaultlogins.txt and defaultregions.txt wrote it:
//
//	Welcome/127/127/23
//
// The list is ordered: entry order is priority order, and selection walks the
// list from a starting index until a region accepts, so earlier entries are
// preferred and later ones are the fallback.
package arrival

import (
	"fmt"
	"regexp"
	"strconv"
)

// pointPattern matches Region/x/y/z. The region name is free text without "/"
// or "&" — slightly stricter than Halcyon's [^&]+, which relied on backtracking
// to keep names containing slashes unambiguous.
var pointPattern = regexp.MustCompile(`^([^/&]+)/(\d+)/(\d+)/(\d+)$`)

// Point is one arrival point: a region named by its grid-visible name and a
// position within it, in region-local metres.
type Point struct {
	Region string
	X      int
	Y      int
	Z      int
}

// String renders the point back in its configured Region/x/y/z form.
func (p Point) String() string {
	return fmt.Sprintf("%s/%d/%d/%d", p.Region, p.X, p.Y, p.Z)
}

// ParsePoint parses one Region/x/y/z entry.
func ParsePoint(value string) (Point, error) {
	match := pointPattern.FindStringSubmatch(value)
	if match == nil {
		return Point{}, fmt.Errorf("arrival point %q is not Region/x/y/z", value)
	}
	x, errX := strconv.Atoi(match[2])
	y, errY := strconv.Atoi(match[3])
	z, errZ := strconv.Atoi(match[4])
	if errX != nil || errY != nil || errZ != nil {
		return Point{}, fmt.Errorf("arrival point %q has a non-numeric coordinate", value)
	}
	return Point{Region: match[1], X: x, Y: y, Z: z}, nil
}

// ParseList parses a configured list of arrival points, failing on the first
// invalid entry so a bad configuration is caught at startup rather than at the
// first login that needs it.
func ParseList(values []string) ([]Point, error) {
	points := make([]Point, 0, len(values))
	for _, value := range values {
		point, err := ParsePoint(value)
		if err != nil {
			return nil, err
		}
		points = append(points, point)
	}
	return points, nil
}
