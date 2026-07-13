# HomeWorldz Implementation Plan

[ARCHITECTURE.md](ARCHITECTURE.md) is authoritative. HomeWorldz is a new
implementation; legacy projects are protocol and behavioral references rather
than runtime dependencies.

## Milestone 0: Foundation

- [x] Establish the C++20 and Go monorepo layout.
- [x] Define initial OpenAPI contracts and common error responses.
- [x] Add Postgres development infrastructure and initial schema migrations.
- [x] Implement `/ping`, `/ready`, and `/version` in both services.
- [ ] Add Windows and Linux CI for builds, tests, and migrations.
- [ ] Add generated or validated C++ and Go API models.
- [ ] Add service-token authentication, request IDs, and structured logging.
- [ ] Add contract tests shared by grid handlers and the region client.

## Milestone 1: Grid And Region Core

- [ ] Implement region registration, lease renewal, expiry, and discovery.
- [ ] Implement development users, login sessions, and session validation.
- [ ] Implement presence and stale-presence cleanup.
- [ ] Add the authoritative scene model and fixed-step simulation loop.
- [ ] Add the region-to-grid HTTP client and registration lifecycle.
- [ ] Add SQLite local scene metadata and atomic snapshots.
- [ ] Add content-addressed local asset blobs and viewer UUID mappings.
- [ ] Add integration tests using disposable Postgres.

## Milestone 2: Physics Gate

- [ ] Define the engine-independent physics world interface.
- [ ] Implement Jolt and PhysX 5 evaluation adapters.
- [ ] Add common avatar, terrain, stacking, impulse, vehicle, handoff, restore,
  and load scenarios.
- [ ] Record timing, memory, jitter, tunneling, replay drift, restore accuracy,
  and adapter complexity on Windows and Linux.
- [ ] Choose Jolt unless a required acceptance scenario fails; otherwise choose
  PhysX 5 and record the decision.

## Milestone 3: Firestorm Vertical Slice

- [ ] Pin the supported Firestorm version and capture the minimum login flow.
- [ ] Implement login response and destination-region resolution.
- [ ] Implement viewer UDP circuits, packet codecs, acknowledgements, throttles,
  and timeouts.
- [ ] Implement region handshake, seed capability, and required event delivery.
- [ ] Implement avatar movement, flying, jumping, and camera state.
- [ ] Implement nearby chat, terrain, and static object updates.
- [ ] Implement local texture and asset fetch.
- [ ] Verify persistence, restart, disconnect, and reconnect.

## Later Milestones

- [ ] Inventory metadata and viewer inventory capabilities.
- [ ] Asset upload, authorization, replication, and federation lookup.
- [ ] Object rez, edit, take, delete, permissions, and persistence.
- [ ] Script runtime boundary and implementation selection.
- [ ] Teleport and cross-region avatar/object handoff.
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
- Region-local capabilities provide viewer events and asset access.
- An engine-independent C++ physics boundary owns bodies, characters, contacts,
  queries, simulation steps, and transferable physical state.

## Acceptance Policy

Every completed item requires automated tests where practical. Viewer-facing
milestones additionally require a repeatable Firestorm smoke test pinned to an
explicit viewer version. `/ping` remains successful during dependency outages;
`/ready` returns unavailable until the service can accept useful traffic.

## Defaults

- Use C++20 and CMake for the region server and Go for one initial grid binary.
- Use HTTP/JSON with OpenAPI, Postgres centrally, and SQLite plus filesystem
  blobs in each region.
- Keep Jolt as the provisional physics preference until the comparison gate.
- Defer complete inventory, scripting, editing, federation, and multi-region
  transfer until after the first playable region.
