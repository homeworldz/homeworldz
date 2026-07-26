// Classification vocabulary, mirroring the server (webaccount.UserKinds and the
// api region kinds / sizes). The API is authoritative; these drive the editor
// options and display.

export const USER_KINDS = ["system", "testing", "default"];
// Region kind is provenance only: grid = operator-provided, user = resident.
// Essential/protected status is a tag ("system"), not a kind.
export const REGION_KINDS = ["grid", "user"];

// Suggested tags surfaced as quick toggles; tags are open-ended beyond these.
export const KNOWN_USER_TAGS = ["admin"];
export const KNOWN_REGION_TAGS = ["system", "ocean"];

// Supported region footprints. Each unit is 256 m of edge; regions are square.
export const REGION_SIZES = [
  { units: 1, edge: 256 },
  { units: 2, edge: 512 },
  { units: 4, edge: 1024 },
];

// Human label for a region size in units, as a square edge length.
export function regionSizeLabel(units) {
  const match = REGION_SIZES.find((size) => size.units === units);
  return match ? `${match.edge} m` : `${units}×`;
}

// Splits a normalized comma-separated tag string into tokens.
export function tagList(tags) {
  return String(tags ?? "")
    .split(",")
    .map((token) => token.trim())
    .filter(Boolean);
}

// A tag/kind token is lowercase, starts with a letter, then letters, digits,
// hyphen, or underscore — matching the server's normalization grammar.
export function isValidTagToken(token) {
  return /^[a-z][a-z0-9_-]*$/.test(token);
}
