# HomeWorldz Architecture

## Direction

HomeWorldz is a clean architecture effort for a Firestorm-first virtual world
server. The only protocol compatibility requirement is the viewer-facing
Second Life/OpenSim behavior needed by Firestorm and compatible viewers.

Internal Halcyon, OpenSim, WHIP, Aperture, MySQL, XML-RPC, protobuf, and C#/.NET
implementation details are references only. They do not need to be preserved
unless they are useful for understanding viewer behavior or existing content.

The current preferred split is:

- C++ for the region server.
- Go for central grid services.
- HTTP and JSON for internal service APIs by default.
- OpenAPI for API documentation.
- Postgres for central grid-service persistence.
- Region-local storage for scene and asset data.

## Goals

- Preserve practical Firestorm viewer compatibility.
- Support distributed region servers operated independently.
- Store assets primarily with the region servers that need them.
- Keep central services focused on identity, inventory metadata, presence,
  region registration, discovery, and coordination.
- Avoid preserving legacy internal protocols unless they are still the simplest
  useful option.
- Keep internal APIs easy to inspect, test, and debug.

## Non-Goals

- Preserving Halcyon or OpenSim internal service APIs.
- Preserving WHIP or Aperture protocols.
- Preserving the current MySQL schema.
- Using protobuf because Halcyon used protobuf.
- Using C#/.NET as an implementation constraint.
- Making the central grid services responsible for all asset blobs.
- Making gRPC a default before there is a measured need.

## Region Server

The region server should be implemented in modern C++ because it owns the
game/simulation-heavy parts of HomeWorldz:

- Firestorm/Second Life viewer protocol handling.
- Region scene graph and authoritative object state.
- Local terrain, parcel, object, script, and asset references.
- Local asset service and local asset persistence.
- Local region scene persistence.
- Physics engine integration and collision mirror.
- Avatar movement and region simulation ticks.
- Script runtime boundary.
- Interest management and viewer object updates.
- Cross-region handoff preparation.

The region server should not embed central account, inventory, or grid database
logic beyond local caches and service clients.

## Grid Services

Central grid services should be implemented in Go by default. These services are
ordinary distributed web services and do not need to share the region server's
C++ simulation stack.

Go is a good fit because it has a small runtime, strong standard networking
libraries, simple deployment, good Postgres support, and a language style that
keeps service code straightforward.

Grid services should own:

- Login and session creation.
- User identity and account metadata.
- Inventory metadata.
- Region registry and region capability discovery.
- Presence and online status.
- Map and search metadata.
- Permissions and groups.
- Asset federation metadata and lookup.
- Administrative APIs.
- Background reconciliation jobs.

Region registration uses renewable leases. A registration claims one grid
coordinate pair, returns a grid-generated UUID, and remains discoverable only
until its lease expires. Initial leases default to 60 seconds and accept values
from 10 through 300 seconds; expired rows no longer block a new registration at
the same coordinates.

When grid credentials are configured, the region registers during startup,
renews halfway through its lease, and deregisters during orderly shutdown. A
registration or renewal failure stops the region rather than leaving an active
simulation undiscoverable. The dependency-free socket transport is limited to
development `http://` URLs; deployed HTTPS will use a maintained TLS transport.

ADR 0024 defines persistent grid-owned region provisioning: UUID, unique name,
owner, coordinates, endpoints, enabled state, and a per-region credential hash.
A Region authenticates by UUID or unique name plus access key, fetches effective
configuration, and then acquires an online lease. Going offline does not delete
the provisioned record.

An authenticated Region can discover its provisioned cardinal neighbors
of any online region with `GET /api/v1/regions/{uuid}/neighbors`. The Grid
derives north, east, south, and west adjacency from persistent coordinates,
excludes diagonals, and returns canonical UUID, name, coordinates, 256-metre
extents, maturity, assigned or live endpoints, and explicit online state in
that deterministic order. A Region fetches and validates this topology at
startup, retains the snapshot in runtime state, and refreshes it every five
seconds so independently started or restarted neighbors converge without a
required start order. Offline neighbors remain visible as topology but are
never eligible teleport destinations. Border crossing is not enabled by
discovery alone;
until the crossing handoff exists, the simulation continues to contain entities
at every region edge.

Avatar teleports and crossings use the durable Grid-coordinated transaction in
[ADR 0025](adr/0025-idempotent-avatar-transits.md). Internal Region clients
prepare an immutable arrival record through `/api/v1/transits`; the destination
accepts it after creating provisional state and activates it only after the
viewer establishes its destination circuit. Activation moves the viewer
session's authoritative destination in the same PostgreSQL transaction.
Prepared or accepted transactions can be rolled back by either participating
Region, expire automatically, and use a monotonically increasing per-avatar
generation to reject stale handoff messages.

The Region answers authenticated viewer `MapBlockRequest` and `MapNameRequest`
UDP messages from that live topology snapshot. Replies currently cover the
Region itself and its cardinal neighbors, including coordinates, name, access,
water height, and local agent count. The initial package advertises a stable
HomeWorldz JPEG-2000 rendering of the default plateau rather than a null image
UUID, preventing unrelated cross-grid cache reuse.

Firestorm also consumes the OpenSim `map-server-url` simulator feature for
world-map imagery. Regions advertise the Grid's public `/map/` base URL, and
the Grid serves Firestorm's `map-{level}-{x}-{y}-objects.jpg` convention only
for tiles containing currently leased coordinates. Level 1 is one Region;
higher levels composite progressively larger powers-of-two areas into 256-pixel
JPEG tiles. For each live Region, the Grid reads a service-authenticated
256-by-256 little-endian float heightfield snapshot, applies
water/land/elevation coloring
and hill shading, and caches the rendered result for one minute. If a Region is
temporarily unreachable or returns invalid data, composition falls back to the
packaged plateau. North is rendered at the top. The helper URI remains the
general viewer-services URI and is not the map-image base.

The Region implements EventQueueGet as a non-blocking 20-second long poll.
Initial queued events return immediately; an empty response retains only the
accepted HTTP socket and deadline while the single Region thread continues its
UDP and simulation work. Logout and authenticated circuit replacement close
any held response for the superseded session.

Initial development accounts use normalized lowercase usernames and bcrypt
password hashes. Successful credential checks create random UUID sessions with
database-controlled expiry; internal clients validate or revoke those sessions
through service-authenticated grid APIs. Viewer-facing login can build on this
boundary without exposing password hashes to region servers.

Online presence is a grid-owned heartbeat record linking a user to an actively
leased region. Heartbeats become stale after 90 seconds, are hidden from lookup,
and are deleted during discovery; normal disconnects clear presence directly.

Postgres is the default durable store for grid services.

## Internal APIs

HomeWorldz internal APIs should start with HTTP and JSON.

This is intentionally simpler than gRPC/protobuf and keeps the first system easy
to inspect with common tools such as curl, browser dev tools, logs, and OpenAPI
clients.

Default API policy:

- Use HTTP plus JSON for region-to-grid APIs.
- Use OpenAPI for schema documentation.
- Use HTTPS in deployed environments.
- Use bearer tokens or signed service tokens for service authentication.
- Require bearer service tokens on grid routes under `/api/`; keep `/ping`,
  `/ready`, and `/version` available without credentials.
- Propagate or generate `X-Request-ID` values and include them in responses and
  structured JSON request logs without logging authentication credentials.
- Use explicit versioned paths for APIs that need compatibility guarantees.
- Use HTTP streaming for large transfers where plain request/response is not a
  good fit.
- Expose `/ping` for process liveness, `/ready` for dependency-aware readiness,
  and `/version` for build and compatibility information.
- Defer gRPC, protobuf, FlatBuffers, or a message bus until a concrete measured
  need appears.

Possible later reasons to add protobuf or gRPC:

- Very high-frequency region-to-grid calls.
- Complex bidirectional streaming.
- Strict generated cross-language schemas.
- Many independently maintained services.
- Binary payloads where JSON overhead becomes a measured problem.

Until one of those reasons is real, HTTP/JSON is the default.

Language-neutral operational cases under `api/contracts/` are consumed by both
the Go handler tests and the C++ region tests. These cases keep status codes,
response schema shapes, and cross-cutting headers aligned as both HTTP stacks
evolve.

## Configuration

HomeWorldz uses INI files under `config/`. Grid hosts use `grid.ini` and
`db.ini`; region hosts use `region.ini` and `grid.ini`. Files are separated by
operational ownership, and environment variables override file values for
service-manager and deployment integration. Configuration containing secrets is
not committed.

### Default Ports

- `42000/tcp`: grid HTTP API.
- `42001/tcp`: region HTTP API.
- `42002/udp`: reserved for the region viewer circuit.
- `42010-42099`: reserved for additional local region instances and future
  HomeWorldz development services.

All ports are deployment configuration rather than protocol constants.

## Physics And Spatial State

The physics engine is important, but it should not be the sole owner of region
state.

HomeWorldz owns authoritative scene state:

- Object identity.
- Persistence.
- Permissions.
- Ownership.
- Inventory references.
- Asset references.
- Viewer update state.
- Edit/build operations.

The initial scene core assigns region-local entity identifiers, tracks state
revisions, and advances velocity-derived transforms at a fixed 45 Hz. Catch-up
work is bounded to eight steps per host update to prevent a delayed region from
entering a simulation spiral. This loop remains independent of the selected
physics engine.

The physics engine owns a simulation mirror:

- Terrain collision.
- All non-phantom collidable static objects.
- Avatar controllers or avatar collision bodies.
- Dynamic physical objects.
- Vehicles.
- Constraints.
- Collision events.
- Sleep/wake state.

Objects marked phantom are excluded from physical collision, though they may
still exist in the scene graph and query systems.

Jolt 5.5 is the selected v1 physics engine. PhysX 5 remains behind the same
adapter boundary as an evaluated alternative. See [PHYSICS.md](PHYSICS.md) and
[ADR 0002](adr/0002-physics-evaluation.md) for the evaluation and decision.

## Asset Architecture

Assets should be primarily region-local.

Each region server should be able to store and serve the assets needed by its
region. Central services may track metadata and federation information, but they
do not need to store every asset blob.

The asset model should support:

- Region-local asset storage.
- Authorized asset fetch between regions.
- Authorized asset copy or replication when content moves between regions.
- Asset metadata lookup through grid services where useful.
- Optional central fallback storage later, but not as a v1 foundation.

Local asset bytes are immutable SHA-256-addressed blobs sharded by the first
hash byte. SQLite maps viewer-facing UUIDs to hashes, sizes, and required
creator UUID provenance, allowing many UUIDs to share one blob. Creator
provenance is independent of inventory ownership. A viewer UUID cannot be
remapped to different content or provenance. Grid metadata locates stable
region origin and replica endpoints; receiving regions verify size and SHA-256
before retaining the same UUID and creator. Reads verify the content hash
before returning bytes; garbage collection is deferred until retention and
replication rules exist. See [ADR 0020](adr/0020-asset-origin-and-replication.md).

This replaces the assumption that WHIP/Aperture are central platform services.
They remain useful references for asset behavior and performance expectations.

## Storage

Central grid services should use Postgres for durable relational data.

Region-local storage remains open and should be selected after access patterns
are better understood. Candidate approaches include:

- SQLite for compact local metadata and scene persistence.
- Filesystem blob storage plus a local metadata database for assets.
- RocksDB or another embedded key-value store if write/read patterns justify it.

The first design should avoid over-committing to a distributed database. Region
servers should be operationally understandable and recoverable from their local
state plus grid-service metadata.

The initial region store uses SQLite in WAL mode for snapshot metadata and a
deterministic JSON scene snapshot under `scene/`. Snapshots are written to a
same-directory temporary file and atomically replace the prior snapshot before
SQLite records the new revision. Regions snapshot every 30 seconds and during
orderly shutdown.

## Viewer Compatibility

Firestorm is the first practical compatibility target.

The first playable slice is pinned to the OpenSim-enabled Firestorm 7.2.4
release branch at commit `10bd3c9f930c76e1427ddd4ecece6cdf36b4406d`.
The minimum login and region-entry flow is recorded in
[FIRESTORM.md](FIRESTORM.md).

The official Second Life viewer and existing OpenSim/Halcyon codebases are
references for protocol behavior, but HomeWorldz only needs to preserve what
the viewer sees and depends on:

- Login and grid information.
- Region handshake.
- Seed capability and required capabilities.
- Event queue behavior.
- Avatar movement.
- Chat.
- Terrain and object updates.
- Inventory metadata access.
- Asset fetch and upload behavior.
- Object rez/edit/delete flows.
- Teleport and region crossing.

Internal service design should not be constrained by old server-to-server
protocols.

## Implementation Roadmap

Implementation phases, milestones, completion state, and upcoming work are
maintained in the [HomeWorldz roadmap](ROADMAP.md). This document describes the
architectural boundaries and decisions rather than duplicating that evolving
project status.
