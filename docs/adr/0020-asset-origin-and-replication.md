# ADR 0020: Asset Origin And Replication

Status: Accepted

ADR 0026 supersedes the durability role of peer replication described here:
the grid vault durably stores all inventory-referenced asset bytes, and
region-to-region fetch is an optimization. The identity, verification, and
registration mechanics below remain authoritative.

ADR 0027 relocates the registry from asset-keyed to blob-keyed: serving
locations and origin/replica registration attach to a grid-assigned `blob_id`.
The immutability guarantee below — a UUID never remaps to different content or
creator — is carried by the immutable `asset_id → blob_id` binding, not by
comparing content hashes at registration. The SHA-256 named below is retained
only as an integrity checksum for verifying bytes fetched across a trust
boundary — core to HomeWorldz, where mostly-untrusted users run their own
regions — never as identity and not on routine local reads. Immutable
provenance is unchanged. Read the hash references below in that light.

Viewer-facing asset UUIDs are immutable content identities within a HomeWorldz
grid. Registering an existing UUID is idempotent only when its content and
creator are unchanged — under ADR 0027, the same immutable `asset_id → blob_id`
binding and creator. A conflicting registration is rejected; a region must
allocate a new asset UUID for different bytes or provenance.

Databases created before creator provenance was recorded may replace the null
UUID's “unknown creator” value once with a known creator, but only when the
asset UUID, its bound content, and size are unchanged. Known provenance cannot
be replaced.

Asset blobs remain region-local under ADR 0014. The grid stores federation
metadata rather than blob bytes: asset UUID, creator UUID, the content's
integrity checksum, size, and one or more stable region asset endpoints (the
locations relocate to the blob under ADR 0027). A region registers an origin
after durably storing an authenticated upload and may register itself as a
replica after a verified copy.

Region-to-region lookup and fetch use authenticated internal HTTP APIs. The
initial single shared grid service token is an interim measure for the
single-operator deployment; ADR 0028 replaces it with per-owner federation
tokens — one per owner, scoped to that owner's regions — as HomeWorldz moves
toward its goal of mostly-untrusted, user-run regions. Public viewer
capabilities are not federation credentials.

A receiving region looks up an authorized endpoint, downloads the immutable
bytes, verifies the advertised size and SHA-256, stores the same viewer UUID
and creator provenance locally, and then registers its replica. Hash or
metadata mismatches fail closed. Origin loss does not invalidate an available
verified replica.

Stable asset endpoints are operator-configured region URLs, not ephemeral
lease-registration UUIDs. Production federation requires HTTPS. Optional
central blob fallback remains out of scope for the first implementation.
