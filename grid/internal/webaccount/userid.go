package webaccount

import (
	"errors"
	"regexp"
	"strings"
)

// userid length bounds mirror the UserId schema in api/openapi.yaml.
const (
	minUseridLength = 3
	maxUseridLength = 32
)

var (
	// ErrInvalidDisplayName is returned when a display name cannot yield a
	// usable userid.
	ErrInvalidDisplayName = errors.New("display name must be two words and yield a 3-32 character userid")

	useridDisallowed = regexp.MustCompile(`[^a-z0-9'.]+`)
	useridCollapse   = regexp.MustCompile(`\.+`)
)

// DeriveUserid performs the authoritative derivation of an avatar login id from
// a display name. It mirrors deriveUserid in the website's src/lib/userid.js:
// lowercase, replace every run of non-[a-z0-9'.] characters with a single
// period, collapse consecutive periods, and trim leading/trailing periods.
func DeriveUserid(displayName string) string {
	lowered := strings.ToLower(displayName)
	replaced := useridDisallowed.ReplaceAllString(lowered, ".")
	collapsed := useridCollapse.ReplaceAllString(replaced, ".")
	return strings.Trim(collapsed, ".")
}

// ValidateDisplayName enforces the two-word rule and the derived userid length
// bounds, returning the derived userid on success.
func ValidateDisplayName(displayName string) (string, error) {
	words := strings.Fields(displayName)
	if len(words) != 2 {
		return "", ErrInvalidDisplayName
	}
	userid := DeriveUserid(displayName)
	if len(userid) < minUseridLength || len(userid) > maxUseridLength {
		return "", ErrInvalidDisplayName
	}
	return userid, nil
}
