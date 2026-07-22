# HomeWorldz Roadmap

This roadmap describes the major implementation sequence for HomeWorldz. It is
organized at three levels: phases, milestones within each phase, and major work
items within each milestone. [`PLAN.md`](PLAN.md) remains the detailed
engineering checklist, while [`FEATURES.md`](FEATURES.md) records intentional
product differences and the ADRs record architectural decisions.

Checkboxes describe the present state, not a promise of a release date. A
milestone is complete only when its automated tests and applicable Firestorm
acceptance tests pass.

## Progress snapshot

Updated 2026-07-21. These bars are effort-weighted engineering estimates, not
simple checkbox ratios. Later scripting, crossings, social systems, security,
recovery, and scale items are substantially larger than many completed viewer
protocol tasks. Percentages are deliberately approximate and should be revised
when scope or implementation evidence changes.

<label class="roadmap-overall-progress">
  <span>Overall progress</span>
  <progress data-color="primary" max="100" value="27">27%</progress>
  <strong>27%</strong>
</label>

| Phase | Progress | Estimate |
| --- | --- | ---: |
| 1. Functional Single-region World | <progress class="roadmap-phase-progress" data-color="primary" max="100" value="98" aria-label="Phase 1 progress: 98%">98%</progress> | 98% |
| 2. Connected Multi-region World | <progress class="roadmap-phase-progress" data-color="primary" max="100" value="68" aria-label="Phase 2 progress: 68%">68%</progress> | 68% |
| 3. Interactive Physical World | <progress class="roadmap-phase-progress" data-color="primary" max="100" value="39" aria-label="Phase 3 progress: 39%">39%</progress> | 39% |
| 4. LSL Scripting | <progress class="roadmap-phase-progress" data-color="primary" max="100" value="15" aria-label="Phase 4 progress: 15%">15%</progress> | 15% |
| 5. Social and Creator Platform | <progress class="roadmap-phase-progress" data-color="primary" max="100" value="9" aria-label="Phase 5 progress: 9%">9%</progress> | 9% |
| 6. Reliable Operations and Distribution | <progress class="roadmap-phase-progress" data-color="primary" max="100" value="12" aria-label="Phase 6 progress: 12%">12%</progress> | 12% |
| 7. Scale, Compatibility, and Ecosystem | <progress class="roadmap-phase-progress" data-color="primary" max="100" value="2" aria-label="Phase 7 progress: 2%">2%</progress> | 2% |

The overall estimate is weighted by expected effort and therefore is not the
arithmetic mean of the phase percentages. The binary checkboxes below remain
the acceptance record; partially implemented work contributes to these bars
but stays unchecked until its complete wording is satisfied.

The phases are parallel workstreams, not completion gates. Work may advance in
any phase when it delivers useful capability or evidence; dependencies constrain
individual tasks rather than requiring an earlier phase to be complete. Current
active work spans connected regions, the interactive physical world, and the
Falcon LSL scripting foundation.

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
- [ ] Bake avatar appearance **server-side** — the region composites the bake
  layers from a user's worn wearables and serves baked textures — so thin or
  headless clients (e.g. LibreMetaverse) rez correctly with no client-side
  baking. Reference: SL's open-source server-side appearance and LMV's
  permissive implementation; C++ compositing with OpenJPEG for the JPEG2000
  encode.
- [ ] Broadcast `KillObject` and clear presence/People-list state when a viewer
  logs out or disconnects; today a departed avatar lingers rezzed in others'
  views (no departure is detected).

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
  landmark, notecard, gesture, and LSL-source creation and updates; and task
  notecard and script updates. Phase 4 now compiles and executes the supported
  Falcon language subset when a script enters or is saved in object contents.
- [ ] Complete Firestorm creation, editing, playback, object-contents,
  restart, and relog acceptance for those fundamental content types.

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
- [ ] Implement landmark resolution, home location, and teleport routing.
- [ ] Add region and parcel search sufficient to find and reach destinations.
- [ ] Show friends and authorized users useful presence and location without
  leaking restricted information.

### Inventory asset durability

- [ ] Implement the grid asset vault: a durable, replica-only blob store that
  never originates assets, never hosts agents, and is never in the viewer data
  path (ADR 0026).
- [ ] Enforce the vault invariant grid-side: commit an inventory item only
  after the vault holds verified bytes for its referenced asset, with
  write-through ingest on upload, take-to-inventory, give, and purchase.
- [ ] Treat region copies of vault-held assets as an evictable cache and
  scene-only assets as region-owned; demote region-to-region fetch to an
  optimization with the vault as the always-available fallback location.
- [ ] Backfill existing inventory-referenced assets into the vault from live
  registered locations and report assets that are already unfetchable.
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

### Parcels and local authority

- [ ] Implement parcel geometry, ownership, access, landing points, media,
  environment, and object accounting.
- [ ] Enforce build, rez, entry, script, damage, push, and object-return policy
  at authoritative boundaries.
- [ ] Implement estate and region settings needed for terrain, access, maturity,
  restart, and emergency administration.
- [ ] Apply permissions recursively and consistently to linksets, object
  contents, attachments, and inventory transfers.

## Phase 4: LSL Scripting

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
  and versioned HomeWorldz bytecode compiler for that full supported language.
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

### Events and region integration

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
  and document intentional HomeWorldz differences.
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

## Phase 5: Social and Creator Platform

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

### Content creation and inventory breadth

- [ ] Complete viewer building workflows for linksets, materials, mesh, sculpt,
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

- [ ] Define whether credits remain display-only or become a transferable grid
  balance before implementing paid behavior.
- [ ] If enabled, implement auditable balances, idempotent transactions, object
  sales, parcel payments, gifts, refunds, and operator controls.
- [ ] Keep texture uploads free and preserve a useful no-economy deployment
  mode.
- [ ] Treat external payment processing and marketplace integration as separate,
  explicitly approved security projects.

## Phase 6: Reliable Operations and Distribution

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

## Phase 7: Scale, Compatibility, and Ecosystem

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
- [ ] Support thin/headless clients such as LibreMetaverse: advertise the
  per-region `FetchInventoryDescendents2` / `FetchLibDescendents2` capabilities
  and make the HTTP asset-fetch capabilities LMV-compatible
  (see `tools/testclient/README.md`). Server-side baking largely removes the
  appearance dependency on these.
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
