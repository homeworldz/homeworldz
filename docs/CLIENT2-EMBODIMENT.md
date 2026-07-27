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
   appearances, and objects, but not 16×16 terrain patch packets (terrain
   reaches a session as one message or a capability fetch, later).
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
   asset-format work (ROADMAP Phase 9).
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
  shape and materials arrive with the Phase 9 asset work, additively.
- `transform` `{id, position: [x,y,z], velocity: [x,y,z], rotation}` —
  interest-filtered by draw distance. **`rotation` length discriminates
  the form**: 3 elements is an avatar's body rotation (the same triplet
  the LLUDP path carries today), 4 is an object's quaternion `[x,y,z,w]`.
- `kill` `{ids: [...]}` — entity ids leaving the scene.
- `chat` `{from, message}` today; grows additively to `{from, fromId?,
  position?, message}` when embodied chat lands — existing readers keep
  working.
- `crossing` (second cut) as above.

JSON first, per the encoding decision; the first-byte rule leaves room for
a packed transform encoding if Phase 2 measurement demands it.

## Milestones

- **E1 — embodied presence:** spawn/move/leave, transforms and kills both
  ways, positioned chat, draw-distance interest. Viewers see session
  avatars; session clients see the scene move.
- **E2 — crossings:** the `crossing` envelope, transit parity with viewers.
- **E3 — appearance/assets:** what session clients render for avatars,
  with the Phase 9 asset formats.

Out of scope throughout: capabilities/EventQueueGet, UDP texture transfer,
inventory xfer — a session client fetches assets over HTTP.
