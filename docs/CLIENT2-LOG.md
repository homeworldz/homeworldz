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
