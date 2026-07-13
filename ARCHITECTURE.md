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
- Use explicit versioned paths for APIs that need compatibility guarantees.
- Use HTTP streaming for large transfers where plain request/response is not a
  good fit.
- Defer gRPC, protobuf, FlatBuffers, or a message bus until a concrete measured
  need appears.

Possible later reasons to add protobuf or gRPC:

- Very high-frequency region-to-grid calls.
- Complex bidirectional streaming.
- Strict generated cross-language schemas.
- Many independently maintained services.
- Binary payloads where JSON overhead becomes a measured problem.

Until one of those reasons is real, HTTP/JSON is the default.

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

Jolt is the leading candidate for the physics engine, with PhysX 5 still worth
comparison. See [PHYSICS.md](PHYSICS.md) for the detailed evaluation.

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

## Viewer Compatibility

Firestorm is the first practical compatibility target.

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

## Initial Milestone

The first playable architecture milestone should implement a minimal vertical
slice:

- One Go login/grid service backed by Postgres.
- One C++ region server.
- HTTP/JSON service calls between region and grid.
- Local region scene and asset storage.
- Jolt or PhysX-backed collision/physics prototype.
- Firestorm login to a single region.
- Basic avatar movement, chat, terrain, static objects, and asset fetch.

After that slice works, expand toward inventory, object editing, scripting,
cross-region transfer, asset federation, and production operations.

