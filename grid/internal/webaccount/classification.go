package webaccount

import (
	"errors"
	"sort"
	"strings"
)

// User kinds are mutually exclusive; every account has exactly one.
const (
	UserKindSystem  = "system"
	UserKindTesting = "testing"
	UserKindDefault = "default"
)

// UserKinds is the ordered set of valid user kinds.
var UserKinds = []string{UserKindSystem, UserKindTesting, UserKindDefault}

// ErrInvalidKind is returned when a kind is not one of the recognized values.
var ErrInvalidKind = errors.New("kind is not recognized")

// ErrInvalidTags is returned when a tag string violates the token pattern.
var ErrInvalidTags = errors.New("tags must be a normalized comma-separated list of tokens")

// ValidUserKind reports whether kind is a recognized user kind.
func ValidUserKind(kind string) bool {
	for _, candidate := range UserKinds {
		if kind == candidate {
			return true
		}
	}
	return false
}

// NormalizeTags validates each token against the tag pattern and returns a
// deduplicated, sorted, comma-separated string. Empty input yields an empty
// string. Tokens share the privilege-name grammar (lowercase, starting with a
// letter, then letters/digits/hyphen/underscore).
func NormalizeTags(tags string) (string, error) {
	trimmed := strings.TrimSpace(tags)
	if trimmed == "" {
		return "", nil
	}
	seen := map[string]struct{}{}
	names := make([]string, 0)
	for _, token := range strings.Split(trimmed, ",") {
		token = strings.TrimSpace(token)
		if !privilegeName.MatchString(token) {
			return "", ErrInvalidTags
		}
		if _, exists := seen[token]; exists {
			continue
		}
		seen[token] = struct{}{}
		names = append(names, token)
	}
	sort.Strings(names)
	return strings.Join(names, ","), nil
}
