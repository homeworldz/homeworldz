# HomeWorldz Implementation Plan

[ARCHITECTURE.md](ARCHITECTURE.md) is authoritative. HomeWorldz is a new
implementation; legacy projects are protocol and behavioral references rather
than runtime dependencies.

## Milestone 0: Foundation

- [x] Establish the C++20 and Go monorepo layout.
- [x] Define initial OpenAPI contracts and common error responses.
- [x] Define the external Postgres requirement and add initial schema migrations.
- [x] Add a cross-platform Go grid bootstrap and separate region-local bootstrap.
- [x] Establish INI configuration files with environment-variable overrides.
- [x] Implement `/ping`, `/ready`, and `/version` in both services.
- [x] Add Windows and Linux CI for builds, tests, and migrations.
- [x] Add generated or validated C++ and Go API models.
- [x] Add service-token authentication, request IDs, and structured logging.
- [x] Add contract tests shared by grid handlers and the region client.

## Milestone 1: Grid And Region Core

- [x] Implement region registration, lease renewal, expiry, and discovery.
- [x] Implement development users, login sessions, and session validation.
- [x] Implement presence and stale-presence cleanup.
- [x] Add the authoritative scene model and fixed-step simulation loop.
- [x] Add the region-to-grid HTTP client and registration lifecycle.
- [x] Add SQLite local scene metadata and atomic snapshots.
- [x] Add content-addressed local asset blobs and viewer UUID mappings.
- [x] Add integration tests using disposable Postgres.

## Milestone 2: Physics Gate

- [x] Define the engine-independent physics world interface.
- [x] Implement Jolt and PhysX 5 evaluation adapters.
- [x] Add common avatar, terrain, stacking, impulse, vehicle, handoff, restore,
  and load scenarios.
- [x] Record timing, memory, jitter, tunneling, replay drift, restore accuracy,
  and adapter complexity on Windows and Linux.
- [x] Choose Jolt unless a required acceptance scenario fails; otherwise choose
  PhysX 5 and record the decision.

## Milestone 3: Firestorm Vertical Slice

- [x] Pin the supported Firestorm version and capture the minimum login flow.
- [x] Implement login response and destination-region resolution.
- [x] Implement viewer UDP circuits, packet codecs, acknowledgements, throttles,
  and timeouts.
- [x] Implement region handshake, seed capability, and required event delivery.
- [x] Decode avatar movement, flying, jumping, and camera inputs and persist the
  resulting provisional avatar state.
- [x] Stream continuous authoritative avatar transforms and flight state back
  to Firestorm.
- [x] Add a Jolt-backed avatar capsule with terrain walking, grounding, flight,
  and collision-safe movement.
- [x] Implement nearby chat, terrain, and static object updates.
- [x] Implement local texture and asset fetch.
- [x] Verify persistence, restart, disconnect, and reconnect.

## Later Milestones

- [x] Add inventory skeleton/item fetch capabilities and seed a valid default
  body-parts outfit.
- [x] Add automatic legacy avatar bake upload, UDP delivery, and persistent
  wearable-hash cache lookup.
- [x] Add AIS v3 inventory mutation, folder management, texture-backed
  non-system item creation, Current Outfit links, Trash lifecycle operations,
  and the narrow legacy UDP move adapters required by supported Firestorm.
- [x] Add a read-only, grid-owned system inventory library with stable owner,
  root, folders, items, bundled-asset provenance, and AIS v3 fetch support;
  expose legacy reads only when their compatibility value justifies the cost.
- [x] Asset upload, authorization, replication, and federation lookup.
- [x] Match OpenSimulator/Halcyon lossless PNG terrain-image semantics,
  including exact region dimensions, north/south row conversion, and HSL
  lightness mapped to the 0-to-128-metre height range; reject JPEG terrain.
- [x] Object rez, edit, take, delete, permissions, and persistence.
- [x] Add persistent nonphysical linksets with root and child transforms,
  whole-object and Edit Linked scaling, duplication, take, take-copy, return,
  derez, inventory round trips, and static child collision.
- [x] Add object contents inventory and its permissions, mutation, copy, derez,
  return, and inventory round-trip lifecycle.
- [ ] Represent physical linksets as compound physics bodies rather than
  independent self-colliding child bodies.
- [x] Define the engine-independent script runtime boundary.
- [x] Select a single-threaded custom C++ bytecode interpreter for Second Life
  LSL plus Halcyon/InWorldz extensions, excluding OpenSim extensions.
- [ ] Implement scripting after avatar synchronization, basic avatar physics,
  and the attachment/crossing transaction skeleton are accepted.
- [x] Teleport avatars between registered Regions with an authenticated,
  idempotent handoff and durable last-location login.
- [ ] Cross-region avatar and object handoff at Region borders.
- [ ] Evaluate and implement exactly three OpenSimulator-compatible region
  sizes: 1x1 (256 metres), 2x2 (512 metres), and 4x4 (1024 metres), without
  coupling terrain, physics, or scene storage to the initial 1x1 size.
- [ ] Map, search, groups, permissions, and administration.
- [ ] Backups, restore drills, reconciliation, audit logs, metrics, and hardened
  deployment.

## Public Interfaces

- `/ping` proves that the process can answer HTTP without dependency checks.
- `/ready` reports whether required dependencies and storage are usable.
- `/version` reports build, API, and schema compatibility information.
- `/api/v1/regions` registers, renews, deregisters, and discovers regions.
- `/api/v1/sessions` creates and validates viewer sessions.
- `/api/v1/presence` updates and queries online presence.
- `/api/v1/transits` coordinates idempotent, durable avatar handoff state.
- `/api/v1/assets` records immutable asset provenance and region locations;
  authenticated region endpoints serve and explicitly replicate verified blobs.
- Region-local capabilities provide viewer events and asset access.
- An engine-independent C++ physics boundary owns bodies, characters, contacts,
  queries, simulation steps, and transferable physical state.

Default development ports are `42000/tcp` for grid HTTP, `42001/tcp` for
region HTTP, and `42002/udp` for the future viewer circuit. Ports
`42010-42099` are reserved for additional local region instances and services.
Every port remains configurable in deployment.

## Acceptance Policy

Every completed item requires automated tests where practical. Viewer-facing
milestones additionally require a repeatable Firestorm smoke test pinned to an
explicit viewer version. `/ping` remains successful during dependency outages;
`/ready` returns unavailable until the service can accept useful traffic.

## Defaults

- Use C++20 and CMake for the region server and Go for one initial grid binary.
- Use HTTP/JSON with OpenAPI, Postgres centrally, and SQLite plus filesystem
  blobs in each region.
- Use Jolt as the selected v1 physics backend while retaining the PhysX lab adapter.
- Defer legacy inventory breadth, scripting, editing, federation, and
  multi-region transfer until after the first playable region.
