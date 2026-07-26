// Mirrors the server privilege model (grid/internal/webaccount/privileges.go).
// The API is authoritative; this mirror gates the admin UI so it only offers
// actions the account can actually perform. Keep the two in sync.

// Named administrative privileges, in a stable display order.
export const NAMED_PRIVILEGES = [
  "users",
  "bans",
  "regions",
  "map",
  "deploy",
  "undeploy",
  "system",
  "admin",
  "super",
];

// `admin` expands to this curated common set. The destructive `undeploy`
// capability is intentionally excluded and must be assigned separately.
const ADMIN_EXPANSION = ["users", "bans", "regions", "map", "deploy"];

// Splits a stored privilege string into its raw, stored names.
export function privilegeList(privs) {
  return String(privs ?? "")
    .split(",")
    .map((name) => name.trim())
    .filter(Boolean);
}

// Resolves a stored privilege string into the concrete set of granted
// capabilities, applying the admin and super expansions. Unknown names confer
// no authority.
function expand(privs) {
  const granted = new Set();
  for (const name of privilegeList(privs)) {
    granted.add(name);
    if (name === "super") {
      for (const priv of NAMED_PRIVILEGES) {
        granted.add(priv);
      }
    } else if (name === "admin") {
      for (const priv of ADMIN_EXPANSION) {
        granted.add(priv);
      }
    }
  }
  return granted;
}

// Reports whether the stored privilege string authorizes the required
// capability, honoring admin/super expansion. An empty requirement is always
// authorized.
export function hasPrivilege(privs, required) {
  if (!required) {
    return true;
  }
  return expand(privs).has(required);
}

// Reports whether the stored privilege string includes super.
export function isSuper(privs) {
  return expand(privs).has("super");
}

// The privileges that grant access to some part of the admin area.
const ADMIN_SURFACE = ["users", "bans", "regions", "map", "deploy", "undeploy", "system"];

// Reports whether the account may reach any admin area, used to decide whether
// to surface the Admin navigation entry at all.
export function hasAnyAdmin(privs) {
  const granted = expand(privs);
  return ADMIN_SURFACE.some((priv) => granted.has(priv));
}

// The full, expanded set of effective privileges as a sorted array, for
// display alongside the stored set.
export function effectivePrivileges(privs) {
  return [...expand(privs)].sort();
}
