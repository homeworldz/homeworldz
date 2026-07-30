# Session-avatar embodiment: design

The next region-session milestone after the observer session
([CLIENT2-TRANSPORT.md](CLIENT2-TRANSPORT.md)): a client on the WebSocket
session gains an avatar — spawn, movement, scene updates, positioned chat,
departure — alongside the LLUDP viewer path, without touching the viewer
wire. Grounded in a survey of the region's avatar machinery (2026-07-27);
file:line references are to that day's tree.

## The one honest summary of the existing machinery

Everything that *models* an avatar is already transport-neutral: the scene
entity, the `AvatarController` kinematics, the physics character, the
transit registry (agent/session-keyed), appearance and baking. Everything
that *addresses* an avatar is not: `avatars` and ten sibling maps are keyed
by the UDP endpoint string, identity is re-fetched from the circuit table
rather than stored, and ~40 inline fan-out loops hold already-encoded LLUDP
bytes at the moment of sending. Embodiment is therefore not a parallel
avatar system — it is re-keying participation and inserting one delivery
seam above encoding.

## Design decisions

1. **One avatar population, keyed by participant.** `avatars` re-keys on an
   opaque participant key — LLUDP keeps `"ip:port"`, sessions use
   `"ws:<session_id>"` — and `LiveAvatar` gains `transport` (lludp |
   session) plus stored `agent_id`/`session_id`/`circuit_code`, killing the
   `circuits.identity(endpoint)` round trips. Every existing
   `for (… : avatars)` loop then already covers both kinds.
2. **The delivery sink sits above encoding.** A `deliver(participant,
   update)` seam takes the semantic update — avatar transform, object
   update, kill, chat, appearance, animation — and switches on transport:
   the LLUDP branch is today's `encode_* / circuits.send / send_udp`
   unchanged; the session branch renders a JSON envelope onto the session's
   outbound queue. First cut carries the four kinds a session avatar needs:
   transforms, object updates, kills, chat. The two existing
   `broadcast_chat` call sites dissolve into it, and session chat gains
   distance filtering for free because the session avatar finally has a
   position.
3. **Movement input narrows to what the controller reads.**
   `AvatarController::apply` takes a `MovementInput` of the seven fields it
   actually consumes; `AgentUpdate` converts trivially. The UDP sequence
   gate stays LLUDP-only — WebSocket is ordered — while the 1 s
   transient-control expiry stays for both (it protects against stalled
   clients of either kind).
4. **Spawn and initial scene are extracted, not duplicated.** The spawn
   body inside `CompleteAgentMovement` (find-or-create entity by agent id,
   arrival/persisted/initial position, controller + Jolt character,
   presence) lifts into `spawn_avatar(...)`; the join backfill lifts into a
   per-kind `send_initial_scene(...)` — a session wants existing avatars,
   appearances, and objects, but not 16×16 terrain patch packets. The
   "later" arrived 2026-07-29 as the capability fetch: `GET /session/terrain`
   (region ticket bearer) returns the heightmap the region collides against
   as float32 little-endian meters, and the hello's `terrain` block states
   width, spacing, and the interpolation rule (each 1 m cell is two planar
   triangles along the (x, y+1)–(x+1, y) diagonal — Jolt's heightfield
   triangulation, which region movement samples by raycast). The hello's
   `avatar` block publishes the capsule contract (radius 0.3, support at
   ground + height/2, grounded tolerance 0.05) and the spawned reply carries
   the avatar's own computed height and hip offset. The support rule's
   honest domain, measured live 2026-07-29 (server-side samples, then the
   client core's cleaner sweeps): with Jolt active (every cloud region) z
   is the Jolt capsule's rest position. Exactness is established only on
   effectively flat ground — 0.000 m at gradient zero, ≤0.013 m at
   gradients under 0.05 (~3°), the strictest filter anyone has measured
   with. On any real slope the capsule contacts downhill of center and z
   sits below ground-under-center + height/2, growing with gradient:
   −0.107 m measured standing at a locally ~11° feature, −0.331 m at 39°
   (inside walkable), ~0.5 m sliding at 51°. The walkable limit —
   region-owned rather than Jolt's silent default,
   `character_walkable_slope_degrees` in physics.h (default 65°, Halcyon's
   MAX_WALKABLE_SLOPE, adopted 2026-07-29; per-region override
   `region.walkable_slope_degrees`), published in the hello avatar block as
   `walkableSlopeDegrees` — is strictly the
   grounded-versus-sliding boundary, NOT an exactness threshold; the two
   differ by an order of magnitude and conflating them was a bug in both
   ends' first drafts.

   **The resting height, and what `groundedTolerance` is measured from.**
   Established by designed measurement on the operator's test slope
   (2026-07-30), ten points across 10-33 degrees agreeing within 6 mm - a
   residual now known to sit inside Jolt's own height quantization, which is
   `block relief / 255` and therefore scales with local roughness rather than
   being a fixed floor:

       resting_z = ground + height * supportOffsetFactor
                          + capsuleRadius * (sqrt(1 + gradient^2) - 1)

   The third term is a hemisphere resting on an incline: its centre is a
   perpendicular distance `capsuleRadius` from the surface, so it sits
   `capsuleRadius * sec` above the ground vertically beneath it rather than
   `capsuleRadius`. It is zero on the flat and derives entirely from constants
   already published, so it is not a new field - but it **must** be included
   before `groundedTolerance` is applied. The tolerance is 0.05 m above
   `resting_z`, not above the flat arithmetic. Applied to the flat arithmetic
   it calls a *standing* avatar airborne from about 31 degrees upward, and
   0.41 m adrift at the 65 degrees a region will still stand an avatar on -
   which is exactly the fault it caused in the client core's predictor before
   the law was known. The region does not have that fault where Jolt is
   present, because `grounded` comes from Jolt's own contact state rather than
   from this arithmetic; the non-Jolt fallback is flat-only by construction,
   having a scalar ground height and no gradient to work from.

   Verified 10-33 degrees. Above that it is extrapolation: the law is
   geometry and should hold to the walkable limit, but no avatar has stood on
   ground between 33 and 65 degrees on this grid, so nothing has measured it. The flat arithmetic is also the non-Jolt fallback's
   clamp. Session z is the capsule center everywhere — the transform
   envelope briefly carried the hip-shifted viewer convention, found by
   the client core's ground comparison and fixed the same night. A fetch is a snapshot,
   so in-world terrain editing announces itself to connected sessions with a
   `terrainChanged` event naming the dirty 16 m patches (the event is itself
   named in the hello's terrain block as `changedEvent`). Notification is
   deliberately **lossy and detectable** rather than reliable (client core
   measurement, 2026-07-30): a monotonic per-region `revision` rides the hello,
   every `terrainChanged`, and the heightmap's `ETag`, so events may be
   coalesced or dropped under load and a client still knows it is behind. The
   region emits at most one event per 250 ms carrying the union of everything
   dirtied since the last, because one event per brush tick cost each client a
   whole-heightmap fetch - 4 MB on a 1024 region - and queued fetches faster
   than they completed. `If-None-Match` answers 304 for a current client, and
   the heightmap honours `Range` (`Accept-Ranges: bytes`), which matters
   because the map is row-major: dirty rows are contiguous, so 16 rows of a
   1024 region is 64 KB rather than 4 MB. Reconnects and crossings should
   compare the revision rather than assume nothing was missed - that is where
   a lost edit used to survive unnoticed. Canonical asset
   bytes reach a session through `GET /session/assets/{id}` on the same
   ticket, announced in the hello as `assets.base` (a base to append an id to, unlike
   `terrain.path`, which is complete); the reply's Content-Type
   names the format actually stored, because a mesh uploaded through the
   session path is glTF while one uploaded by a viewer is Second Life mesh
   (ADR 0033 stores each verbatim). Scene objects carry an optional
   `geometry` block (`assetId` plus `kind`, mesh or sculptMap) so a client
   can draw what an object is rather than a placeholder box - absent for
   prims, so a reader written before it keeps working.
5. **Departure splits into what the avatar owns and what the viewer owns.**
   `retire_avatar(key)` (kill broadcast, physics removal, avatar-keyed maps)
   serves both transports; `clear_viewer_transport(endpoint)` (texture
   queues, xfers, event responses) stays LLUDP-only. Session liveness is
   socket close plus protocol ping — no LLUDP ping/pong; the grid-session
   revalidation is already session-id-based and applies unchanged.
6. **Draw distance is explicit or defaulted, never zero.** `draw_distance
   = 0` means "no filter" to the dynamic-object interest check, so an
   embodied session that never reports one would receive every dynamic
   object in the region. The session's spawn message carries an optional
   `drawDistance`; absent, the server applies a conservative default
   (128 m) rather than infinity.
7. **Appearance first cut: the server bake.** A session avatar seeds the
   default-outfit server bake exactly as an appearance-less viewer does, so
   viewers see something sane. Sessions do not receive `AvatarAppearance`
   blobs in the first cut (texture-entry byte blobs are a legacy shape);
   what a session client renders for other avatars is deferred to the
   asset-format work (ROADMAP Phase 10).
8. **Crossings cost one new message.** The transit machinery needs no
   change; the session equivalent of `EnableSimulator`/`CrossedRegion` is a
   single `crossing` envelope carrying `{regionHandle, sessionURL of the
   neighbor, position, lookAt}` — and the neighbor's session URL is already
   in the lease store. Deferred to the second cut, after single-region
   embodiment stands.

## Wire messages (session envelope kinds)

Field-level contract, agreed with the client core 2026-07-27. Conventions
as everywhere on this surface: the `{type, version, correlationId?,
payload}` envelope, camelCase keys, vectors as 3-element JSON arrays,
errors as `{code, message, field?}`. Optional fields are omitted, never
sent as zero values. Fields are only ever added to these payloads, never
repurposed.

Client → server, after auth:

- `spawn` `{drawDistance?}` — requests embodiment; answered by `spawned`
  or an `error`. An observer session stays legal: no spawn, no scene
  traffic, chat region-wide as today.
- `move` `{controls, bodyRotation: [x,y,z], camera?: {center, at, left,
  up (each [x,y,z])}, drawDistance?}` — `controls` is the `control_flags`
  bitfield unchanged. Applied at most once per tick, last-write-wins
  within a tick. **Draw distance is session state, not per-move input**:
  the server keeps the last value (from `spawn` or a `move` that carried
  one) and re-fills it into every `MovementInput`, so a `move` without it
  can never write the zero that means "no filter". Values clamp to
  [16, 512]; a session that never sends one gets 128. A `move` without
  `camera` keeps the previous camera.
- `say` `{message}` — public chat at the avatar's position, normal radius,
  same 1-2048 character rule as instant messages.
- `leave` — payload omitted entirely; retires the avatar, keeps the
  session open (back to observer).

Server → client:

- `spawned` `{entityId, position: [x,y,z], lookAt: [x,y,z]}` — `entityId`
  is the scene entity id as a decimal string, the same id `transform` and
  `kill` use.
- Initial scene after `spawned`: one `avatar` per present avatar, one
  `object` per entity, then live traffic.
- `avatar` `{id, userId, position: [x,y,z], rotation: [x,y,z]}` — display
  names arrive with the name-resolution work, additively.
- `object` `{id, objectId, ownerId, name, position: [x,y,z],
  rotation: [x,y,z,w], scale: [x,y,z]}` — enough to place a box; prim
  shape and materials arrive with the Phase 10 asset work, additively.
- `transform` `{id, position: [x,y,z], velocity: [x,y,z], rotation}` —
  interest-filtered by draw distance. **`rotation` length discriminates
  the form**: 3 elements is an avatar's body rotation (the same triplet
  the LLUDP path carries today), 4 is an object's quaternion `[x,y,z,w]`.
- `kill` `{ids: [...]}` — entity ids leaving the scene.
- `chat` `{from, message}` today; grows additively to `{from, fromId?,
  position?, message}` when embodied chat lands — existing readers keep
  working.
- `hello` (the auth reply) carries, additively since 2026-07-28,
  `movement: {walkSpeed, runSpeed, jumpVelocity, gravity}` and
  `interestSweepMs`. The movement block is the region's authoritative
  movement model for client-side prediction of the client's own avatar —
  the same constants the server controller computes with, published so no
  client hard-codes an observation. Semantics a predictor needs beyond the
  numbers: diagonal input is normalized (never faster than straight);
  horizontal velocity is control-driven while flying or grounded, but an
  airborne non-flying avatar is ballistic with directional input still
  steering; flight cruise speed equals walk/run speed; avatar capsule height
  is per-avatar (from its shape), not a constant. `interestSweepMs` is the
  avatar-interest sweep period — the floor on remote-transform staleness,
  which is what an extrapolation cap should be derived from.
- `crossing` (second cut) as above.

JSON first, per the encoding decision; the first-byte rule leaves room for
a packed transform encoding if Phase 2 measurement demands it.

## E2: a session crossing is a re-entry, not a handoff

The design left "one crossing envelope" unresolved on the question that
actually decides the shape: **what credential does the client present at the
destination?** Its region ticket names the region it was minted for, so it is
refused next door — correctly, that is the audience and region claim doing
their job.

Two candidate answers. The **handoff** model mirrors viewers: the source
region asks the grid to mint a ticket for the neighbor and hands it over with
the crossing, keeping one grid session across the border. The **re-entry**
model has the client run world entry again for the named destination,
receiving a fresh ticket and session by the path it already used to arrive.

**Decided: re-entry.** The handoff model would let a region obtain a
credential for a *different* region — a new trust surface on machines
[ADR 0028](adr/0028-untrusted-region-trust-model.md) explicitly does not
trust — and it would need the destination to accept an avatar staged against
a session id the client is about to replace. Re-entry needs no new endpoint,
no new trust, and no change to the viewers' transit machinery, which stays
exactly as it is. What the client already holds makes it cheap: world entry
accepts `Region/x/y/z`, so the arrival point survives the crossing exactly.

The honest cost: **a crossing is not atomic for sessions.** The avatar is
retired here before it exists there, so there is a brief gap — a REST round
trip plus a socket open — where it is in neither region, and other
participants see it leave and arrive rather than slide across. Viewers keep
the seamless path. This is acceptable because the grid channel, not the
region session, is what must survive a crossing, and it does: instant
messages and notices are unaffected by a region change, which is the whole
reason that channel is anchored to the grid.

The envelope, sent to the crossing session and then the avatar is retired
in the same tick (so it never wanders outside the region):

```json
{ "region": "Welcome", "sessionURL": "wss://…/session/welcome",
  "start": "Welcome/250/128/23", "position": [250, 128, 23], "lookAt": [1, 0, 0] }
```

`start` is pre-formatted for handing straight back to world entry, so no
float formatting can drift between the two sides. A client that ignores the
crossing simply stops moving: containment applies toward any neighbor that
serves no session, and its last position is persisted, so `start=last`
recovers it.

## Interest: sessions now, viewers on a Firestorm pass

A session holds an **interest set** — the avatars it currently knows about.
Avatar `transform` frames flow only for pairs already in that set, and a
100 ms sweep emits the boundary events: an `avatar` message when a body comes
into range, a `kill` when it leaves.

**The sweep evaluates every observer-subject pair rather than reacting to the
subject's motion**, because interest changes when *either* party moves. A
subject-driven design looks correct in every test where the moving avatar is
the one being watched, and is silently wrong the moment an observer walks
away from someone standing still — that observer is never told, and its
client holds a body frozen at a stale position forever. Verified in exactly
that direction.

Rules worth stating: **self is always in interest** (an avatar's own
transforms are its authoritative position), spawn seeds the set from what is
genuinely in range rather than announcing the whole region, and retirement
forgets the avatar in every other session's set so a later arrival is
announced afresh rather than assumed known. Draw distance is the session
state described above, so it can never be the zero that means "no filter".

**Objects follow the same discipline**, and for the same reason: a client
keeps what it was told about. Every path that kills an object for viewers —
link-set removal, derez, temporary expiry, auto-return — kills it for
sessions too, a dynamic object leaving interest is said out loud rather than
having its transforms silently stop, and first sight of one is an `object`
introduction rather than a `transform` for an id the client never learned.
So for both kinds: **a `kill` means gone or out of view, and either way
remove it** — if it matters again, an introduction follows.

**Viewers remain region-wide, deliberately.** The same narrowing for the
LLUDP path changes what a legacy viewer sees — bodies would need killing and
re-rezzing at the boundary — and that is a visible behavior change on the
compatibility-critical path with no automated acceptance test behind it. It
wants a manual Firestorm regression pass before it ships, and is left
unchecked on the roadmap for that reason rather than forgotten.

## A quiet client is not a dead client

Established by the client core's browser work, 2026-07-27, and binding on
anything added later. **Browsers throttle hidden pages** — a tab hidden long
enough runs timers roughly once a minute ("intensive throttling"), and may be
frozen outright. A backgrounded client therefore stops sending keepalives,
while its socket stays open and reads normally.

Two consequences the server side must honor:

- **No idle or pong timeout below about 120 seconds** on the region session
  or the grid channel. A tighter one disconnects users whose only fault is
  looking at another tab. The session has no idle timeout today, and the
  auth deadline (10 seconds, before any traffic) is unaffected.
- **A stalled reader must not grow the region's memory.** Each session's
  outbox is bounded: past a soft limit, frames a later one supersedes
  (transforms) are dropped first; past a hard limit the oldest go
  regardless, logged as `session outbox trimmed`. A client that was stalled
  long enough to lose frames resynchronizes by spawning again — the initial
  scene is the resync mechanism. Durable traffic is deliberately not here:
  instant messages live on the grid channel with store-and-forward.

## Milestones

- **E1 — embodied presence:** spawn/move/leave, transforms and kills both
  ways, positioned chat, draw-distance interest. Viewers see session
  avatars; session clients see the scene move.
- **E2 — crossings:** the `crossing` envelope, transit parity with viewers.
- **E3 — appearance/assets:** what session clients render for avatars,
  with the Phase 10 asset formats.

Out of scope throughout: capabilities/EventQueueGet, UDP texture transfer,
inventory xfer — a session client fetches assets over HTTP.
