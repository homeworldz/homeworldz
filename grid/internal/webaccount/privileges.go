package webaccount

import (
	"errors"
	"regexp"
	"sort"
	"strings"
)

// Named administrative privileges. See api/README.md "Privileges".
const (
	PrivUsers    = "users"
	PrivBans     = "bans"
	PrivRegions  = "regions"
	PrivMap      = "map"
	PrivDeploy   = "deploy"
	PrivUndeploy = "undeploy"
	PrivAdmin    = "admin"
	PrivSuper    = "super"
)

// adminExpansion is the deliberately curated common set that `admin` grants.
// The destructive `undeploy` capability is intentionally excluded and must be
// assigned separately.
var adminExpansion = []string{PrivUsers, PrivBans, PrivRegions, PrivMap, PrivDeploy}

// allNamedPrivileges is every privilege `super` grants; it is also the set of
// privileges an endpoint can require.
var allNamedPrivileges = []string{
	PrivUsers, PrivBans, PrivRegions, PrivMap, PrivDeploy, PrivUndeploy, PrivAdmin, PrivSuper,
}

// ErrInvalidPrivileges is returned when a privilege string violates the
// Privileges schema pattern.
var ErrInvalidPrivileges = errors.New("privileges must be a normalized comma-separated list of names")

var privilegeName = regexp.MustCompile(`^[a-z][a-z0-9_-]*$`)

// NormalizePrivileges validates each name against the schema pattern and
// returns a deduplicated, sorted, comma-separated string. An empty input is
// valid and yields an empty string.
func NormalizePrivileges(privs string) (string, error) {
	trimmed := strings.TrimSpace(privs)
	if trimmed == "" {
		return "", nil
	}
	seen := map[string]struct{}{}
	names := make([]string, 0)
	for _, name := range strings.Split(trimmed, ",") {
		if !privilegeName.MatchString(name) {
			return "", ErrInvalidPrivileges
		}
		if _, exists := seen[name]; exists {
			continue
		}
		seen[name] = struct{}{}
		names = append(names, name)
	}
	sort.Strings(names)
	return strings.Join(names, ","), nil
}

// expand resolves a stored privilege string into the concrete set of granted
// capabilities, applying the admin and super expansions. Unknown names confer
// no authority.
func expand(privs string) map[string]bool {
	granted := map[string]bool{}
	for _, name := range strings.Split(privs, ",") {
		name = strings.TrimSpace(name)
		if name == "" {
			continue
		}
		granted[name] = true
		switch name {
		case PrivSuper:
			for _, p := range allNamedPrivileges {
				granted[p] = true
			}
		case PrivAdmin:
			for _, p := range adminExpansion {
				granted[p] = true
			}
		}
	}
	return granted
}

// HasPrivilege reports whether the stored privilege string authorizes the
// required capability, honoring admin/super expansion.
func HasPrivilege(privs, required string) bool {
	if required == "" {
		return true
	}
	return expand(privs)[required]
}

// IsSuper reports whether the stored privilege string includes super.
func IsSuper(privs string) bool {
	return expand(privs)[PrivSuper]
}
