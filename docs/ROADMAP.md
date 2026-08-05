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
  managers bypass parcel and estate restrictions. The Region/Estate Terrain tab is
  handled in full as of 2026-08-04: textures and elevations (`texturedetail`,
  `textureheights`, `texturecommit`) and then water height, the terrain edit
  limits, and region sun (`setregionterrain`). Region restart and estate
  kick/teleport-home admin actions remain.
  Three things learned by handling the second message. Its estate sun fields
  (params 6-8) are **hard-coded by the viewer** to Y/N/0 whatever the estate
  actually holds - indra says so itself, "*NOTE: this resets estate sun info" -
  so the region ignores them; honouring them would wipe an estate's sun
  configuration every time an operator touched the tab, and it would look exactly
  like success. The terrain raise and lower limits were **announced in
  `RegionInfo` and enforced nowhere**, so both fields read as settings while being
  decoration; they are now bounded against the region's baseline rather than the
  current height, because a bound against the current height is a rate and
  repeated edits walk straight past it. And re-sending `RegionHandshake`
  mid-session is **correct rather than disruptive** - see the retraction in
  CLIENT2-EMBODIMENT.md; the viewer diffs the composition and refreshes the
  Region/Estate floater, and an earlier decision here not to re-send it is what
  made an operator reopen the form to stale values. Live Firestorm acceptance on the Sandbox Region
  (2026-07-25): the Region tab, Estate tab (My Estate / owner Jim Tarber), and
  Covenant tab populate correctly.
- [ ] Pin the walkable slope limit with a test. It is published to clients, feeds
  Jolt's `CharacterVirtual` max slope angle, and **nothing on this side asserts
  what it does** - which is why three documents here and a header in the client
  repository could all state the opposite of the code ("steeper ground holds but
  never grounds, so avatars slide") with nothing to contradict them. The client
  core measured it in-world on purpose-built faces at 57.5, 65 and 70 degrees:
  an avatar rests on 70 repeatably and cannot walk up 65, so the limit governs
  traversal and grounding is a contact test.
  **An attempt on 2026-08-05 was withdrawn rather than tuned.** A scenario using a
  tilted static box reported "not supported on steep ground" on both backends -
  the opposite of the in-world result - and the model is the likely reason: a box
  is not a heightfield, its contacts on a near-vertical face fall outside the
  character's supporting volume, and the scenario added no floor, so "not
  supported" may only mean it fell past the slab. Adjusting a test until it agrees
  with the answer already believed is the one thing that must not happen here, so
  it was reverted.
  **A second attempt, with a real heightfield ramp, was also withdrawn - and it
  established two constraints the next attempt needs.** First, **PhysX has no
  heightfield support**: the base `create_heightfield` returns 0, so a heightfield
  scenario belongs in a Jolt-only test rather than in `run_common_scenarios`, which
  runs every backend. That is not a compromise and needs no third verdict from the
  harness - engines matching is a **goal but not a guarantee**
  ([ADR 0015](adr/0015-physics-world-boundary.md)), so an engine-specific scenario
  is the ordinary answer and an adapter declining a capability is a legitimate
  state. I had treated the difference as a problem to work around, which is the
  wrong frame. Second, dropping the
  character on flat ground and walking it *to* the ramp makes the approach
  dominate the distance measured, so the discriminator reads walking rather than
  climbing; it has to start on the face.
  So the shape of a working version is known: a Jolt-only scenario, character
  placed on the face, asserting support on both sides of the limit and traversal
  only below it. Until it exists the behaviour is verified only by somebody
  walking on it, which is the state that let four documents be wrong at once.
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
  texture pipeline. The related gap found live 2026-07-29 — no RenderMaterials
  capability, so viewer materials edits silently did not persist — is **closed
  and verified end to end on 2026-07-31**: assigned in Firestorm, registered,
  written onto the named faces, persisted, and read back after a relog with the
  Normal and Specular pickers populated.

  The format is recorded from the wire rather than guessed. All seventeen LLSD
  key names were confirmed exactly as modelled, offsets and repeats scaled by
  10000. Three things measurement supplied that reasoning had not: definitions
  arrive wrapped in a `FullMaterialsPerFace` array of `{Face, ID, Material}`
  entries where `ID` is the *object's* local id, so the request tells the server
  which face to write the material id onto; the same definition arrives once per
  face rather than once with a face mask, which is why identity by content
  matters; and a viewer resolves a face's material by GETting all of them on
  login rather than querying specific ids — the id-query path is implemented and
  has never been exercised.

  Three defects on the way there, each caught by instrumentation rather than by a
  test, and each looking exactly like success. The capability was advertised and
  its path parsed but it was missing from the dispatch gate, so four PUTs
  answered 404 and read as ordinary traffic. The envelope was then parsed *as* a
  definition, registering the all-default material and answering 200 — which is
  why that material's id is now a golden value in the tests, since seeing it in a
  log means nothing was read. And the definitions were stored while nothing
  referenced them, because writing the id onto the named face had never been
  implemented; a TextureEntry codec that edits rather than only builds was the
  missing piece. The unknown-key warning exposed the second and a relog exposed
  the third.
  Texture serving was fixed 2026-07-31: the rule that a viewer gets the
  derived JPEG2000 rather than the canonical PNG was implemented on the older
  GetTexture capability only, and Firestorm fetches through ViewerAsset, which
  special-cased `mesh_id=` and returned canonical bytes for everything else.
  Every server-side check passed — right id, 200, sound rendition — while the
  viewer held its grey placeholder. Two capabilities that had to agree, made
  to agree by duplication, is the defect worth remembering; the request is now
  folded into one shape where it is parsed. The parsers were hoisted into
  `capability_paths.{h,cpp}` with tests on 2026-07-31, including one asserting
  every seeded capability is reachable at the path its own handler parses.

  **What actually gates the PBR tab, measured 2026-08-04.** The operator found
  Firestorm's PBR tab crossed out — "a big X through all texture fields" — and a
  note here said the fix was advertising `PBREnabled`/`GLTFEnabled` in
  `SimulatorFeatures`. That was wrong twice over. The flag names this viewer
  actually reads are `PBRTerrainEnabled`, `PBRMaterialSwatchEnabled`, and
  `PBRTerrainTransformsEnabled`, and the gate on *applying* a material is not a
  flag at all: `LLMaterialEditor::applyToSelection` refuses with "Not connected
  to materials capable region, missing ModifyMaterialParams cap", and
  `LLGLTFMaterialList::modifyMaterialCoro` posts overrides to that capability.
  Read out of the shipped 51 MB viewer binary's string table, which is evidence
  about the build the operator is running rather than about a source tree.

  So M3 is three capabilities, not a flag: `ModifyMaterialParams` for per-face
  overrides, and `UpdateMaterialAgentInventory` /
  `UpdateMaterialTaskInventory` for saving a material asset. The flags stay
  false until those exist, per the rule stated on `SimulatorFeatures` itself —
  advertising a feature the region does not implement produces controls that
  silently do nothing, which is precisely the class of defect this milestone
  already supplied three of.
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
  The first prerequisite is done (2026-07-31): the four layers are now
  1024×1024 PNG canonicals with the JPEG2000 derived, on new asset ids, and
  the region's asset importer accepts PNG and JPEG so a seeded texture's
  canonical form can be the modern one. The replacements are generative-AI
  upscales of the Linden originals, so CC BY-SA 3.0 follows them and the
  licence record says so. Two findings from doing it: the importer treated the
  file extension as the test of what is an asset when the real test is a UUID
  filename (adding PNG swept in the heightmap sources and crash-looped every
  region), and **the derived JPEG2000 is now ~2 MB per layer, 8 MB of ground
  per viewer login, against 24 KB for all four before** — because `encode_j2c`
  is lossless while every terrain texture a viewer has ever loaded was lossy.
  The canonical should stay lossless; the rendition wants a rate.
  The second prerequisite is done (2026-08-04): textures and elevations are
  per-region operator state, set from the viewer's own Region/Estate → Terrain
  tab and persisted. The viewer was already sending it — `texturedetail`,
  `textureheights`, `texturecommit` on Apply — and the region dropped all three
  silently, so the tab could be filled in with no effect whatever. They are now
  staged and applied together, mirroring the viewer's own stage-stage-commit
  sequence rather than applying each as it arrives. Two decisions worth
  recording: a stored row is distinguishable from no row, because "never
  touched" should follow a change of defaults and "set to exactly the defaults"
  must not; and an already-connected viewer keeps the terrain it was handed at
  login, since `RegionHandshake` is the only message carrying these and
  re-sending it mid-session restarts more viewer region state than a texture
  change warrants.
  **The elevation semantics flipped twice in one day and the second flip was the
  correction.** The protocol's field names say start height and height range, and
  that is what they mean: the viewer computes
  `t = clamp((h + noise − start) * 4 / range, 0, 3)` and blends the four layers by
  `t`, so boundaries sit at `start + 0.125/0.375/0.625 × range`. That was published
  first, then retracted in favour of absolute low/high on the strength of the
  viewer's *dialog text* — which claims the low value is the maximum height of
  texture 1 and describes a model the viewer's own renderer does not implement.
  An operator's screenshot settled it: sand at 22 m on a region set to 20 and 60,
  where `t` is 0.13 and layer 1 is almost pure. The wrong reading survived a day
  because the vendored viewer source was in this repository the whole time and the
  UI copy was believed over `llvlcomposition.cpp`. Names restored to
  `startHeight`/`heightRange`, the published `selection` rule now states the
  arithmetic, and `layer_composition_value` is asserted against the screenshot's
  own numbers so the boundaries cannot drift again.
  Verified from both ends, 2026-08-04. The client core probed the grass layer it
  had fetched and rendered: 1024x1024, **97522 distinct colours**, mean r101 g132
  b44. Probing the source PNG on the operator's disk gives 1024x1024, **97522**,
  mean 101.452 / 131.833 / 43.623 - the same distinct count exactly, and the
  channel means differing only by their rounding against this side's truncation.
  Two codebases sharing no code, measuring opposite ends of import, vault,
  rendition selection, and serving, agreeing on a 97522-way fingerprint: the
  canonical PNG path is lossless in fact and not merely by intent. It also
  settles a flat-looking render as distance and mipmapping at a grazing angle
  rather than a flat image.
  **The blend contract closed itself, and a published field had to be retired for
  it (2026-08-05).** Once the layer rule was stated correctly the crossfade stopped
  being a separate quantity: neighbouring layers blend linearly between their
  peaks, so a transition is `heightRange / 4` wide and follows from `selection`.
  `blendMetres` and `region.terrain_blend_tenths` were invented while the model was
  believed to be two absolute bounds, where a width genuinely was independent —
  under the real arithmetic the field contradicted the rule published beside it,
  saying 2 m on a region whose rule gives 15. Both are gone, and the greeting
  asserts no blend key of any kind so neither returns without a thought.
  What stays unpublished is the viewer's **noise term** — a two-octave turbulence
  sum added to the height before `t` is computed, perturbing every boundary by a
  few metres in a pattern never reproduced outside Linden. That is a fact about the
  ground rather than a gap in the contract: a client matching `selection` exactly
  still differs from a viewer, and knowing why is the useful part.
- [x] Close the texture pipeline's asymmetry — live 2026-07-31. Mesh converted
  both directions but textures only converted modern to legacy, so every texture
  created in Firestorm was canonically JPEG2000 and invisible to the first-party
  client, which refuses that format by rule. `png-texture` is the reverse
  rendition: decode the canonical JPEG2000, re-encode as lossless PNG, and serve
  it from the session asset route as a derived representation. Verified against
  the operator's own normal and specular maps, uploaded through Firestorm that
  evening: both served as `image/png` where the canonical is `ff4f`. It recovers
  the stored pixels, not the detail the viewer's uploader discarded — it
  downsizes anything over 1024 and encodes lossily — so uploading through the
  client remains the better path for new art.
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

### Known hazards on the current deployment

- [ ] **Nothing watches free disk space, on a host that is the only home for
  everything.** The OVH box runs the grid, the API, the conversion worker and
  all four regions; measured 2026-07-31 at 96 GB total, 9.9 GB used, 86 GB free,
  11 GiB RAM, 6 cores. Headroom is comfortable today, so this is not urgent —
  but a process that fills the disk takes the whole grid with it, and the first
  sign would be regions failing rather than a warning. Recorded on its own
  merits rather than as a footnote to any particular build-tooling question:
  the gap exists whether or not another toolchain is ever installed there.
  A free-space threshold in the region's health reporting is the obvious cheap
  answer; it is the operator's call whether to spend the change on it.
  Related: CPU is emphatically *not* the constraint — a full cold build of a
  sibling project pinned to one job cost the regions a mean 1.9 ms/s of
  scheduling delay with an 11 ms/s peak, and its link steps were cheaper than
  its compile phase.

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

- [ ] A canonical avatar body. **Blocked on content licensing, not on protocol
  work, and deliberately without a milestone.** The client core drew avatars as
  capsules and asked the grid for a body; checking gave three noes, of which this
  is the one that does not resolve with code. The legacy body mesh and skeleton
  ship *inside the viewer* (96 files in Firestorm's `character/`), so they are
  Linden viewer content under the viewer's licence and the obvious conversion is
  closed. A default body is therefore **content authoring, not code** — a grid
  feature waiting on art. The terrain layers are the precedent: the first attempt
  was a generative upscale of Linden artwork, which carried CC BY-SA into the
  ground, and it took re-sourcing from CC0 to get clear.
  **Ruth2 and Roth2 examined, 2026-08-04** (operator's suggestion, relayed by the
  client core, verified here against the repository rather than a summary). They
  are real, current (Ruth2 v4, Roth2 v2), mesh, and distributed as Collada `.dae`
  for legacy viewer upload — **no glTF**. The licence file assigns components
  separately: mesh body parts **AGPL** (Shin Ingen 2018, Ada Radius 2020), the UV
  map **CC-BY, Linden Lab**, button meshes **CC-BY-3.0** (Serie Sumei), rig
  components **CC-BY-3.0** (Machinimatrix.org).
  Three things follow, and they are considerations rather than a decision — this
  is not legal advice and the choice is the operator's.
  1. The AGPL clause that matters here is the one about work "made available in a
     service", because converting to glTF is plausibly a modification and it is
     **the grid** making the result available, not the client. That is
     dischargeable — publish the converted asset and the conversion pipeline —
     and it would be an obligation taken knowingly, which is the distinction the
     terrain episode turned on. But AGPL is written for software, so what counts
     as Corresponding Source for a mesh is genuinely unclear (the `.blend`? the
     Collada?), and an obligation nobody can cleanly discharge is worse to accept
     than a strict one that is precise.
  2. It is **not a Linden-free path**: the UV map is CC-BY from Linden Lab. With
     an explicit grant this time rather than by accident, which is the important
     difference from the upscale, but the component is still there.
  3. **The rig claim does not currently verify upstream, and it is the part that
     matters most** — a body can be authored, a skeleton is the hard part to
     originate. Ruth2 attributes CC-BY-3.0 to Machinimatrix for the rig
     components. Machinimatrix's *current* licence page (Avastar 2.92) says "The
     Avastar source code is distributed under the Blender compatible GPL licence"
     and "All parts of the products which are not explicitly marked as GPL are
     not distributed according to GPL license terms and may not be redistributed",
     mentions CC-BY-4.0 for the Avamesh developer kit, and **does not mention
     CC-BY-3.0 at all**. The pages that reportedly carried the CC-BY-3.0 wording
     now redirect to a legacy landing page with no licence text; an archived
     snapshot exists (2026-01-23) but could not be read from here. So the grant
     may well have been made and later restated — the Ruth2 authors presumably
     relied on something — but it is **unverified against the rights holder** as
     of this date. Confirm from the archive or from Machinimatrix directly before
     relying on it.
  **MakeHuman examined next, 2026-08-04, and the chain verifies at every step**
  (client core generated the candidates; every claim below re-checked here against
  the upstream repository rather than the summary). Two bodies, MPFB 2.0.17 under
  Blender 4.5, one macro axis apart: 36,972 triangles and 163 joints each, and
  distinct files by digest rather than by claim.
  1. The skeleton **declares its own licence**. `data/rigs/default.mhskel` carries
     `license: "CC0"` and `copyright: "(c) 2020 Data Collection AB, Joel Palmius,
     Jonas Hauquier"` in its own metadata — read from the repository. That is the
     asset speaking for itself, which is exactly the standard Ruth2's rig failed:
     there a downstream project summarised someone else's terms and the terms did
     not confirm.
  2. `LICENSE.md` enumerates five CC0 asset categories — base mesh and proxies,
     targets and modifiers, textures, clothes, poses and expressions — and **does
     not name skeletons**. Verified, and recorded because it is the residual the
     client core flagged rather than let be discovered later. Silent, not contrary:
     the file's own declaration and the project's blanket asset statement both say
     CC0, and the enumeration simply omits the category.
  3. The code is AGPL and that is the tool, not the output. `LICENSE.md` says so
     itself: "no output from MakeHuman contains any trace of program logic" and
     "no limitation on what you can do with this combined output". A generator's
     licence no more covers its exports than an image editor's covers an image.
  Two things this side measured that matter for adoption.
  **The exported GLBs carry no licence metadata at all** — `asset.copyright` is
  absent from both. Worth fixing before adoption rather than after: the whole
  reason the skeleton's claim is strong is that the file speaks for itself, and an
  export that declares nothing loses that property on the way into a
  content-addressed vault. The terrain precedent is a licence record beside the
  assets (`assets/region/library/terrain/LICENSE.md`), and these would want both
  that *and* `asset.copyright` set at export.
  **The height is settled from the files.** Both report a Y extent of **1.6946 m**
  from their POSITION accessor bounds with identity node transforms, which agrees
  with Blender for both bodies; a reported 1.850 m for one of them is not a reading
  these files support. Origin sits at the feet within 2.7 cm (min Y −0.0267 /
  −0.0263, max Y 1.6679 / 1.6683), so placing the origin exactly on the ground
  sinks the soles by that much — which is the number anything positioning feet
  precisely needs, and the resolution to the client core's open question.
  **The skeleton is the one choice that matters, and it is one choice rather than
  two (client core correction, 2026-08-04).** An earlier reading here was that a
  viewer's fixed skeleton and a client's free one meant the two families need
  different bodies. That inverts: glTF binds skin joints by node *index*, never by
  name - verified in these files, where `skins[0].joints` is a list of node indices
  and `JOINTS_0` indexes into it, with names carried only on the nodes. So a client
  that draws arbitrary skeletons is unconstrained while a viewer uses its own and
  no other. The constraint is one-sided, and **a body re-rigged to the viewer's 71
  named joints serves both families from one asset**; re-rigged to anything else it
  serves only the first-party client.
  The expected skeleton is therefore published now, ahead of M4, in the mesh
  acceptance policy as `skeleton` and `skeletonJoints` - listed under
  `forwardLooking` while `rigged` is false. It is the one fact a re-rig has to
  target and no amount of server work recovers from getting it wrong.
  **The static previews were bundled as Library objects and then withdrawn the
  same night (2026-08-05), because they did not contain bodies.** MakeHuman's base
  mesh carries helper geometry - a skirt-shaped shell from waist to ankles, hair
  planes, a face mask - plus a locator cube per joint, all hidden behind a MASK
  modifier that Blender's glTF exporter does not apply unless told to. So it was
  invisible in Blender and present in the file: hundreds of disconnected shells,
  and the skirt hid every difference between the two bodies while the base mesh's
  crotch showed beneath it. The operator looked at them and reported both as
  identical clothed female figures with male genitalia. All three observations were
  right.
  **What makes this worth recording is how thoroughly it was verified first.**
  Between the two of us: triangle count, joint count, height from the POSITION
  accessor bounds, node transforms, licence chain at each upstream source, both
  files read through a real glTF reader, and an explicit check that they were not
  the same file twice - distinct digests, 87 per cent of overlapping bytes
  differing. Every one measured a property the fault did not disturb, and the last
  one was aimed at exactly the right suspicion and landed one layer away from it,
  which retired the suspicion. Measured as geometry rather than as bytes, the two
  bodies differ by **0.4 mm at every vertex** - a uniform rigid offset, identical
  extents - so they were one body twice, with gender sitting in morph targets that
  a default-state renderer ignores.
  **The trap generalises to the vault and is the reason this is here rather than
  only in a commit message**: a glTF file can be valid, conformant, and carry a
  shape nobody will see, because the shape is in morph targets rather than in
  POSITION. Any ingestion path that judges a mesh by POSITION alone will accept it.
  **One more defect from the preview, and it was mine rather than the content's.**
  The bodies rezzed short and wide. Mesh geometry is normalized to a unit domain
  and a viewer scales it by the prim, so a wrapper's `scale` *is* the object's size
  - `declared_world_bounds`, "the ONE bounds definition", and the upload path
  honours it. My hand-authored wrapper set 1,1,1. The operator stretched one back
  by eye and landed on 0.4365 and 1.7266 against true extents of 0.476 and 1.729,
  which is what confirmed the cause over a wrong-axis one: an axis error would have
  needed a factor of 4.4 and would have read as lying down.
  So the answer to "who carries a body's real dimensions" was already decided and
  documented; it was simply not applied by the one path that bypassed the code.
  Recorded at the importer's extension list, where the next person bundling mesh
  content will be standing.
  Still not adopted, and still the operator's decision. Accepting a canonical body
  commits the grid to a licensing position, and publishing appearance is the
  prerequisite before any client can be told to wear one — a body only one client
  can draw is the partial the client core has refused three times.
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
- [x] Bound the terrain alignment invariant by what Jolt quantizes, after the
  operator's carved 24 m walls made it fail on Gamma (2026-07-31) against a
  flat 1 cm tolerance while the ground was fine. Jolt stores each sample as 8
  bits within its own 2×2 block's range, measured over a 3×3 span, so a block
  beside a cliff inherits the cliff's range. The bound is now each sample's own
  quantization step — tighter almost everywhere (flat ground 1 cm → 2 mm, where
  a real displacement could previously hide) and roomy only where the storage
  needs it. All four regions run at 33–39% of allowance; Beta, entirely flat,
  deviates by exactly zero, which is the measurement that rules out any cause
  other than quantization.
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
