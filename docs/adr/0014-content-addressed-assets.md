# ADR 0014: Content-Addressed Region Assets

Status: Accepted

Region asset bytes are immutable blobs addressed by lowercase SHA-256. Blobs
are sharded under `assets/<first-two-hex>/<remaining-hex>`, while SQLite maps
viewer-facing UUIDs to content hashes and sizes. Multiple viewer UUIDs may map
to one blob without duplicating bytes.

Every viewer-facing asset mapping also records a required creator UUID as
creation provenance. Authenticated uploads use the uploader's agent UUID;
bundled or migrated assets with unknown original provenance use the zero UUID.
The creator is independent of inventory ownership and is not replaced when an
existing mapping is encountered again. Under ADR 0020, an existing viewer UUID
may only be registered idempotently with the same hash, size, and creator;
conflicting content or provenance requires a new UUID.

New blobs use same-directory temporary files and atomic replacement. Reads
recompute SHA-256 and reject missing or corrupted content. Asset mappings are
immutable after creation. Unreferenced-blob collection is deferred until
retention requirements are defined.
