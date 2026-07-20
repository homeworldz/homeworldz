# HomeWorldz Assets

This document describes how HomeWorldz stores, identifies, and moves asset
content: region-local storage, inventory-referenced assets, what happens to an
avatar's attachments during a teleport or border crossing, and how viewers will
observe content across a region border. It is the implementation-level
companion to the asset sections of [ARCHITECTURE.md](ARCHITECTURE.md) and to
four ADRs:

- [ADR 0014: Content-Addressed Region Assets](adr/0014-content-addressed-assets.md)
- [ADR 0017: Central Inventory Metadata](adr/0017-central-inventory-metadata.md)
- [ADR 0018: AIS-First Viewer Inventory](adr/0018-ais-first-viewer-inventory.md)
- [ADR 0020: Asset Origin And Replication](adr/0020-asset-origin-and-replication.md)

Sections are labeled **Implemented** (in the tree today), **Partially
implemented**, or **Planned** (accepted direction, not yet built). Planned
behavior cites the ADR or roadmap item it comes from; one section is explicitly
an open design area with no ADR yet.

## Design summary

Two kinds of truth live in two places:

- **Asset bytes are region-local.** Every region server owns an immutable,
  content-addressed blob store for the assets its scene and connected viewers
  need. There is no central blob service; the grid never stores asset bytes.
- **Asset and inventory *metadata* are grid-central.** PostgreSQL at the grid
  holds inventory folders/items and the asset **federation registry**: which
  asset UUIDs exist, their SHA-256/size/creator, and which region endpoints can
  serve them.

Content moves by **pull-through replication**: when a region needs bytes it
does not have, it asks the grid *where* the asset lives, fetches it from
another region, verifies it, stores it, and registers itself as an additional
replica. Nothing is pushed ahead of time, and replicas are only ever added —
immutable content-addressed blobs make copying safe and idempotent.

## 1. Region-local asset storage — **Implemented**

Defined by ADR 0014; implemented in `region/src/region_storage.cpp`.

### Blob store

Asset bytes are immutable blobs addressed by lowercase SHA-256 and sharded on
the region filesystem:

```
<region-data>/assets/<first-two-hex>/<remaining-62-hex>
```

Writes go to a same-directory temporary file followed by atomic replacement.
Reads recompute the SHA-256 and **fail closed** on missing or corrupted
content — a region never serves bytes that do not match their address.

### Viewer-facing identity (SQLite)

Viewers address assets by UUID, not hash. Region SQLite maps between them:

```
asset_mappings(viewer_id, creator_id, sha256, size)
```

- Many viewer UUIDs may map to the same blob; identical bytes are stored once.
- `creator_id` is **required creation provenance**: the authenticated
  uploader's agent UUID, or the zero UUID for bundled/migrated content with
  unknown origin. Provenance is independent of inventory ownership and is
  never replaced (see FEATURES.md, "Asset creation provenance").
- Mappings are immutable. Re-registering an existing viewer UUID is idempotent
  only when hash, size, and creator all match (ADR 0020); different bytes or
  provenance require a new UUID. Conflicts are rejected.
- A `baked_texture_cache(cache_id, texture_index, asset_id)` table persists
  baked appearance textures keyed by wearable hash, enabling server-side
  wearable-cache hits across sessions.

### How viewers get bytes

The region advertises `GetTexture` and `ViewerAsset` capabilities pointing at
its own HTTP endpoint (`region/src/viewer_capabilities.cpp`). A viewer fetches
asset bytes **from the region it is connected to**, which serves them from the
local blob store — or fetches them on demand via federation (section 3).

Unreferenced-blob collection is deferred until retention requirements are
defined (ADR 0014); today the blob set only grows.

## 2. Inventory assets (including non-rezzed items) — **Implemented**

Defined by ADRs 0017/0018; grid schema in `db/migrations/000005` (inventory)
and `000006` (library identity); code in `grid/internal/inventory`.

A natural assumption is that the grid must store the asset bytes for inventory
items, since an item may not be rezzed in any region. HomeWorldz deliberately
does **not** do this:

- The grid's PostgreSQL stores inventory **metadata**: folders (one root per
  user, at most one folder per system type, monotonic versions for descendant
  change detection) and items (explicit viewer asset/inventory types,
  permission masks, sale metadata, flags, creator/owner). Items reference
  viewer-facing **asset UUIDs**.
- The **bytes** behind those UUIDs remain on region servers — normally the
  region where the upload or edit happened (the *origin*), plus any regions
  that have since replicated them.

Inventory therefore survives independently of any one region (the metadata is
central), while bytes stay where they were created until some region actually
needs them. The federation registry (section 3) is what connects the two: any
region can resolve an inventory item's asset UUID to a fetchable endpoint. A
region being offline can make specific *bytes* temporarily unfetchable (if it
holds the only replica), but never damages the inventory structure itself.

Viewer inventory access is AIS v3-first (ADR 0018): mutations go through the
Agent Inventory Service protocol backed by the same grid store; legacy UDP
paths are optional compatibility shims over identical operations. The shared
"Library" is ordinary inventory owned by a reserved `homeworldz.library`
identity, seeded by the `configure-library` tool.

## 3. Asset federation: lookup, fetch, replicate — **Implemented**

Defined by ADR 0020; grid registry in `grid/internal/assetmeta` (migration
`000007`); region client in `region/src/grid_client.cpp`; on-demand fetch in
`region/src/main.cpp` (`read_federated_asset`).

The grid keeps a federation registry, not blobs:

```
asset_metadata(asset_id, creator_user_id, sha256, size)
asset_locations(asset_id, endpoint, origin, verified_at)
```

- After durably storing an authenticated upload, a region registers itself as
  the asset's **origin** (`POST /api/v1/assets`).
- When a region needs bytes it lacks, it runs the pull-through sequence:
  1. Try the local blob store.
  2. Ask the grid for the asset's metadata and location list.
  3. Try each *other* region endpoint: `GET /api/v1/assets/{uuid}` with the
     internal service token.
  4. Verify the advertised **size and SHA-256** of the downloaded bytes;
     mismatches are skipped (fail closed).
  5. Store the blob locally under the same viewer UUID and creator provenance.
  6. Register itself with the grid as an additional **replica**.
- Origin loss does not invalidate verified replicas; any registered location
  can serve future fetches.

Endpoints are operator-configured stable region URLs. The single service token
is acceptable for the current single-operator grid; scoped, short-lived fetch
authorization must replace it before mutually untrusted regions are supported,
and production federation requires HTTPS (ADR 0020).

## 4. Attachment assets on teleport — **Partially implemented**

Avatar transfer uses the grid-coordinated transit transaction of
[ADR 0025](adr/0025-idempotent-avatar-transits.md): a transit UUID with a
per-avatar generation moves through `prepared → accepted → activated` (or
`rolled_back`), the source region keeps authority until activation, and every
step is idempotent. **This same transaction is shared by explicit teleports and
border crossings** — the difference is who initiates and how the destination
position is computed, not the asset mechanics.

What is implemented today (ROADMAP, Phase 2):

- Explicit teleports keep **appearance, inventory, and Current Outfit stable**
  across regions. Inventory needs no transfer at all — it is grid-central
  metadata, equally visible from every region. Appearance is re-established at
  the destination from the avatar's wearables and baked textures.
- Asset bytes the destination lacks (wearable bodies, clothing, baked
  textures, attachment meshes/textures once attachments transfer) arrive by
  the **pull-through path of section 3** — the destination fetches, verifies,
  stores, and registers replicas on demand. Nothing is pre-pushed with the
  transit; the transit carries *state*, the federation layer moves *content*.

What is still open (unchecked roadmap items):

- **Transferring the complete attachment set with the avatar** and preventing
  duplicate activation at source and destination. Per ADR 0025, regions are
  responsible for serializing "appearance, attachments, controls, physics, and
  later script state" into the transit bundle; the attachment portion of that
  bundle is not yet built.
- Remote-host failure recovery and reconciliation for interrupted teleports.

For a scripted attachment, the Falcon VM's crossing snapshot ([VM.md](VM.md))
is designed to ride inside this same bundle, so a script suspends mid-handler
at the source and resumes identically at the destination.

## 5. Attachment and object assets on a border crossing — **Partially implemented**

A crossing is the same ADR 0025 transaction triggered by a detected border
exit: the source selects the online neighbor covering the exact border
coordinate, translates the position into the destination's local frame,
prepares the transit, and rolls back if the crossing does not activate within
30 seconds. A two-way live Firestorm border handoff (Welcome ↔ Sandbox) has
passed acceptance; crossing while walking/flying with full state preservation
and the attachment-set transfer remain open, as does the object/vehicle bundle
(which may use a separate record type per ADR 0025).

**"Different or additional region?" — additional.** Asset bytes are never
*moved*. When an entity crosses, the destination pulls whatever blobs it lacks
and registers as a replica; the source keeps its copies and remains a valid
location. Because blobs are immutable and content-addressed, this is safe,
idempotent, and convergent: an avatar commuting between two regions causes at
most one fetch per asset per region, ever — after that, both regions serve the
content locally. The replica set for an asset only grows (garbage collection
is deferred per ADR 0014).

For every *moving* entity, the roadmap requires a defined off-region
disposition: cross to an accepting neighbor, bounce/contain within the source,
or return an owned object to inventory — no entity may continue silently
outside all region authority. Today, at a border with no eligible online
neighbor, avatars and physical objects are constrained to the region extent
with outward velocity cancelled ([PHYSICS.md](PHYSICS.md)).

## 6. Observing assets across a border, within draw distance — **Planned (open design)**

This is the one area with **no accepted ADR yet**; nothing is implemented, and
the notes below are the anticipated direction, not a commitment. Today a viewer
sees the scene of the region its agent occupies; neighbor topology is known
(regions discover cardinal neighbors via the grid and expose them to the
viewer for the world map, which already composes adjacent tiles), but neighbor
*scene content* is not streamed.

The Second Life viewer protocol HomeWorldz targets solves this with **child
agents**: the region an avatar occupies tells the viewer about its neighbors
(`EnableSimulator` / `EstablishAgentCommunication`), the viewer opens a
secondary, non-authoritative connection to each neighbor within interest
range, and each neighbor streams its own object updates directly to the
viewer.

That model composes cleanly with the HomeWorldz asset architecture, which is
the main reason to expect it to be adopted largely as-is:

- Each neighbor advertises **its own** `GetTexture`/`ViewerAsset` capabilities
  on the child connection, so the viewer fetches a neighbor's scene assets
  **directly from the region that owns them**. Cross-border visibility
  requires no asset proxying, no pre-replication, and no new federation
  traffic — draw distance is a *viewer↔region* concern, and region-local
  storage already serves it.
- Region-to-region federation (section 3) stays reserved for what actually
  transfers authority: uploads, teleports, and crossings.
- Child connections also give crossings a warm start: the viewer already holds
  the destination's scene state and capabilities when the avatar reaches the
  border, which is how the protocol achieves seamless handoffs.

Open questions to settle in a future ADR: which neighbor connections to open
(all online cardinals vs. draw-distance/interest-driven), child-agent presence
records and their capacity/authorization rules at the neighbor, interest-list
filtering of object updates near the shared edge, and lifecycle (when child
connections are dropped as the avatar moves away). Physics stays strictly
authoritative in one region per entity ([PHYSICS.md](PHYSICS.md)) — observation
across a border never duplicates simulation.

## Halcyon/InWorldz lineage

The stack-and-cache asset topology of InWorldz (per-region caches over central
services) informs performance expectations, but HomeWorldz inverts the
authority: regions are the *primary* asset stores and the center holds only
metadata (ARCHITECTURE.md). Whisper/Aperture-style separate texture servers
are unnecessary because each region serves its own HTTP asset capabilities.

## References

- [ARCHITECTURE.md](ARCHITECTURE.md) — system shape, "Asset Architecture"
- [FEATURES.md](FEATURES.md) — asset creation provenance; region-local assets
- [ADR 0014](adr/0014-content-addressed-assets.md), [ADR 0017](adr/0017-central-inventory-metadata.md),
  [ADR 0018](adr/0018-ais-first-viewer-inventory.md), [ADR 0020](adr/0020-asset-origin-and-replication.md),
  [ADR 0025](adr/0025-idempotent-avatar-transits.md)
- [ROADMAP.md](ROADMAP.md) — Phase 2 crossings and navigation milestones
- [VM.md](VM.md) — Falcon VM crossing snapshots for scripted attachments
