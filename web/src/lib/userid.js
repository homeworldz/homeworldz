// Derives the avatar login id from a display name. The server performs the
// authoritative derivation; this mirror exists only for the live registration
// preview. Keep the two implementations in sync (see api/openapi.yaml UserId).
export function deriveUserid(displayName) {
  return String(displayName ?? "")
    .toLowerCase()
    .replace(/[^a-z0-9'.]+/g, ".") // non-allowed (incl. whitespace) -> period
    .replace(/\.+/g, ".") // collapse consecutive periods
    .replace(/^\.|\.$/g, ""); // trim leading/trailing period
}

export function displayNameWords(displayName) {
  return String(displayName ?? "")
    .trim()
    .split(/\s+/)
    .filter(Boolean);
}

// Returns an error message string when the display name is unusable, or null
// when it is valid.
export function validateDisplayName(displayName) {
  const words = displayNameWords(displayName);
  if (words.length !== 2) {
    return "Enter exactly two words: a first and last name.";
  }
  const userid = deriveUserid(displayName);
  if (userid.length < 3) {
    return "That name is too short to form a login ID.";
  }
  if (userid.length > 32) {
    return "That name is too long; please shorten it.";
  }
  return null;
}
