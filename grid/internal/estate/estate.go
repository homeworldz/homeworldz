// Package estate models Second Life-style estates: named collections of regions
// that share ownership, access-control lists, and region defaults. An estate is
// grid-level state (it spans regions), so it lives in the central store rather
// than in any single region.
package estate

import (
	"context"
	"errors"
	"sort"
	"strings"
	"sync"
)

var (
	ErrNotFound = errors.New("estate not found")
	ErrInvalid  = errors.New("estate is invalid")
)

// Member roles within an estate access list.
const (
	RoleManager      = 0 // estate manager
	RoleAllowedUser  = 1 // explicitly allowed resident
	RoleAllowedGroup = 2 // explicitly allowed group
	RoleBannedUser   = 3 // banned resident
)

// The default estate assigned to a region that has none. Second Life reserves
// estate 1 for the mainland; user estates start at 100.
const (
	DefaultEstateID   = 100
	DefaultEstateName = "My Estate"
	MainlandEstateID  = 1
)

// Estate is the full estate record including its access-control lists.
type Estate struct {
	ID             int      `json:"id"`
	Name           string   `json:"name"`
	OwnerUserID    string   `json:"ownerUserId,omitempty"`
	ParentEstateID int      `json:"parentEstateId"`
	Flags          uint64   `json:"flags"`
	PublicAccess   bool     `json:"publicAccess"`
	SunHour        float64  `json:"sunHour"`
	UseGlobalTime  bool     `json:"useGlobalTime"`
	FixedSun       bool     `json:"fixedSun"`
	BillableFactor float64  `json:"billableFactor"`
	PricePerMeter  int      `json:"pricePerMeter"`
	RedirectGridX  int      `json:"redirectGridX"`
	RedirectGridY  int      `json:"redirectGridY"`
	AbuseEmail     string   `json:"abuseEmail,omitempty"`
	Managers       []string `json:"managers"`
	AllowedUsers   []string `json:"allowedUsers"`
	AllowedGroups  []string `json:"allowedGroups"`
	Bans           []string `json:"bans"`
}

// SettingsUpdate carries the estate scalar/flag fields a caller may change. Nil
// fields are left unchanged; the access lists are managed separately via SetMember.
type SettingsUpdate struct {
	Name           *string
	OwnerUserID    *string
	Flags          *uint64
	PublicAccess   *bool
	SunHour        *float64
	UseGlobalTime  *bool
	FixedSun       *bool
	BillableFactor *float64
	PricePerMeter  *int
	RedirectGridX  *int
	RedirectGridY  *int
	AbuseEmail     *string
}

// Store persists estates and the region->estate association.
type Store interface {
	// ForRegion returns the estate a region belongs to, lazily creating and
	// assigning a default estate owned by defaultOwner when the region has none.
	ForRegion(ctx context.Context, regionID, defaultOwner string) (Estate, error)
	Get(ctx context.Context, id int) (Estate, error)
	UpdateSettings(ctx context.Context, id int, update SettingsUpdate) (Estate, error)
	// SetMember adds (present=true) or removes (present=false) a member with the
	// given role from the estate access lists.
	SetMember(ctx context.Context, id int, memberID string, role int, present bool) (Estate, error)
}

func roleList(estate *Estate, role int) *[]string {
	switch role {
	case RoleManager:
		return &estate.Managers
	case RoleAllowedUser:
		return &estate.AllowedUsers
	case RoleAllowedGroup:
		return &estate.AllowedGroups
	case RoleBannedUser:
		return &estate.Bans
	default:
		return nil
	}
}

func applySettings(estate *Estate, update SettingsUpdate) {
	if update.Name != nil {
		estate.Name = strings.TrimSpace(*update.Name)
	}
	if update.OwnerUserID != nil {
		estate.OwnerUserID = strings.TrimSpace(*update.OwnerUserID)
	}
	if update.Flags != nil {
		estate.Flags = *update.Flags
	}
	if update.PublicAccess != nil {
		estate.PublicAccess = *update.PublicAccess
	}
	if update.SunHour != nil {
		estate.SunHour = *update.SunHour
	}
	if update.UseGlobalTime != nil {
		estate.UseGlobalTime = *update.UseGlobalTime
	}
	if update.FixedSun != nil {
		estate.FixedSun = *update.FixedSun
	}
	if update.BillableFactor != nil {
		estate.BillableFactor = *update.BillableFactor
	}
	if update.PricePerMeter != nil {
		estate.PricePerMeter = *update.PricePerMeter
	}
	if update.RedirectGridX != nil {
		estate.RedirectGridX = *update.RedirectGridX
	}
	if update.RedirectGridY != nil {
		estate.RedirectGridY = *update.RedirectGridY
	}
	if update.AbuseEmail != nil {
		estate.AbuseEmail = strings.TrimSpace(*update.AbuseEmail)
	}
}

// MemoryStore is an in-memory Store for tests and no-database deployments.
type MemoryStore struct {
	mu            sync.Mutex
	byID          map[int]*Estate
	regionEstate  map[string]int
	nextEstateID  int
	defaultOfName map[string]int // owner -> estate id, for reusing one default per owner
}

func NewMemoryStore() *MemoryStore {
	return &MemoryStore{
		byID:          map[int]*Estate{},
		regionEstate:  map[string]int{},
		nextEstateID:  DefaultEstateID,
		defaultOfName: map[string]int{},
	}
}

func clone(estate *Estate) Estate {
	out := *estate
	out.Managers = append([]string(nil), estate.Managers...)
	out.AllowedUsers = append([]string(nil), estate.AllowedUsers...)
	out.AllowedGroups = append([]string(nil), estate.AllowedGroups...)
	out.Bans = append([]string(nil), estate.Bans...)
	return out
}

func (s *MemoryStore) ForRegion(_ context.Context, regionID, defaultOwner string) (Estate, error) {
	if strings.TrimSpace(regionID) == "" {
		return Estate{}, ErrInvalid
	}
	s.mu.Lock()
	defer s.mu.Unlock()
	if id, ok := s.regionEstate[regionID]; ok {
		return clone(s.byID[id]), nil
	}
	// Reuse a single default estate per owner so co-owned regions share one estate.
	id, ok := s.defaultOfName[defaultOwner]
	if !ok {
		id = s.nextEstateID
		s.nextEstateID++
		s.byID[id] = &Estate{ID: id, Name: DefaultEstateName, OwnerUserID: defaultOwner,
			ParentEstateID: MainlandEstateID, PublicAccess: true, UseGlobalTime: true}
		s.defaultOfName[defaultOwner] = id
	}
	s.regionEstate[regionID] = id
	return clone(s.byID[id]), nil
}

func (s *MemoryStore) Get(_ context.Context, id int) (Estate, error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	estate, ok := s.byID[id]
	if !ok {
		return Estate{}, ErrNotFound
	}
	return clone(estate), nil
}

func (s *MemoryStore) UpdateSettings(_ context.Context, id int, update SettingsUpdate) (Estate, error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	estate, ok := s.byID[id]
	if !ok {
		return Estate{}, ErrNotFound
	}
	applySettings(estate, update)
	return clone(estate), nil
}

func (s *MemoryStore) SetMember(_ context.Context, id int, memberID string, role int, present bool) (Estate, error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	estate, ok := s.byID[id]
	if !ok {
		return Estate{}, ErrNotFound
	}
	list := roleList(estate, role)
	if list == nil {
		return Estate{}, ErrInvalid
	}
	filtered := (*list)[:0:0]
	for _, existing := range *list {
		if existing != memberID {
			filtered = append(filtered, existing)
		}
	}
	if present {
		filtered = append(filtered, memberID)
		sort.Strings(filtered)
	}
	*list = filtered
	return clone(estate), nil
}
