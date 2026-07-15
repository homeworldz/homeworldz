# ADR 0020: Asset Origin And Replication

Status: Accepted

Viewer-facing asset UUIDs are immutable content identities within a HomeWorldz
grid. Registering an existing UUID is idempotent only when its SHA-256 hash,
byte size, and creator UUID match. A conflicting registration is rejected; a
region must allocate a new asset UUID for different bytes or provenance.

Databases created before creator provenance was recorded may replace the null
UUID's “unknown creator” value once with a known creator, but only when the
asset UUID, content hash, and size are unchanged. Known provenance cannot be
replaced.

Asset blobs remain region-local under ADR 0014. The grid stores federation
metadata rather than blob bytes: asset UUID, creator UUID, SHA-256, size, and
one or more stable region asset endpoints. A region registers an origin after
durably storing an authenticated upload and may register itself as a replica
after a verified copy.

Region-to-region lookup and fetch use authenticated internal HTTP APIs. The
initial grid service token is acceptable for the single-grid implementation;
scoped, short-lived fetch authorization must replace it before mutually
untrusted regions are supported. Public viewer capabilities are not federation
credentials.

A receiving region looks up an authorized endpoint, downloads the immutable
bytes, verifies the advertised size and SHA-256, stores the same viewer UUID
and creator provenance locally, and then registers its replica. Hash or
metadata mismatches fail closed. Origin loss does not invalidate an available
verified replica.

Stable asset endpoints are operator-configured region URLs, not ephemeral
lease-registration UUIDs. Production federation requires HTTPS. Optional
central blob fallback remains out of scope for the first implementation.
