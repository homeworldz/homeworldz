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
	"context"
	"errors"
	"fmt"
	"math/rand"
	"regexp"
	"strconv"
	"strings"

	"github.com/homeworldz/server/grid/internal/regions"
)

// ErrNoDestination reports that no leased region satisfies the request.
var ErrNoDestination = errors.New("no online region matches the destination")

// RegionLister is the one store method resolution needs; *regions.PostgresStore
// satisfies it, and narrowed store views can too.
type RegionLister interface {
	List(context.Context) ([]regions.Region, error)
}

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

// Destination is a selected region together with the position within it, when
// the selection carries one. Position is nil when the caller's stored location
// supplies the position instead.
type Destination struct {
	Region   regions.Region
	Position *Point
}

// Resolve selects the region a session opens in.
//
// preferredRegionID (the user's last or home region) wins when it is leased.
// When it is not — a new arrival, or a stored region that is offline — the
// walk-until-accepted selection over the arrival list runs: starting at a
// random index so arrivals spread across the configured points, each entry is
// tried in order until one names a leased region. Halcyon walked its list from
// the top and got distribution only from failures; the random start supplies
// it statelessly (docs/CLIENT2.md, "Default and fallback arrival points").
func Resolve(ctx context.Context, store RegionLister, preferredRegionID string, points []Point) (Destination, error) {
	items, err := store.List(ctx)
	if err != nil || len(items) == 0 {
		return Destination{}, ErrNoDestination
	}
	if preferredRegionID != "" {
		for _, region := range items {
			if region.ID == preferredRegionID {
				return Destination{Region: region}, nil
			}
		}
	}
	return selectPoint(items, points)
}

// ResolveNamed selects a specific region by name, for an explicitly requested
// destination. An absent region is an error rather than a diversion: the user
// asked for somewhere in particular, and quietly landing them elsewhere would
// misreport the cause.
func ResolveNamed(ctx context.Context, store RegionLister, name string) (Destination, error) {
	items, err := store.List(ctx)
	if err != nil {
		return Destination{}, ErrNoDestination
	}
	trimmed := strings.TrimSpace(name)
	for _, region := range items {
		if strings.EqualFold(region.Name, trimmed) {
			return Destination{Region: region}, nil
		}
	}
	return Destination{}, ErrNoDestination
}

func selectPoint(items []regions.Region, points []Point) (Destination, error) {
	if len(points) == 0 {
		return Destination{}, ErrNoDestination
	}
	start := rand.Intn(len(points))
	for offset := range points {
		point := points[(start+offset)%len(points)]
		for _, region := range items {
			if strings.EqualFold(region.Name, point.Region) {
				selected := point
				return Destination{Region: region, Position: &selected}, nil
			}
		}
	}
	return Destination{}, ErrNoDestination
}
