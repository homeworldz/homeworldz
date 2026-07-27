# CLIENT2 Implementation Log

Decisions made while implementing [CLIENT2.md](CLIENT2.md), recorded for review
rather than approval-in-advance: each was judged cheaper to revise after the
fact than to hold the implementation for. Entries are grouped by the build-order
step they belong to.

The 2026-07-26 grid-side entries below were reviewed and approved 2026-07-26 as
a good "for now" state; individual calls may be reopened as evidence arrives.

## 2026-07-26 — steps 0 through 4 (commits `1f2e178` through `28c8218`)

### Step 0: Go version

- **The language version landed at 1.23, not the planned 1.22.** Two forces:
  a bare `go 1.22` directive names a toolchain that does not exist as a release
  (the download fails; `1.22.0` resolves), and `coder/websocket` v1.8.15
  requires Go ≥ 1.23, which `go get` recorded in `go.mod` on its own. `go.work`
  says `1.23.0`.
- **Method-pattern routing was tried and abandoned for these routes.** The
  public mux registers a `/` catch-all serving JSON 404s, and it out-matches
  the ServeMux fallback that would otherwise turn a wrong-method request into
  an automatic 405. New handlers therefore keep the explicit method check the
  rest of the codebase uses. The bump still earns its keep through the
  dependency floor and future patterns elsewhere.

### Step 1: the probe

- **The probe advertises `protocol: 1` while `regions.transports` is empty.**
  The protocol gate covers the REST arrival surface (probe, tokens, world
  entry), which now exists; the empty transports list says, honestly, that no
  region session transport does. A client distinguishes "grid too old" from
  "grid ready for login but not yet for scene traffic" from those two fields.
  If review decides protocol 1 should mean the whole path including transport,
  the constant is one line.
- **`welcome` reports its name whenever the arrival list is configured, and
  coordinates only when a provisioned region matches that name** (case-
  insensitive, `MapX`/`MapY`). With no arrival list the field is omitted
  entirely rather than invented.
- **`welcome_locations` lives in `[grid]`,** beside `name`, since it describes
  the grid rather than the website API. Comma-separated per the existing
  `splitList` convention, which makes region names containing commas
  unsupportable in this setting — judged acceptable. Entries are parsed at
  binary startup so a malformed entry fails fast, and the config layer keeps
  `[]string` so it stays dependency-free.

### Step 2: the version handshake

- **The reported region protocol is not persisted** — no migration, no column.
  Enforcement is stateless at registration and renewal, which is all the match
  rule needs; storing the last-reported value is operator-dashboard material
  and can arrive with that work.
- **Refusal is 409 `region_protocol_mismatch`,** naming both versions. When
  the region is *ahead* of the grid the message says the grid is behind rather
  than telling the operator to upgrade a region that is already newer.
- **Deployment order constraint, worth knowing:** the internal tier's decoder
  rejects unknown fields, so a region that starts sending `regionProtocol` to
  a grid predating this commit would get a 400. Grids upgrade before regions —
  the natural order, but now a required one.
- **The "upcoming value" announcement from CLIENT2.md is not implemented** —
  there is no increment scheduling mechanism to announce from yet. The reply
  carries the current protocol only.

### Step 3: world entry

- **The region-ticket audience is the constant `homeworldz:region`,** not
  configuration: the separation is structural and there is nothing for an
  operator to choose. The ticket signer shares `jwt_secret` and issuer with
  the account signer; a separate secret would add a key to manage without
  adding a boundary the audience does not already draw. TTL is
  `[website] region_ticket_ttl_seconds`, default 300 seconds.
- **Client sessions live 12 hours, matching viewer sessions,** so one expiry
  story covers both kinds of client. `CreateClientSession` is deliberately not
  on the `identity.Store` interface, which serves the viewer paths.
- **The `start` parameter accepts `last` (default), `home`, or `Region/x/y/z`**
  — the legacy `uri:Region&x&y&z` spelling is not carried over. An explicitly
  named region that is offline is a 404, never a diversion; only the implicit
  paths (`last`/`home` missing or offline) walk the welcome list, from a
  random starting index. A stored position survives only when the stored
  region is the one selected, so a diversion cannot leak a stale position
  into the wrong region.
- **The legacy resolver still stands apart.** The new `arrival` package is the
  shared home CLIENT2.md decided on, but `httpapi.resolveDestination` was not
  refactored onto it tonight — that touch of the viewer login path deserves
  its own change with Firestorm regression attention. Follow-up, not accident.
- **The session response's capability manifest is `{version: 1, transports: []}`.**
  Honest and nearly empty; it grows as region extensions ship.

### Step 4: the grid channel

- **`coder/websocket` v1.8.15 is the dependency,** the module's first beyond
  the original four. Taken deliberately against the minimal-dependency stance:
  a WebSocket endpoint on the open internet parses hostile framing, and that
  is the wrong place to hand-roll. `gorilla/websocket` was the alternative;
  the smaller, context-first API fit better.
- **Authentication is a mandatory first `auth` message under a 10-second
  deadline,** because a browser cannot set an `Authorization` header on a
  WebSocket. A query-string token was rejected — tokens in URLs reach logs.
  The token is resolved exactly as REST `requireAuth` resolves it, and a
  region ticket is refused by the audience check, proven by test.
- **The envelope is `{type, version, correlationId, payload}`** with the
  first-byte rule enforced: a text frame not starting `{` closes the
  connection with "unsupported message encoding" rather than a JSON syntax
  error. Ping/pong echoes the correlation identifier; an unknown type gets an
  `error` envelope rather than a close, so one bad message does not cost a
  connection.
- **The channel carries no notification traffic yet** — hello, ping/pong, and
  errors only. Instant messages, presence, and inventory offers need
  delivery plumbing from the stores outward, which is why the corresponding
  ROADMAP2 item stays unchecked.
- **`channelURL` in the probe comes from a new `[website] public_url`,** and
  is omitted when unset — a client that probed can derive the URL from the
  API base it already used. The bind address cannot honestly produce a public
  URL, so none is fabricated from it.

### Not done, so nobody hunts for it

- No C++ region work (per "grid side only"): the region does not yet send
  `regionProtocol` (done later that day — see the next section), and nothing
  region-side validates a region ticket — that lands with the region session
  transport.
- No CORS allowlist change: no client origin exists to add.
- No persistence of reported region protocols, no increment scheduling, no
  region session transport, no asset-format work.

## 2026-07-27 — embodiment E1 ships

Session avatars spawn, move, and speak, per [CLIENT2-EMBODIMENT.md](CLIENT2-EMBODIMENT.md)
and its settled wire contract; E2E-proven on the test grid with two live
accounts observing each other. Judgment calls within the contract:

- **Avatar transforms reach sessions region-wide**, exactly as viewers get
  them; the draw-distance interest filter applies to dynamic objects (where
  viewers have it too). Narrowing avatar transforms is interest-management
  work both transports need together.
- **Session say uses the viewer chat radius** (`chat_range` of type say) for
  viewer fan-out, and reaches all sessions region-wide with `fromId` and
  `position` attached so a client can filter or place it — session-side
  radius filtering lands with the interest work above.
- **A session avatar spawns with a default appearance geometry** and no
  parcel bookkeeping (`push_agent_parcel` is viewer reporting); the
  default-outfit bake seeding for viewer eyes is deferred with E3 appearance
  work — viewers currently see a default-shaped avatar.
- **Spawn is idempotent**: a second spawn answers `spawned` with current
  state rather than erroring, so a client can re-assert after uncertainty.
- **The initial scene excludes the spawning session's own avatar** — the
  client knows itself from `spawned` — and excludes terrain, per the design.

## 2026-07-27 — embodiment E2: crossings, and the arrival gap it exposed

- **Crossings are re-entry, not handoff**, decided on the credential
  question and argued in [CLIENT2-EMBODIMENT.md](CLIENT2-EMBODIMENT.md).
- **The probe found a gap older than E2:** world entry resolved an arrival
  position, returned it to the client, and never told the region — so both
  a named `start` and a crossing landed wherever the region defaulted. Fixed
  by carrying the position as a **signed ticket claim**, returned from
  validate-ticket: the region learns it from the grid, never from the
  client, so no client can choose where it spawns. A schema change was the
  alternative and the ticket is the better carrier — it already binds region
  and session, and the position is only meaningful for the ticket's life.
- **Neighbor session endpoints ride the grid's topology response**, so a
  region knows whether a neighbor can receive a session at all; toward one
  that cannot, containment still applies.
- **A retiring session avatar persists its last location**, so an abandoned
  crossing recovers with `start=last` rather than stranding the avatar at a
  border.
- Verified live: crossing envelope, re-entry, and landing at the resolved
  point on the neighbor's edge, handoff about 90 ms.

## 2026-07-27 — instant messages: the first store-and-forward kind

- **Stored before any delivery is attempted** (`instant_messages` table,
  migration 000026): durability is the point of the kind, so the store write
  is not conditional on the recipient being offline.
- **"Handed to a connection" counts as delivered.** A connection that dies
  mid-write loses the message exactly as it would have live; read receipts
  are a client feature for later, not a delivery-marking mechanism.
- **The backlog replays on channel connect, before anything else**, in sent
  order, capped at 100 per connection; the stable message `id` lets a client
  de-duplicate a live delivery against a replay race.
- **No history endpoint yet** — the table is the durable record and a
  paginated `GET` over it is management-surface work, deliberately not
  smuggled in here.
- **Anyone authenticated can message anyone.** Blocking, muting, and rate
  policy beyond the tier's existing per-IP limiter are social-platform work
  (ROADMAP Phase 5), not transport work.

## 2026-07-27 — step 5, the region session (WebSocket, option A)

The region session ships over TLS + WebSocket per the accepted decision in
[CLIENT2-TRANSPORT.md](CLIENT2-TRANSPORT.md). The milestone is deliberately an
**observer session**: authenticate, hello, ping/pong, and the region's public
chat delivered server-initiated — no avatar embodiment, no scene updates, no
client-to-region chat. Those arrive with the arrival/embodiment work, which
has its own design surface (spawn, appearance, interest management).

- **Region-side ticket validation is a grid round trip**, POST
  `/api/v1/region-runtime/{id}/validate-ticket`, authenticated by the
  region's own access key. Verifying locally would require the signing
  secret on machines the operator does not run, and an evil region holding
  that secret could mint account tokens; ADR 0028 forbids exactly that
  trust. The validation call blocks the session service thread for one grid
  round trip during auth only.
- **TLS terminates at fronting infrastructure this milestone** (the grid's
  edge or a local proxy such as the Caddy already on the deployment box);
  the region listens in plaintext on `region.session_port` and reports the
  *public* `region.session_public_url` — explicit configuration, because
  only the operator knows where TLS terminates. In-region TLS arrives with
  direct home-hosted serving, where the relay design already concluded the
  certificate question is hardest.
- **The protocol layer is transport-free** (`session_protocol.h`:
  envelope codec, first-byte discrimination, and the `SessionCore` state
  machine), in the shared-library shape CLIENT2.md calls for — dependency-
  free, unit-tested without sockets, structured for later consumption by the
  client core. The libwebsockets glue (`session_server.cpp`) runs one
  service thread; cross-thread chat is queued and drained via
  `lws_cancel_service`.
- **An authenticated session hears the whole region's public chat.** A
  session has no position yet, so llSay's 20 m radius has nothing to measure
  from; region-wide delivery is the honest interim and narrows to
  position-based interest when sessions gain an avatar. `llOwnerSay` stays
  viewer-only (it targets one owner in-world).
- **Avatar chat reaches sessions with the same `from_name` the viewer path
  sends today** — currently the agent UUID, which viewers resolve by name
  lookup. Parity, not polish; name resolution for sessions lands with
  identity work.
- **libwebsockets is the region's first networking dependency** (vcpkg, both
  platforms), taken per the decision document; the region's own hand-rolled
  HTTP/1.1 listener is untouched.

Follow-ups from the client core's review, same day:

- **`\u` escape decoding now handles surrogate pairs** (one supplementary
  code point, proper UTF-8) and refuses lone or unpaired surrogates rather
  than substituting — emitting CESU-8 was a latent bug the client core
  caught; nothing sends surrogate escapes today, but any JSON encoder that
  escapes non-ASCII would have surfaced it.
- **No origin enforcement on the session listener.** lws's
  security-best-practices option refuses cross-origin upgrades, and every
  browser client is cross-origin by design; the region ticket is the
  credential.
- **The client core is not adopting `session_protocol.h`**, for reasons it
  argued and this log accepts: `SessionCore` is the server role (there is no
  client mirror in it), and the client's own reader is deliberately stricter
  (bounded depth and length, absent-versus-empty `correlationId`) because it
  meets a grid before deciding to trust it — client ADR 0005 territory. The
  anti-drift mechanism is documented wire examples used as test specimens on
  both sides, not shared code; if a shared codec is wanted later, the
  stricter reader is the one to lift into the shared position.

## 2026-07-26 — viewer destination resolution moves onto the arrival package

The follow-up the world-entry work deferred: `httpapi.resolveDestination` now
resolves on `arrival.Resolve`/`ResolveNamed` instead of its own walk, so the
viewer login lands on the welcome list where the old code fell back to
`items[0]`, and the client and viewer paths cannot drift apart.

- **The first-region fallback survives only on grids with no welcome list**,
  so an unconfigured development grid still logs a viewer in somewhere. With
  a welcome list configured, exhausting it refuses the login — landing the
  user in a region the operator never named would misreport the outage.
- **`startLocation` semantics are untouched.** Arrival points carry positions,
  but populating the viewer's start position from them (in place of
  `normalizeStart`) changes the login response and deserves its own
  Firestorm-regression pass; the region still chooses the spawn.
- **Needs one manual Firestorm login pass** before full trust, per the
  original deferral's reasoning; the XML-RPC/LLSD shapes are covered by tests
  and did not change.

## 2026-07-26 — notification delivery over the grid channel

The channel's first server-initiated traffic. Scope was set by what actually
exists: a survey found **no notification producers anywhere in the grid** — no
instant messages, no inventory offers, no friendships, no groups — so the work
is the delivery machinery plus the one producer that is real today, an
operator notice.

- **A per-user connection hub in `internal/api`** (`channel_hub.go`): the
  channel handler registers the authenticated account's connection and
  deregisters on exit; a user may hold several connections and each receives
  every delivery. The hub lives in the same process as the channel, so no
  cross-process bus was needed — that question arrives with region-originated
  events (IMs), which do not exist yet.
- **One writer goroutine per connection.** Hub deliveries and read-loop
  replies both write to the socket, so all post-hello writes funnel through a
  per-connection queue. Replies wait for space (their ordering matters); hub
  deliveries drop when a consumer's queue is full rather than stalling
  everyone else's.
- **Best-effort, deliberately unpersisted.** A notice to an offline user
  reports `delivered: 0` and stores nothing. Every notification the grid can
  produce today describes durable state the client re-reads on reconnect;
  store-and-forward is deferred to the notification kinds that genuinely need
  it (IMs, offers), which also have no tables yet.
- **The producer is `POST /v1/admin/users/{id}/notice`** (privilege `users`),
  sending `kind: "system_notice"`. Automatic notices from admin actions (ban,
  privilege change) were considered and deferred — a ban's delivery story
  should arrive together with session-revocation eviction from the hub, which
  needs a sessions-by-user query that does not exist.

## 2026-07-26 — step 2, region side

The region binary now reports its grid-region protocol version, completing the
version handshake in both directions.

- **The version is `homeworldz::grid::region_protocol = 1`, a compiled-in
  constant** in `region/include/homeworldz/grid_client.h`, per CLIENT2.md's
  distribution rule: the number is an assertion about what the code implements,
  so it ships with the code and cannot be separated from it by a stale file.
- **It is sent on the provisioned paths only** — registration
  (`POST /api/v1/region-runtime/{id}`) and lease renewal
  (`PUT .../lease`) — because those are where the grid enforces the match. The
  legacy `POST /api/v1/regions` request model does not carry the field and the
  internal tier's strict decoder would 400 it; the legacy dev path stays as it
  was.
- **The registration reply's `regionProtocol` is required and kept** on
  `RegisteredRegion::grid_region_protocol`, matching the function's strict
  parsing of every other reply field. Requiring it costs nothing: a grid old
  enough to omit it would already have 400ed the request's unknown field
  (grids deploy before regions, the standing order). Today a successful
  registration always matches; the field is the hook for warning ahead of an
  announced increment once the grid can announce one.
- **Refusals reach the operator's log.** `register_provisioned_region` and
  `renew_provisioned_lease` take an optional out-parameter filled with the
  grid's error `message` on failure, `RegistrationLifecycle` keeps the last
  renewal failure's message, and `main.cpp` logs either as a `reason` field
  beside the existing error line — so a protocol-mismatch refusal, which names
  both versions, is actionable from the region's own log per CLIENT2.md.
