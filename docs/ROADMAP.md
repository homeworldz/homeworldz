# Homeworldz Roadmap

This roadmap describes the major implementation sequence for Homeworldz. It is
organized at three levels: phases, milestones within each phase, and major work
items within each milestone. [`PLAN.md`](PLAN.md) remains the detailed
engineering checklist, while [`FEATURES.md`](FEATURES.md) records intentional
product differences and the ADRs record architectural decisions. 

Checkboxes describe the present state, not a promise of a release date. A
milestone is complete only when its automated tests and applicable Firestorm
acceptance tests pass.

## Progress snapshot

**Updated 2026-07-28**: These bars are effort-weighted engineering estimates, not
simple checkbox ratios. Later scripting, crossings, social systems, security,
recovery, and scale items are substantially larger than many completed viewer
protocol tasks. Percentages are deliberately approximate and should be revised
when scope or implementation evidence changes.

Two overall bars, because this repository carries two deliverables on
different clocks: the legacy-compatible server platform (phases 1-8, serving
Firestorm and compatible viewers), and the back-end grid/region support for
the modern Homeworldz client (phases 9-10). The client itself is tracked in
its own repository with its own roadmap and progress.

<p>
<label class="roadmap-overall-progress">
  <span>Legacy (Firestorm-compatible) services:</span>
  <progress data-color="primary" max="100" value="30">30%</progress>
  <strong>30%</strong>
</label>
</p>

<p>
<label class="roadmap-overall-progress">
  <span>Modern Homeworldz client + back-end:</span>
  <progress data-color="primary" max="100" value="28">28%</progress>
  <strong>28%</strong>
</label>
</p>

| Phase | Progress | Estimate |
| --- | --- | ---: |
| 1. Functional Single-region World | <progress class="roadmap-phase-progress" data-color="primary" max="100" value="100" aria-label="Phase 1 progress: 100%">100%</progress> | 100% |
| 2. Connected Multi-region World | <progress class="roadmap-phase-progress" data-color="primary" max="100" value="82" aria-label="Phase 2 progress: 82%">82%</progress> | 82% |
| 3. Interactive Physical World | <progress class="roadmap-phase-progress" data-color="primary" max="100" value="39" aria-label="Phase 3 progress: 39%">39%</progress> | 39% |
| 4. Mesh and Creator Platform | <progress class="roadmap-phase-progress" data-color="primary" max="100" value="6" aria-label="Phase 4 progress: 6%">6%</progress> | 6% |
| 5. LSL Scripting | <progress class="roadmap-phase-progress" data-color="primary" max="100" value="15" aria-label="Phase 5 progress: 15%">15%</progress> | 15% |
| 6. Social Communications | <progress class="roadmap-phase-progress" data-color="primary" max="100" value="4" aria-label="Phase 6 progress: 4%">4%</progress> | 4% |
| 7. Reliable Operations and Distribution | <progress class="roadmap-phase-progress" data-color="primary" max="100" value="12" aria-label="Phase 7 progress: 12%">12%</progress> | 12% |
| 8. Scale, Compatibility, and Ecosystem | <progress class="roadmap-phase-progress" data-color="primary" max="100" value="2" aria-label="Phase 8 progress: 2%">2%</progress> | 2% |
| 9. Modernized Communications Transport | <progress class="roadmap-phase-progress" data-color="primary" max="100" value="55" aria-label="Phase 9 progress: 55%">55%</progress> | 55% |
| 10. Modern Client Support | <progress class="roadmap-phase-progress" data-color="primary" max="100" value="3" aria-label="Phase 10 progress: 3%">3%</progress> | 3% |

The overall estimate is weighted by expected effort and therefore is not the
arithmetic mean of the phase percentages. The binary checkboxes below remain
the acceptance record; partially implemented work contributes to these bars
but stays unchecked until its complete wording is satisfied.

## Phase 1: Functional Single-region World

### Platform foundation

- [x] Establish the C++20 region server, Go grid service, PostgreSQL central
  state, and SQLite plus filesystem region-local state.
- [x] Define configuration, bootstrap, health, authentication, logging, CI, and
  service contracts.
- [x] Implement region registration, leases, discovery, login sessions, and
  online presence.
- [x] Establish the authoritative fixed-step scene loop and durable scene
  snapshots.

### Viewer connection and appearance

- [x] Complete the minimum supported Firestorm login, UDP circuit, handshake,
  capabilities, event delivery, terrain, chat, and static-object flow.
- [x] Provide default body parts, clothing, Current Outfit links, legacy avatar
  baking, and persistent appearance across relogs.
- [x] Provide a read-only system Library with default avatar and terrain
  content.
- [x] Synchronize nearby avatar presence, movement, appearance rebakes, and
  animation changes between concurrently connected viewers.
- [x] Bake avatar appearance **server-side** — the region composites the bake
  layers (skin, colour-tinted and alpha-masked clothing, hair colour) from worn
  wearables with OpenJPEG and serves the baked textures, so thin or headless
  clients (e.g. LibreMetaverse) rez correctly with no client-side baking
  (ADR 0029). A LibreMetaverse bot — whose own client baker fails — rezzes as a
  correct default avatar (textured head, hair colour, tinted shirt/pants,
  skin hands and feet) entirely from the server bake. Per-user COF-driven baking
  for arbitrary custom outfits and server-side-appearance (SSB) delivery to full
  viewers remain future work; full clients still bake locally and are relayed
  untouched.
- [x] Broadcast `KillObject` for a departing avatar so it no longer lingers
  rezzed in other viewers' views. All avatar-removal paths (clean logout,
  session-invalidation/disconnect via the 5-second session revalidation,
  duplicate-login replacement, and teleport/region-crossing source-retirement)
  funnel through one teardown point that broadcasts a `KillObject` for the
  avatar's local id to the remaining in-region viewers; in-region moves keep the
  avatar in the region set, so it does not misfire. Presence/People-list is
  cleared on explicit logout and deliberately preserved on teleport/crossing
  (the avatar stays online in the destination region). Live Firestorm
  acceptance: a departing bot vanished immediately on clean logout and was
  removed from the observer's view, People radar, and minimap. Lost connections
  (crash / force-kill / sustained packet loss) are detected by **missed ping
  replies**: the region pings every 5s and retires a viewer that has not answered
  a `CompletePingCheck` within `region.connection_timeout_seconds` (default 60s),
  broadcasting its kill rather than waiting on the grid session TTL (verified by
  force-killing a bot → retired ~62s later). An idle-but-connected viewer still
  answers pings, so it is never affected, and the timeout stays well above
  transient outages. On region shutdown (SIGINT/SIGTERM) the region sends each
  connected viewer a `KickUser` with a reason string — so it shows a clear
  "region is restarting" message instead of a generic disconnect — then waits
  briefly for delivery before exiting.

### Authoritative avatar movement

- [x] Decode movement, camera, jump, and flight controls and persist provisional
  avatar state.
- [x] Stream authoritative position, velocity, and rotation changes
  back to viewers.
- [x] Complete viewer-visible walking, turning, jumping, stopping, flight
  toggling, ascent, and descent without requiring a relog.
- [x] Add animation-state selection and synchronization for standing, walking,
  running, jumping, falling, flying, hovering, and landing.
- [x] Reconcile viewer prediction with the authoritative region position without
  visible snapping or drift, confirmed by live Firestorm observation. The region
  streams authoritative position and velocity and the viewer dead-reckons between
  updates; reconciliation is a viewer-side, live-verified outcome rather than
  region-side code.

### Basic avatar physics

- [x] Integrate a Jolt avatar capsule into the production scene loop.
- [x] Support persistent viewer terrain editing with live patch updates for
  targeted slope, step, drop, and grounding tests.
- [x] Mirror authoritative terrain into Jolt at startup and replace the collision
  heightfield immediately after viewer edits.
- [x] Sample terrain continuously for provisional grounding rather than
  retaining only the height at the login position.
- [x] Support terrain walking, slopes, steps, falling, jumping, landing, flight,
  and collision-safe motion.
- [x] Collide avatars with static scene objects while preserving practical
  viewer movement behavior.
- [x] Collide avatars with dynamic scene objects while preserving
  practical viewer movement behavior.
- [x] Persist and restore position, orientation, velocity, and flight state;
  safely recompute grounded contact from Jolt on entry.

### Core inventory, assets, and objects

- [x] Implement AIS v3 inventory fetch and mutation, viewer-authored wearables,
  named outfit saving, Library outfit copying, Current Outfit links, folder
  operations, and Trash lifecycle operations.
- [x] Implement free texture upload, required creator provenance,
  content-addressed assets, origin registration, and region replication.
- [x] Implement primitive rez, edit, permissions, ownership, take, delete,
  restore, and restart persistence.
- [x] Implement persistent nonphysical linksets with root and child transforms,
  whole-object and Edit Linked scaling, duplication, take, take-copy, return,
  derez, inventory round trips, and static child collision.
- [x] Implement task inventory (object contents) and complete its permissions,
  mutation, copy, derez, return, and inventory round-trip lifecycle.
- [x] Implement creator-attributed sound and animation uploads; personal
  notecard, gesture, and LSL-source creation and updates; and task notecard and
  script updates. Phase 5 now compiles and executes the supported Falcon
  language subset when a script enters or is saved in object contents.
  (Menu-based personal landmark creation needs land data and moves to Phase 2;
  existing landmark assets remain usable via teleport routing.)
- [x] Complete Firestorm creation, editing, playback, object-contents,
  restart, and relog acceptance for those fundamental content types. (Landmark
  creation via the viewer and About Land ownership are deferred to Phase 2 land
  operations.)

## Phase 2: Connected Multi-region World

### Region topology and variable size

- [x] Load an operator-owned JSON registry of provisioned regions and
  authenticate region startup by UUID plus per-region access key, returning the
  authoritative name and map coordinates.
- [x] Add authenticated grid-management endpoints to create, inspect, update,
  enable, disable, relocate, remove, and rotate credentials for provisioned
  regions.
- [x] Persist each region's UUID, unique name, owner UUID, X/Y location,
  endpoints, enabled state, and per-region access-key hash independently of its
  online lease.
- [x] Let a region authenticate by UUID or unique name plus its access key and
  fetch effective grid-wide and region-specific startup configuration.
- [x] Represent neighboring regions, coordinates, extents, public endpoints,
  maturity, and online state in grid discovery.
- [x] Support exactly 1x1 (256 m), 2x2 (512 m), and 4x4 (1024 m) provisioned
  Regions in runtime configuration, automated terrain/protocol/map tests, and
  disposable 512 m and 1024 m process-start checks.
- [x] Generalize terrain, physics bounds, viewer coordinates, storage, map
  tiles, and interest management to the three supported sizes.
- [x] Complete initial live Firestorm acceptance of a 2x2 Region as one
  continuous 512 by 512 metre simulator. Movement, terrain, and minimap
  position remained correct through all four quadrants and across both
  internal 256 metre lines.
- [x] Complete 2x2 terrain editing, object persistence, map-idle, and restart
  acceptance in the live Beta Region.
- [x] Repeat the full variable-size acceptance suite in a 4x4 Region.
- [x] Prevent overlaps and invalid neighbor layouts and define behavior beside
  offline or differently sized regions.

### Parcels and local authority

- [x] Implement parcel geometry, ownership, access, landing points, media,
  and object accounting. Parcels carry the full ParcelFlags/Category/LandingType
  set and a 4 m-resolution coverage bitmap generalized to 256/512/1024 m regions;
  a fresh region gets one region-wide parcel owned by the authoritative grid
  region owner. Viewers see About Land through `ParcelProperties` (delivered over
  the Event Queue), edit land options via `ParcelPropertiesUpdate`, subdivide and
  join with `ParcelDivide`/`ParcelJoin`, and manage access/ban lists via
  `ParcelAccessListRequest`/`Update`, all persisted in region SQLite. Per-parcel
  WindLight environment is deferred to the estate/region settings work below.
  Live Firestorm acceptance on the Sandbox Region (2026-07-25): About Land shows
  the owner, area, and resolved Parcel ID (`RemoteParcelRequest`); "Landmark This
  Place", Set Landing Point (with snapshot and teleport), subdivide/join with the
  boundary overlay, the Objects tab with live object return, and Set Home to Here
  (with confirmation) all work.
- [x] Enforce build, rez, entry, script, and object-return policy at authoritative
  boundaries. Build/rez (CreateObjects), script execution (AllowOtherScripts),
  teleport entry and continuous walk-in ejection (ban/access lists plus
  landing-point routing), viewer-initiated object return (`ParcelReturnObjects` to
  each owner's Lost and Found, with the About Land Objects tab backed by
  `ParcelObjectOwnersReply`/`ForceObjectSelect`), and periodic `OtherCleanTime`
  auto-return of non-owner objects are enforced authoritatively. The damage
  (`AllowDamage`) and push (`RestrictPushObject`) flags are carried and surfaced
  but their enforcement is inert until the combat/health and `llPushObject`
  systems exist (Phase 5).
- [x] Implement estate and region settings needed for terrain, access, maturity,
  restart, and emergency administration. Estates are grid-level records shared
  across regions (owner, managers, allowed users/groups, bans, deny/voice/teleport
  flags), persisted centrally and fetched by each region. The Region/Estate floater
  works: `RequestRegionInfo`/`RegionInfo` populate the Region tab (flags, maturity
  SimAccess, water, terrain limits, estate id), and `EstateOwnerMessage` `getinfo`
  returns `estateupdateinfo` + the `setaccess` lists, `estateaccessdelta` adds and
  removes bans/allowed users/managers, and `estatechangeinfo` toggles the estate
  flags. Estate bans and private-estate access gate region entry; estate owner and
  managers bypass parcel and estate restrictions. Viewer-driven terrain-texture
  settings (`setregionterrain`), region restart, and estate kick/teleport-home
  admin actions remain. Live Firestorm acceptance on the Sandbox Region
  (2026-07-25): the Region tab, Estate tab (My Estate / owner Jim Tarber), and
  Covenant tab populate correctly.
- [ ] Apply permissions recursively and consistently to linksets, object
  contents, attachments, and inventory transfers.

### Teleports and avatar crossings

- [x] Keep avatar appearance, inventory, and Current Outfit stable through an
  explicit teleport between registered regions.
- [ ] Keep avatar appearance stable while sitting and crossing region borders.
- [x] Build an authenticated, idempotent two-region handoff transaction with a
  transit UUID, generation, prepare, accept, activate, and rollback stages.
- [x] Teleport between registered regions with destination validation, viewer
  circuit establishment, arrival placement, source retirement, and durable
  last-location login.
- [x] Teleport within the current Region without creating a Grid transit,
  preserving flight state and returning Firestorm's `TeleportLocal` response.
- [x] Detect avatar border exits, select the online neighbor covering the exact
  mixed-size border coordinate, translate destination-local position, prepare
  the authenticated transit, emit Firestorm's crossing event, contain failed
  exits, and roll back an unactivated crossing after 30 seconds.
- [x] Complete initial live Firestorm acceptance for a two-way 1x1 border
  handoff between Welcome and Sandbox with one continuous viewer session,
  correct edge placement, facing and flight-state transfer, destination
  activation, and source retirement.
- [ ] Complete remote-host failure recovery and reconciliation for interrupted
  teleports.
- [ ] Cross a walking or flying avatar between adjacent regions while preserving
  appearance, controls, velocity, camera, and session continuity.
- [ ] Transfer the complete attachment set with the avatar and prevent duplicate
  activation at source and destination.
- [ ] Handle disconnects, destination failure, retries, stale transit records,
  and reconciliation after process restart.

### Object and vehicle crossings

- [ ] Define an off-region disposition for every moving entity: cross an
  eligible avatar, attachment, vehicle, or object to an accepting neighbor;
  otherwise bounce/contain it within the source region or return an owned
  object to inventory. No entity may continue silently outside all region
  authority.
- [x] At a border with no eligible online neighbor, constrain avatar and
  physical-object origins to the configured Region extent and cancel outward
  velocity at the crossed edge.
- [x] Resolve border neighbors from persistent grid region records plus their
  current online leases before choosing crossing versus containment.

- [ ] Cross individual objects and complete linksets without changing creator,
  owner, permissions, inventory, or physical state.
- [ ] Cross scripted and unscripted attachments as part of their avatar bundle.
- [ ] Cross vehicles while preserving linear and angular motion, vehicle
  parameters, and object inventory.
- [ ] Transfer a vehicle and all seated avatars as one coordinated bundle, with
  no passenger briefly becoming authoritative in both regions or neither.
- [ ] Establish event and collision cutoffs so crossing does not duplicate or
  silently lose observable actions.

### World navigation

- [x] Generate live terrain-derived region tiles and compose world-map zoom
  levels for 1x1 regions.
- [x] Extend region and world-map tile composition to the planned 2x2 and 4x4
  region sizes.
- [x] Implement viewer map-block and prefix-name discovery for registered live
  regions.
- [x] Implement landmark resolution, home location, and teleport routing.
  Login-to-home currently lands in the home region at the last in-region
  position; exact home-coordinate placement on login is deferred within this
  phase.
- [x] Serve region land data (ParcelProperties: owner, flags, area, bitmap,
  landing point, prim accounting) so the viewer's "Landmark This Place" creation
  and About Land ownership work. Delivered over the Event Queue as LLSD; full
  parcel geometry and local authority landed early with the Parcels work above.
- [ ] Add region and parcel search sufficient to find and reach destinations.
- [ ] Show friends and authorized users useful presence and location without
  leaking restricted information.

### Inventory asset durability

**Landed 2026-07-28.** A region that dies no longer takes its users'
inventory with it: the vault holds verified bytes for every
inventory-referenced asset — the item's whole reference closure, since
inventory-to-asset is 1:N — and the grid refuses any inventory commit it
cannot make durable first. The adoption backfill named 19 pre-vault assets
as already lost; everything else is safe, and everything created from now
on is safe by construction. Regions mirror the rule for scene content: a
rezzed object's closure is materialized region-locally so region backups
are self-contained, and a hidden retained record preserves no-copy items
that leave inventory into the scene.

Sequencing, decided on one fact: **the vault is empty, so re-keying it costs
nothing now and means moving every stored blob later.** The layer separation
below therefore lands before the write-through and backfill that fill the
vault, and the enforcement is written once against the final shape rather
than twice.

- [x] Separate the blob, asset, and instance layers of
  [ADR 0027](adr/0027-asset-blob-instance-separation.md): a grid-assigned
  `blob_id` naming bytes, with the digest demoted to an integrity checksum;
  `asset_id` carrying creator, provenance, and the exportable option;
  locations attached to blobs rather than assets; and reference counts from
  back-links deciding retention. Instances (inventory items, rezzed objects)
  already hold owner and permissions and do not move. `blob_id` stays
  grid-internal — regions keep naming assets and verifying with the
  checksum — so this re-keys the vault and the registry without changing
  what a region speaks.

- [x] Implement the grid asset vault: a durable, replica-only blob store that
  never originates assets, never hosts agents, and is never in the viewer data
  path (ADR 0026). Blob bytes live on a sharded filesystem tree under the
  `[vault] path` setting and are indexed in PostgreSQL; regions reach them at
  `/api/v1/vault/assets/{assetId}` behind the internal service-token boundary,
  which is what keeps the vault out of the viewer fetch path. Ingest verifies the
  registry's recorded checksum and length on a temporary file before an atomic rename, so
  bytes that fail verification are never reachable, and it is idempotent. A
  presence check confirms the stored file as well as the index row, so the vault
  never claims a blob it cannot serve. This is the store only — the enforcement,
  ingest, fallback, and backfill items below are what make inventory durability
  real, and until they land the vault holds nothing.
- [x] Enforce the vault invariant grid-side: commit an inventory item only
  after the vault holds verified bytes for its referenced asset — the whole
  reference closure, gathered by parsing the vault's own copy of the bytes
  (objects' face textures and task inventory, nested objects recursively,
  wearable textures, gesture animations and sounds, notecard embeds).
  Enforcement wraps the inventory store, so every committing path — and any
  written later — passes through it; the grid *fetches* bytes from recorded
  locations at commit, so durability never depends on region cooperation and
  a region write-through stays an optimization.
- [x] Treat region copies of vault-held assets as an evictable cache and
  scene-only assets as region-owned; demote region-to-region fetch to an
  optimization with the vault as the always-available fallback location. A
  region also materializes the reference closure of scene content locally —
  at rez, at contents-add, and in a whole-scene sweep at startup — so a
  backup of the region's own storage is self-contained.
- [x] Backfill existing inventory-referenced assets into the vault from live
  registered locations and report assets that are already unfetchable
  (`cmd/vaultbackfill`, idempotent, closure-aware; the first live run
  ingested 85 blobs and named 19 assets as already lost).
- [ ] Tier rarely accessed vault blobs onto slower S3-compatible storage with
  hash re-verification on rehydration, keeping tiering vault-internal.

## Phase 3: Interactive Physical World

### Production physics integration

- [x] Make Jolt the default production physics world while retaining the
  engine-independent plugin boundary.
- [x] Create, update, sleep, wake, remove, and restore physical bodies from
  authoritative scene changes.
- [x] Synchronize physical transforms and velocities to viewers at suitable
  rates with interest-aware throttling.
- [x] Exclude phantom objects from collision and implement an authoritative,
  nonpersistent 60-second temporary-on-rez lifecycle with viewer kill updates.
- [ ] Complete collision filtering, material behavior, volume detection, and
  collision events.
- [x] Represent physical linksets as compound Jolt bodies with correct child
  shapes, mass properties, collision behavior, transforms, and persistence.
- [ ] Complete live Firestorm acceptance for compound collision, falling and
  rotation, editing, delinking, and restart persistence.
- [x] Verify deterministic-enough restart and handoff behavior through shared
  physics acceptance scenarios.

### Attachments and sitting

- [ ] Attach inventory objects to named avatar attachment points with stable
  local transforms, permissions, ownership, and persistence.
- [ ] Represent worn attachments as part of the authoritative avatar bundle and
  restore them on login.
- [ ] Implement sit targets, avatar seating, unsit, camera placement, and seated
  animation state.
- [ ] Support avatars as seated attachments to object linksets so their world
  transforms follow the root object correctly.
- [ ] Define lifecycle ordering for attachment, seated-avatar, physics, viewer,
  and later script events.

## Phase 4: Mesh and Creator Platform

What a creator needs before scripting matters: the content pipeline itself —
mesh and its collision sources, uploads and validation, inventory breadth, and
the economy boundary that decides whether creations can be sold. Scripting
operates on this content, which is why it now follows rather than precedes it.

### Mesh pipeline

Decided in [ADR 0033](adr/0033-mesh-pipeline-gltf-canonical.md): glTF (GLB)
is the canonical stored format, the creator's original upload is never
rewritten, and each client family is served a derived rendition — SL mesh
(type 49) for viewers, GLB for the Homeworldz client — generated by a
grid-side conversion worker. FBX/OBJ/DAE and Daz exports convert in the
Homeworldz client at import, keeping the server pipeline to two verifiable
input formats.

- [x] M1 static mesh — complete 2026-07-29, finish line crossed in
  Firestorm: a GLB uploaded through the session path stands in-world as a
  solid textured mesh. The last mile was serving (GetMesh capabilities and
  ranged 206 responses), the mesh ExtraParams block, mesh rez through the
  object wrapper, synthesized normals and texture coordinates for sources
  that carry none — and one pre-mesh-era SimulatorFeatures flag
  (MeshRezEnabled=0) that gates viewer mesh *rendering* and silently ate
  every proof until the viewer's own log named it. Originally shipped as: the GLB upload
  capability (`POST /session/uploads/mesh`, authorized by the region ticket
  as a bearer token — one credential, both transports), the validation gate
  with its policy published in the session hello (read, never encode), the
  `asset_renditions` table and lease-based conversion queue behind a
  dedicated worker credential, and vault write-through so an uploaded GLB
  is durable at commit. The conversion worker
  (`homeworldz-meshsmith`) is live too: it claims queued jobs on the worker
  credential, derives the type-49 payload (round-trip-tested serializer,
  meshoptimizer LOD chain, bounding-box convex physics pending V-HACD), and
  stores the rendition — the first uploaded GLB converted on the worker's
  first claim. Remaining: region mesh serving to viewers and mesh rez on
  the Jolt collision source.
- [x] M2 Firestorm mesh uploads — complete 2026-07-29, confirmed in
  Firestorm: a Utah teapot uploaded through Upload Model stands rezzed and
  smooth-shaded in-world. The mesh branch of NewFileAgentInventory: fee
  request answered with a one-shot uploader URL and a real zero price,
  viewer-written type-49 payloads stored verbatim as canonical vault
  assets (read, never encode — no rendition exists or is needed; serving
  falls back to canonical bytes), textures stored with inventory items,
  the linkset built from instance transforms with per-face TextureEntry,
  MeshUploadEnabled on, and the MeshUploadFlag permission capability the
  uploader queries. The `gltf` rendition closed the loop 2026-07-30: a
  stored type-49 asset derives a GLB (queued at upload and on first demand,
  so content predating it heals itself), and the session asset route serves
  it - the Firestorm-uploaded teapot fetches as 24880 bytes of glTF, 558
  vertices and 1024 triangles with normals and texture coordinates, and the
  first-party client renders viewer-authored content with no client change.
  Each family fetches one asset id and receives the form it can read.
- [ ] M3 material and texture renditions: glTF material JSON (type 57) for
  PBR-capable viewers and JPEG2000 texture extraction for the legacy
  texture pipeline. Related gap found live 2026-07-29: the region serves no
  RenderMaterials update capability, so viewer materials edits
  (normal/specular assignments) silently do not persist.
  Texture serving was fixed 2026-07-31: the rule that a viewer gets the
  derived JPEG2000 rather than the canonical PNG was implemented on the older
  GetTexture capability only, and Firestorm fetches through ViewerAsset, which
  special-cased `mesh_id=` and returned canonical bytes for everything else.
  Every server-side check passed — right id, 200, sound rendition — while the
  viewer held its grey placeholder. Two capabilities that had to agree, made
  to agree by duplication, is the defect worth remembering; the request is now
  folded into one shape where it is parsed. Both parsers still live in
  `main.cpp`'s anonymous namespace with no test able to reach them.
- [ ] Terrain surface for session clients (client core request 2026-07-29,
  after the operator saw untextured ground in the desktop client). The
  ground's geometry is published and verified; its *surface* is not. Today
  the four layer textures are a hardcoded grid-wide constant in the region
  binary (real vault assets: Sand and Dirt, Grass, Mountain, Rock) and the
  per-corner elevation parameters are literals in the RegionHandshake
  encoder (start 10, range 60, uniform) — neither is per-region data, and
  no blend rule is implemented server-side at all: viewers apply their own.
  There is a format inversion in the way as well: the four layer assets are
  JPEG2000 *at rest*, and the first-party client refuses JPEG2000 by rule, so
  they are unusable by it however stable they become. The committed direction
  is the mesh pipeline's pointed at textures - modern canonical,
  `j2c-texture` derived - so the seed assets need re-sourcing from modern
  originals rather than a client exception.
  Prerequisites, in order: re-source the layer assets so a modern client can
  read them, make textures and elevation per-region state
  (what `setregionterrain` would drive), then decide the blend contract
  knowing that a viewer's blend is not ours to specify — SL's noise term
  was never reproduced outside Linden, so a published rule is authoritative
  for first-party clients only and the two families will differ subtly on
  the same ground. Publishing an approximate rule early is worse than
  publishing none (the client core's own preference, and the reason the
  contact model stayed unpublished).
- [ ] V-HACD convex decomposition for mesh physics; the shipped physics
  block is the conservative bounding-box hull.
- [x] Regenerate stale renditions — live 2026-07-29: the generator column
  records which converter produced each rendition; the grid re-queues
  everything a different generator produced (worker-token endpoint), and
  meshsmith sweeps at startup, so a deployed converter upgrade reconverts
  existing content automatically (its first sweep reconverted the pre-UV
  probe meshes).
- [ ] M4 rigged mesh: glTF skins mapped onto the Bento skeleton (refusing
  rigs that do not map), attachments and body wearables.
- [ ] M5 import breadth: client-side FBX/OBJ/DAE import, documented Daz
  Studio export path, optional web import service on the management site.

### Content creation and inventory breadth

- [ ] Complete viewer building workflows for linksets, materials, sculpt,
  animation, sound, gesture, notecard, landmark, and script content.
- [ ] Store portable mesh collision sources separately from visual LODs; build
  validated static triangle shapes or dynamic convex compounds through the
  selected physics adapter, with immutable collision capture for deforming
  meshes and non-colliding attachments by default.
- [ ] Implement uploads, validation, dependencies, creator attribution, asset
  replication, and inventory creation for each supported asset type.
- [x] Add viewer-authored wearable creation, editing, and named outfit saving
  beyond the initial default-avatar flow.
- [ ] Provide bulk inventory, search, copy, transfer, export-policy, recovery,
  and large-inventory performance behavior.

### Economy and marketplace boundary

What a creator can sell and how the value moves. Whether a given grid runs an
economy at all is deployment configuration rather than creator tooling, and
lives with the operator's other settings in Phase 7.

- [ ] Define whether credits remain display-only or become a transferable grid
  balance before implementing paid behavior.
- [ ] If enabled, implement auditable balances, idempotent transactions, object
  sales, parcel payments, gifts, and refunds.
- [ ] Treat external payment processing and marketplace integration as separate,
  explicitly approved security projects.

## Phase 5: LSL Scripting

### Language and compiler

- [x] Establish the dependency-free handwritten Falcon lexer, parser, semantic
  analyzer, versioned bytecode format, compiler, and automated proof-of-concept
  suite for an initial typed LSL subset.
- [x] Return Falcon compilation success and escaped error arrays through the
  Firestorm task-script capability protocol, including line and column locations
  for lexical errors.
- [ ] Inventory the complete Second Life LSL language and built-in surface plus
  Halcyon/InWorldz extensions, explicitly excluding OpenSimulator extensions.
- [ ] Complete the handwritten lexer, parser, semantic analysis, diagnostics,
  and versioned Homeworldz bytecode compiler for that full supported language.
- [x] Store creator-attributed LSL source in personal and task inventory, with
  Firestorm creation, retrieval, editing, saving, and drag-to-contents behavior.
- [ ] Cache immutable bytecode by source hash, compiler version, and runtime ABI.
- [ ] Build compatibility tests for syntax, types, conversions, lists, strings,
  states, constants, built-ins, and observable errors.

### Cooperative runtime and resource control

- [x] Integrate the single-threaded C++ Falcon bytecode VM into the authoritative
  Region thread with explicit instruction-level execution state and no native
  thread per script.
- [x] Apply bounded aggregate and per-script instruction slices on every Region
  tick so an infinite loop yields cooperatively instead of blocking the world.
- [ ] Schedule scripts fairly using bounded weighted instruction and wall-clock
  budgets across scripts, objects, owners, and parcels.
- [ ] Enforce memory, stack, call-depth, event-queue, string, list, payload,
  owner, object, and parcel limits.
- [ ] Make slow host operations asynchronous and represent waits as serializable
  tokens or continuations.
- [ ] Add operator metrics, throttling, diagnostics, stopping, resetting, and
  isolation for inefficient or faulty scripts.

### LSL Events and Region Interaction

- [x] Decode Firestorm `RezScript`, create or transfer the task inventory item,
  compile its source, instantiate an enabled VM, and dispatch `state_entry`.
- [x] Recompile task scripts after Firestorm edits, preserve the previous running
  instance after a failed compile, honor the viewer's running flag, and remove
  the live VM when its task inventory item is deleted.
- [x] Route the initial `llSay` and `llOwnerSay` host calls to Firestorm object
  chat with owner-only and distance behavior, confirmed in the live cloud Grid.
- [x] Advertise the `SCRIPTED` and `HANDLE_TOUCH` object-update flags for prims
  carrying enabled scripts so Firestorm enables the Touch action, then decode the
  `ObjectGrab` touch packet distinctly from the physical `ObjectGrabUpdate` drag
  path, authorize the touching avatar, resolve the clicked child and linkset
  root, and dispatch `touch_start(1)` to each enabled compiled script through a
  bounded per-script event queue that never clobbers an in-flight handler.
- [ ] Implement the remaining object lifecycle, sustained/ended touch, timer,
  listen, sensor, control, permission, inventory, changed, link-message,
  collision, land-collision, attachment, and moving events.
- [ ] Implement bounded LSL host functions for scene, physics, inventory,
  communication, parcel, avatar, HTTP, and data operations.
- [ ] Preserve Second Life event ordering and delay semantics where observable
  and document intentional Homeworldz differences.
- [ ] Integrate script ownership and permissions with linksets, attachments,
  seated avatars, parcels, and estate policy.

### Script persistence and crossings

- [x] Demonstrate automated Falcon snapshots after every completed instruction,
  restoration into a fresh VM, preservation of globals, and continuation from
  the middle of a `touch_start` handler.
- [ ] Restore enabled task scripts across Region restarts. Startup now re-rezzes
  enabled task scripts so they run and re-advertise touch, but each restart still
  re-runs `state_entry` because VM state is not yet persisted; full state-carrying
  restoration remains outstanding.
- [ ] Serialize bytecode identity, instruction pointer, stacks, frames, globals,
  current event, event queue, timers, listens, permissions, and pending work in
  a compact versioned binary format.
- [ ] Integrate stop-and-restore after any completed bytecode instruction into
  live task scripts and Region persistence without relying on the native C++
  stack.
- [ ] Snapshot scripts atomically with their attachment, object, or vehicle
  physics bundle.
- [ ] Cross heavily scripted attachments and vehicles within defined latency,
  memory, duplication, and event-loss limits.
- [ ] Version the runtime ABI and provide safe upgrade, incompatibility, and
  rollback behavior for stored script state.

### Vehicles and physical objects

- [x] Implement stable dynamic-object movement, editing, taking, and restoration
  without losing physics state.
- [ ] Add the Second Life vehicle parameter model required by LSL vehicles.
- [ ] Make a single `llSetVehicleType(VEHICLE_TYPE_*)` call activate a usable
  SL/Halcyon-compatible car, sled, boat, airplane, balloon, sailboat, or motorcycle
  preset; map presets and later parameter overrides to each physics plugin's
  native vehicle, motor, and constraint facilities.
- [ ] Synchronize driver controls, vehicle motion, cameras, passengers, and
  seated-avatar transforms.
- [ ] Preserve object, linkset, inventory, permission, passenger, and physical
  state as one transferable vehicle bundle.
- [ ] Add load, tunneling, stacking, recovery, and abusive-object safeguards.

## Phase 6: Social Communications

Who people are to each other, and how they reach each other: identity and
profiles, direct and group messaging, voice, friendship, and the group and
role machinery that shared ownership rests on.

### Identity, profiles, and communication

- [ ] Implement user-visible names, profiles, interests, images, privacy, and
  account administration.
- [ ] Implement direct messages, offline messages, group chat, conference chat,
  mute/block behavior, and delivery history where appropriate.
- [ ] Provide voice via **WebRTC** — the direction Second Life and current
  viewers (including Firestorm) are moving to. Vivox is explicitly not pursued.
  Lower priority than server-side baking, but wanted sooner rather than later.
- [ ] Implement friendship, calling cards, presence permissions, and offers.
- [ ] Add abuse reporting and the minimum moderation evidence needed by grid
  operators.

### Groups, roles, and shared ownership

- [ ] Implement groups, roles, powers, membership, invitations, notices, and
  group communication.
- [ ] Support group-owned land and objects without weakening creator provenance
  or transfer permissions.
- [ ] Apply group powers consistently to parcels, estates, object editing,
  inventory sharing, and moderation.
- [ ] Audit sensitive group and ownership changes.

## Phase 7: Reliable Operations and Distribution

### Grid and region packages

- [x] Produce separate versioned grid-owner and region-owner packages containing
  prebuilt executables, runtime dependencies, examples, bootstrap tools, and
  end-user installation guides.
- [ ] Support clean install, unattended install, upgrade, downgrade where safe,
  uninstall, and configuration preservation.
- [ ] Sign release artifacts, publish checksums and provenance, and generate a
  machine-readable release manifest.
- [ ] Validate supported Windows and Linux installations without requiring a
  source checkout or development toolchain.

### Backups, upgrades, and reconciliation

- [x] Restart or replace the central grid service without restarting connected
  regions; retain PostgreSQL-backed viewer sessions so region simulation and
  active viewer circuits continue while grid-backed operations resume. This holds
  as long as grid services return before a region's lease renewal window elapses;
  a grid outage that persists for an extended period past that window stops the
  affected regions.

- [ ] Back up and restore PostgreSQL grid state, region SQLite state, assets,
  terrain, configuration, and compatible runtime state.
- [ ] Export and import OpenSim-compatible region archives (OAR) and user
  inventory archives (IAR). OAR is the portable scene backup and migration
  format; IAR covers user inventory transfer.
- [ ] Write only the latest supported OAR and IAR format versions while
  reading older format versions where practical, since archives are
  long-lived files that outlive the software that wrote them.
- [ ] Test full-grid, single-region, and selected-user recovery with documented
  recovery-point and recovery-time expectations.
- [ ] Version schemas and protocols and support rolling grid and region upgrades
  within a documented compatibility window.
- [ ] Reconcile leases, presence, inventory, assets, crossings, and duplicated or
  orphaned state after crashes or partial restores.

### Observability and administration

- [ ] Provide metrics, structured logs, traces, health detail, dashboards, and
  actionable alerts for grid and region owners.
- [ ] Add command-line and authenticated web administration for users, regions,
  estates, assets, inventory repair, scripts, crossings, and moderation.
- [ ] Make the economy an operator setting: enable or disable it per grid, keep
  texture uploads free, preserve a useful no-economy deployment mode, and
  provide the controls a running economy needs — limits, freezes, corrections,
  and their audit trail. The mechanics themselves are Phase 4.
- [ ] Record tamper-evident audit events for privileged and security-sensitive
  operations.
- [ ] Define capacity indicators and load-shedding behavior before a region
  becomes unresponsive.

### Security and deployment hardening

- [ ] Add transport encryption, service identity, credential rotation, scoped
  authorization, secret-management guidance, and secure defaults for non-local
  deployments.
- [ ] Validate all viewer, inter-region, asset, inventory, script, and operator
  inputs against resource-exhaustion and malformed-data attacks.
- [ ] Add dependency, artifact, and configuration scanning plus a vulnerability
  response and supported-version policy.
- [ ] Perform fault-injection, abuse, denial-of-service, and recovery testing
  before describing a release as production-ready.

## Phase 8: Scale, Compatibility, and Ecosystem

### Performance and scale

- [ ] Establish repeatable concurrency, scene-complexity, physics, inventory,
  asset, crossing, script, and network benchmarks.
- [ ] Implement interest management, packet prioritization, backpressure, and
  adaptive update rates for crowded or complex regions.
- [ ] Scale central services horizontally where measurements justify it while
  keeping each region's authority unambiguous.
- [ ] Publish tested capacity envelopes rather than relying on nominal limits.

### Compatibility

- [ ] Maintain conformance tests against the pinned supported Firestorm release
  and evaluate newer releases deliberately.
- [ ] Add read-only legacy inventory access only if its older-viewer benefit
  justifies the maintenance cost; AIS v3 remains authoritative.
- [x] Support thin/headless clients such as LibreMetaverse: advertise the
  per-region `FetchInventoryDescendents2` / `FetchLibDescendents2` capabilities
  and make the HTTP asset-fetch capabilities LMV-compatible
  (see `tools/testclient/README.md`). Server-side baking largely removes the
  appearance dependency on these. Live acceptance 2026-07-26: an LMV v3.1.3
  bot on the cloud grid enumerated its Current Outfit through the descendents
  capabilities, fetched wearables and textures over the asset caps, completed
  a client-side bake, and rendered fully baked in Firestorm.
- [ ] Validate Halcyon/InWorldz LSL extensions without admitting OpenSimulator
  scripting extensions accidentally.
- [ ] Document import and migration tools separately from live legacy service or
  database compatibility.

### Physics and service extensions

- [ ] Promote the existing PhysX 5 adapter to an optional supported physics
  plugin after it passes the same production scenarios as Jolt.
- [ ] Stabilize versioned plugin contracts only for boundaries with demonstrated
  operational value.
- [ ] Define safe extension points for grid services without exposing region
  authority or script execution to untrusted in-process plugins.
- [ ] Maintain deterministic transfer and persistence contracts across every
  supported physics implementation.

### Release readiness

- [ ] Publish administrator, region-owner, creator, scripter, and contributor
  documentation appropriate to the supported feature set.
- [ ] Run sustained multi-region worlds with real viewers, scripts, attachments,
  vehicles, failures, upgrades, and restores.
- [ ] Resolve all release-blocking correctness, data-loss, permissions,
  crossing, security, and viewer-compatibility findings.
- [ ] Define the supported platform matrix, compatibility guarantees, upgrade
  policy, and long-term maintenance expectations for the first stable release.

## Phase 9: Modernized Communications Transport

The modern client-facing wire surface: REST bootstrap, a grid-anchored
notification channel, and a region-anchored session — replacing LLUDP,
capability HTTP, and long polling for the first-party client while legacy
viewers keep all three untouched. [CLIENT2.md](CLIENT2.md) is the
implementation companion, [CLIENT2-TRANSPORT.md](CLIENT2-TRANSPORT.md) records
the transport decision, and [ROADMAP2.md](ROADMAP2.md) keeps the detailed
sequence this summarizes.

### Arrival and bootstrap

- [x] Serve the unauthenticated compatibility probe at `GET /v1/version`,
  reporting protocol versions, grid capabilities, and the welcome region.
- [x] Open world entry at `POST /v1/client/session`: destination resolution on
  the shared arrival logic, a session in the store viewer logins share, and a
  short-lived region-scoped ticket so the account token never reaches a region.
- [x] Enforce the grid-region protocol handshake in both directions, at
  registration and at renewal, so the probe's region claims rest on enforced
  leases.

### The grid channel

- [x] Serve the grid-anchored WebSocket at `GET /v1/client/channel` with
  first-message token auth, ping/pong, and error envelopes.
- [x] Deliver server-initiated notifications to connected users (system
  notices via the per-user delivery hub; best-effort, honestly reported).
- [x] Add the first store-and-forward notification kind: instant messages,
  stored before delivery, delivered live to open channels, and replayed in
  sent order on the next connection otherwise.
- [ ] Add the remaining store-and-forward kinds — inventory offers and
  friendship requests — which need producers and tables that do not exist
  yet.

### The region session

- [x] Decide the transport: TLS + WebSocket now on libwebsockets, with
  QUIC/WebTransport revisited when the RFC lands or measurements demand it
  ([CLIENT2-TRANSPORT.md](CLIENT2-TRANSPORT.md)).
- [x] Serve the region-session listener with ticket authentication (validated
  by a grid round trip — the signing secret never reaches a region), hello,
  heartbeat, and the region's public chat delivered server-initiated.
- [x] Advertise the session per region as data: registration reports the
  session endpoint, and world entry's capability manifest carries
  `transports` and `sessionURL`.
- [x] Carry scene traffic: avatar embodiment, object and avatar updates,
  movement, and client-to-region chat over the session
  ([CLIENT2-EMBODIMENT.md](CLIENT2-EMBODIMENT.md) milestone E1; crossings
  and appearance are its later milestones).
- [x] Carry a session avatar across a region border
  ([CLIENT2-EMBODIMENT.md](CLIENT2-EMBODIMENT.md) milestone E2): the region
  hands the client its continuation and the client re-enters next door,
  landing on the arrival point the grid resolved. Not atomic the way a
  viewer's handoff is, deliberately — the reasoning is in the design.
- [x] Narrow avatar traffic by interest for sessions: transforms flow only
  within draw distance, with arrival and departure emitted by a sweep that
  evaluates both parties' motion
  ([CLIENT2-EMBODIMENT.md](CLIENT2-EMBODIMENT.md)).
- [ ] Narrow it for viewers too. The mechanism is the same, but it changes
  what a legacy viewer sees (bodies killed and re-rezzed at the boundary),
  so it needs a **manual Firestorm regression pass** before shipping —
  viewers stay region-wide until then.
- [ ] Serve home-hosted regions through the call-home relay — an outbound
  connection to the grid in place of a listening socket and a certificate —
  with direct service preferred and verified by dial-back.
- [ ] Add WebTransport as a second advertised transport when its RFC
  publishes, per the version-floor rule.

## Phase 10: Modern Client Support

The grid/region back end for what the first-party client can do that a legacy
viewer cannot — served through negotiated region extensions so Firestorm never
sees a change ([ADR 0032](adr/0032-region-extensions-for-new-client.md)). The
**client itself** — the engine-neutral C++ core and its native and browser
frontends — is developed and tracked in its own repository, with its own
roadmap, status, and progress; phases 9 and 10 here are the server-side surface
it builds against.

- [x] Dress a session avatar for viewers: spawn seeds the server-side
  default-outfit bake and derives body geometry from it, so a viewer rezzes
  a properly shaped, clothed avatar rather than a default one
  ([CLIENT2-EMBODIMENT.md](CLIENT2-EMBODIMENT.md) milestone E3, viewer half).
- [x] Publish the region's water to session clients — live 2026-07-31. A
  viewer had always been told a height in `RegionHandshake`; the region never
  set one, so every region silently used the built-in 20 m, and a session
  client was told nothing at all and drew water wherever it guessed. Both
  paths now read `region.water_height` (default the same 20), and the hello
  states `water: {height}`: a height, not a surface, because the plane is
  flat and region-wide and the drawing is the client's business.
- [x] Measure what LayerData loses, after the client core saw rougher ground
  in Firestorm than in its own render of the same region and offered
  compression noise as a candidate. Ruled out by decoding the wire bytes with
  an independent decoder: 50 m of rise inside one 16×16 patch returns within
  0.12 m, a ±4 m one-metre-pitch checkerboard within 0.36 m, flat ground
  within a tenth of a millimetre. Both families are drawing the same heights,
  so the difference is in the drawing — which the terrain-surface item above
  is already about.
- [ ] Serve appearance *to* session clients, so they can render each other —
  the remaining half of E3, waiting on the asset formats below rather than
  on legacy texture-entry blobs.
- [ ] Store modern asset formats at rest — KTX2 textures, glTF meshes — with
  down-conversion serving legacy viewers the formats they expect; the mesh
  half is decided and scheduled as the Phase 4 pipeline of
  [ADR 0033](adr/0033-mesh-pipeline-gltf-canonical.md), and KTX2 becomes one
  more rendition kind on it.
- [ ] Mesh prims server-side, so the client renders one geometry pipeline and
  prim meshing logic is written once.
- [ ] Extend the session's capability manifest as extensions ship, keeping
  per-region capabilities data the client adapts to rather than negotiates.
- [ ] Add voice and modern presence surfaces appropriate to the new client.
- [ ] Build creator tooling on the modern pipeline: visual scripting and a
  modern content workflow ([ROADMAP2.md](ROADMAP2.md) Phase 6).
