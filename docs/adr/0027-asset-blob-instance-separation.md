# ADR 0027: Asset, Blob, and Instance Separation

Status: Accepted

HomeWorldz models stored content in three distinct layers — blob, asset, and
instance. Earlier ADRs conflated the first two: a single record carried the
bytes' identity, the creator, and the serving locations together (ADR 0014's
content-addressed blob, ADR 0020's `asset_metadata`). That conflation cannot
express per-creator policy over shared bytes, and it ties byte identity to a
content hash. This ADR separates the layers and makes byte identity a
grid-assigned identifier rather than a hash.

## The three layers

**Blob** — raw immutable bytes. A blob has a grid-assigned `blob_id`, a byte
length, an integrity checksum, and one or more storage locations. The `blob_id`
is a surrogate identifier, **not** a content hash: it is the stable, indirect
reference every higher layer uses to name bytes. Byte-identical content may
exist as more than one blob; nothing in the design requires otherwise (see
*Deduplication*).

**Asset** — a named unit of content with provenance. An asset has a
viewer-facing `asset_id` (UUID), references exactly one blob by `blob_id`, and
carries creator identity, creation and provenance history, and creator-level
options such as whether the content may be exported from the grid. The asset is
**per-creator**: the same bytes uploaded independently by two creators are two
assets that may reference the same blob but are otherwise unrelated, with
independent provenance and independent export policy. An asset's creator and
provenance are immutable. Its `blob_id` is stable except through the
deduplication service, which may only repoint it to another blob holding
byte-identical content.

**Instance** — an owned reference to an asset. Inventory items and rezzed scene
objects are instances. An instance carries its owner and the standard
SL-compatible permission masks (base, owner, group, everyone, next-owner) and
references exactly one asset. **All day-to-day permission state lives here** and
is identical to Second Life semantics; assets and blobs never carry permission
masks.

The cardinalities are: many instances → one asset → one blob.

| Layer | Key | Deduplicated? | Carries |
| --- | --- | --- | --- |
| Blob | `blob_id` (surrogate) | Optionally (see below) | bytes, length, integrity checksum, locations |
| Asset | `asset_id` (UUID) | No | creator, provenance, export option, → one `blob_id` |
| Instance | item / object id | No | owner, SL permission masks, → one `asset_id` |

## Why the layers are separated

Bytes are shared; policy is not. Consider creator A's asset marked exportable
and creator B independently uploading byte-identical content marked
not-exportable. They can share one blob, but export policy must differ — so
policy cannot hang on the blob. The same is true of provenance: a blob has no
creator, an asset does. Any per-creator metadata over shared bytes forces
blob ≠ asset.

Permissions are per-instance, which a transfer chain makes concrete. A gives a
full-perm item to B, B to C; C sets next-owner to no-transfer and gives it to
D:

| Instance | Owner | Permissions | Asset | Blob |
| --- | --- | --- | --- | --- |
| A's | A | copy+mod+transfer | asset X (creator A) | blob H |
| B's | B | copy+mod+transfer | asset X | blob H |
| C's | C | copy+mod+transfer, next-owner = no-transfer | asset X | blob H |
| D's | D | no-transfer | asset X | blob H |

Each give creates a new instance referencing the same asset X; because A/B/C's
instances are copy-enabled, they keep their copies and can keep handing out
more. Permissions ratchet down per transfer (the recipient's base mask folds to
the giver's next-owner grant and can never rise again), so D cannot pass it on.
Throughout, **asset X is invariant** — creator A, unchanged provenance,
unchanged export option — regardless of who owns a copy or how restricted their
instance is, and **blob H is one set of bytes** shared by every instance.

## `blob_id`, not content addressing

Byte identity is the surrogate `blob_id`, assigned by the grid at blob
creation. A surrogate key cannot collide, so no correctness property depends on
the strength of any content hash. This supersedes ADR 0014's use of a content
hash as the blob's identity; see that ADR's amendment note.

The indirection asset → `blob_id` → bytes is what makes the rest of this design
cheap: a blob can be replaced under an asset long after creation without
touching the asset or any instance, because they name the blob indirectly.

## Integrity checksum

Each blob carries a checksum recorded at ingest, as **metadata, not identity**.
Its one load-bearing purpose is **verifying bytes fetched across a trust
boundary**. HomeWorldz is built for mostly-untrusted users to run their own
regions from home or a VPS, so a region serving a blob is not trusted: a
fetching region compares the received bytes against the checksum the grid
recorded and accepts them without trusting the sender (ADR 0020 already does
this on region-to-region fetch). That is a security property core to the
project, not a future add-on.

It is deliberately *not* used to verify routine local reads. A region reading
back its own store is merely trusting storage to return the bytes it stored —
not this system's job any more than it is for the grid database, scene
snapshots, or configuration, none of which are checksum-verified on read, and
modern storage detects its own corruption. Recomputing a hash on every local
read would be software auditing working hardware.

Because the checksum never establishes identity, it need not be
collision-resistant — any hash or CRC that detects corruption suffices, and the
algorithm is recorded so a stronger one can replace it later. SHA-256 is
already computed and is a fine default. The checksum may also accelerate
deduplication candidate search, but dedup correctness relies on literal byte
comparison, never on it.

## Deduplication is optional, asynchronous, and byte-exact

Deduplication applies **only to blobs**; assets and instances are never
deduplicated. It is an optional space optimization — the design is fully
correct if every asset has its own blob and no bytes are ever shared; that
simply costs more disk. Sharing a blob across assets with a reference count
*is* the dedup mechanism; there is no separate dedup engine.

Because assets reference blobs indirectly, deduplication runs as a separate
**asynchronous, post-hoc service**, entirely off the ingest hot path:

1. Walk blobs, grouping candidates by byte length (and, as an accelerator, by
   checksum).
2. Within a candidate group, compare bytes **literally** — a definitive match,
   not a hash coincidence.
3. On a literal match, repoint the losing asset(s)' `blob_id` to the surviving
   blob (a single-row update per asset), union the two blobs' locations onto
   the survivor, and delete the redundant blob once its reference count reaches
   zero.

Blobs are immutable, so the byte-compare is stable and the operation is
convergent and idempotent: it can run at any time, as often as desired, and a
missed or deferred pass only leaves duplicate bytes, never wrong bytes.

## Reference counting

Back-links are the source of truth; a cached reference count is a derived
optimization.

- A **blob** is live while any asset references it. An **asset** is live while
  any instance (inventory item, or a rezzed / task-embedded scene object)
  references it.
- Hot-path logic reads the cached count. A cached **non-zero** is safe to trust
  — at worst it is conservatively high, which only delays collection.
- **Any destructive action on a zero — final delete, garbage-collection
  eligibility, dedup blob removal — must first recompute from back-links and
  never trust a cached zero.** A wrongly-zero count would lose data; the
  recompute makes the irreversible path safe while keeping the fast path fast.
- The count is also recomputed on detected inconsistency and by periodic audit.

## Permissions and export

Standard copy/mod/transfer permission masks are instance-level and behave
exactly as in Second Life; assets and blobs hold none. The creator's
**exportable-from-grid** option is an asset-level property, because it follows
the content and its creator rather than any one owner's copy.

Export therefore reads both layers: content may leave the grid only when the
asset's export option allows it **and** the exporting instance's permissions
permit it. Enforcement applies at every export boundary — OAR/IAR archives and
any asset-download capability.

## Schema

The grid registry splits into blob, asset, and (existing) instance layers.
Instances already exist as inventory items (`db/migrations/000005`) carrying
owner and permission masks. The blob and asset layers replace the combined
`asset_metadata` / `asset_locations` of migration `000007`:

```
blobs(blob_id, byte_length, checksum, checksum_algorithm, created_at)
blob_locations(blob_id, endpoint, is_origin, verified_at, PRIMARY KEY (blob_id, endpoint))
assets(asset_id, blob_id -> blobs, creator_user_id, exportable, provenance, cached_refcount, created_at)
```

- Locations attach to the **blob** (`blob_id`), not the asset — identical bytes
  reachable at an endpoint are reachable regardless of which asset names them.
- `checksum_algorithm` is stored alongside the checksum so a stronger algorithm
  can be adopted later as a data migration rather than a redesign.
- Deduplication is enabled or disabled purely by policy: the design permits
  multiple blobs with the same checksum (no unique constraint on checksum), and
  the async service coalesces them when and if it runs.

Migration from `000007` seeds one blob per distinct existing `(sha256, size)`,
repoints each asset at its blob, and moves location rows from asset to blob.

## Relationship to other ADRs

- **ADR 0014 (Content-Addressed Region Assets)** — amended: blob identity is
  `blob_id`, not the content hash; the hash survives as the fail-closed
  integrity checksum.
- **ADR 0017 (Central Inventory Metadata)** — inventory items are the instance
  layer; unchanged.
- **ADR 0020 (Asset Origin And Replication)** — the registry becomes
  blob-keyed; serving locations and origin/replica registration attach to
  `blob_id`. The immutability guarantee (a UUID never remaps to different
  content or creator) is carried by the immutable `asset_id → blob_id` binding,
  not by comparing content hashes at registration. Cross-boundary fetch
  verification uses the integrity checksum where trust requires it.
- **ADR 0026 (Vault-Authoritative Inventory Assets)** — the vault stores
  **blobs**; retention is decided by the reference counting above (a blob is
  vault-durable while any live asset/instance references it).
