# Serving the Homeworldz Client

How this server grows the surface the first-party Homeworldz client needs, in the
order it should be built. This is the implementation companion to
[ADR 0032](adr/0032-region-extensions-for-new-client.md) and to Phase 1 of
[ROADMAP2.md](ROADMAP2.md); those state intent, this states the mechanism and the
sequence.

It records **current expectation and intent**, not a commitment, and is expected
to be revised as evidence arrives — particularly by the throwaway browser client
of ROADMAP2 Phase 2, which exists precisely to find where this plan is wrong
before a durable client is written against it.

The client itself is built in a separate repository and is not touched from here.
Terminology follows [STYLE.md](STYLE.md): **viewer** means Firestorm and its
Second Life-lineage peers, **client** means Homeworldz's own.

Like [ROADMAP2.md](ROADMAP2.md), this document is repository-only. `syncweb.mjs`
publishes `ROADMAP.md` and nothing else.

## The shape of the problem

A viewer discovers what a region can do by **negotiating**: it reads the
`SimulatorFeatures` capability, asks for named seed capabilities, and a region
that does not understand a request simply does not grant it. That mechanism is
implemented ([region/src/region_extensions.cpp](../region/src/region_extensions.cpp))
and its registry is deliberately empty.

The client cannot use it, for two reasons that are worth restating because they
determine everything below. `SimulatorFeatures` and the seed reply are LLSD, and
the client is built specifically to carry no legacy serialization at any layer.
And negotiation implies a fallback, which a browser does not have: it cannot open
a raw UDP socket, so a grid that cannot serve the modern path cannot serve the
client at all.

So the client's discovery is a **probe, not a negotiation**. One unauthenticated
JSON document over HTTPS tells it whether this grid speaks anything other than
LLUDP and LLSD. If it does, the client proceeds down the modern path and never
looks at a capability array. If it does not, the client reports an incompatible
grid and stops.

## The compatibility document

**`GET /v1/version`**, unauthenticated, JSON. The first request the client makes,
and the only one it makes before deciding whether this grid is usable at all.

`/v1/version` rather than `/v1/config`: the internal tier already serves
`/version` with a `{service, version, apiVersion}` body
([grid/internal/httpapi/api.go:332](../grid/internal/httpapi/api.go:332)), and
reusing that name and shape on the public tier keeps one convention instead of
two. A `config` document also invites deployment detail that has no business
being unauthenticated.

Note that the public tier previously had **no unauthenticated operational
routes** — `/ping`, `/ready`, and `/version` existed only on the internal `grid`
binary. This is the first one, and `internal/api` did not receive a version
string even though `cmd/homeworldz-api` already had one
([grid/cmd/homeworldz-api/main.go:32](../grid/cmd/homeworldz-api/main.go:32)), so
`api.Options` grew a `Version` field (shipped 2026-07-26).

### What it carries

```json
{
  "service": "homeworldz-api",
  "version": "0.9.14",
  "apiVersion": "v1",
  "client": {
    "protocol": 1,
    "minimumProtocol": 1,
    "grid": {
      "name": "Homeworldz",
      "channel": "websocket",
      "channelURL": "wss://api.homeworldz.com/v1/client/channel"
    },
    "regions": {
      "transports": ["webtransport", "websocket"],
      "assetFormats": ["ktx2", "gltf2"],
      "meshedPrims": false
    },
    "welcome": { "name": "Welcome", "gridX": 1000, "gridY": 1000 }
  }
}
```

The `client` object is the extra field the client reads; everything outside it is
what the existing `/version` shape already carries, so a monitoring check does not
have to learn a new document.

Two of these fields are optional, and their absence is meaningful rather than
an error. **`channelURL`** appears only when the deployment knows its public
URL (`[website] public_url`); when absent, the client derives the channel URL
from the API base it just probed — same host and port, `http` → `ws` and
`https` → `wss`, path `/v1/client/channel`. That derivation is normative, not
repair: a grid that omits the field is saying "the channel is where you found
me." **`welcome`** appears only when the grid configures an arrival list; a
grid without one simply has no advertised landing region.

**Nothing in this document is allowed to be non-universal.** That rule is what
keeps the probe honest, and it excludes more than it first appears — region size,
for instance, is not a grid property at all. `validRegionSizes` is `{1, 2, 4}`
([grid/internal/api/admin_regions.go:37](../grid/internal/api/admin_regions.go:37))
rendered as `Size * 256`, so regions in one grid are 256, 512, or 1024 metres and
vary independently. The 1 MiB upload cap is likewise enforced region-side. Both
belong to the session manifest.

The rule also excludes limits as a category, for a reason worth stating: the probe
is fetched once, before login, by a client with no session, so any number in it
gets trusted for the rest of that session. Limits move on config reload and region
deploy. The session manifest re-resolves on every region crossing, which is the
cadence a changing value needs.

### Gate versus data

The distinction inside `client` is the load-bearing part of this design.

**`protocol` is a gate.** It is a single integer, not a feature list, because the
client requires the whole modern path rather than parts of it. The client declares
a minimum and compares; there is nothing to negotiate and no partial success. The
document reports both the protocol version the grid speaks and the oldest one it
still accepts, so a client can tell "too new for this grid" from "too old for
it".

**`features` is data.** These are genuinely optional capabilities a grid may lack
without being incompatible — voice, guest access, whether the region will mesh
prims for you. A client adapts rather than refuses. Keeping them out of `protocol`
is what stops the probe from turning back into a negotiation.

**`transports` is also data**, and it is what makes the WebSocket fallback
possible without conflating a blocked network with an incompatible grid. If
`webtransport` is advertised and the QUIC attempt fails, that is a network
condition and the client falls back to WebSocket. If it was never advertised,
there was nothing to fall back from.

### Why capabilities are split into `grid` and `regions`

Because only one of the two groups can honestly claim to be universal, and
flattening them into a single map would state a falsehood on any grid that hosts a
region the operator does not run.

- **`grid`** is served by the grid services themselves. The grid channel is a
  WebSocket on the public tier, so the tier answering the probe is the same
  software that will answer the upgrade. This group is genuinely universal.
- **`regions`** describes what this grid's **region software** offers — QUIC,
  KTX2, meshed prims. Regions are hosted by their owners rather than by the grid,
  and [ADR 0028](adr/0028-untrusted-region-trust-model.md) admits regions the
  operator does not control, so the grid is only the authority for this group to
  the extent that it controls **which regions get a lease**. That is what the
  region protocol version below is for.

An alternative was to omit `regions` entirely and let `protocol` imply it —
protocol 1 *means* QUIC and KTX2. That is rejected because it forces every client
to carry a hardcoded version-to-feature table, which makes a newly added optional
capability undiscoverable without a version bump. Explicit flags are
self-describing; a version number is not.

### What makes `regions` trustworthy: the region protocol version

**A region whose protocol version does not match the grid's is refused
registration, and told to upgrade.** Without this the `regions` group is only
advisory. With it the grid can state what every leased region speaks, because it is
the thing handing out leases.

Registration carried no version before this work — `regions.Registration` was
name, coordinates, endpoint, port, and lease duration
([grid/internal/regions/store.go:29](../grid/internal/regions/store.go:29)),
submitted over the `/api/v1/region-runtime/` path that per-region access keys
authorize ([ADR 0024](adr/0024-provisioned-region-identity.md)). **Shipped both
sides 2026-07-26:** the region sends its protocol version and the grid checks
it, at registration and at renewal.

#### Two protocol numbers, neither of them a release version

The number that governs this is a **region protocol version**: a small integer,
owned by the grid↔region contract, that increments **only when a change actually
requires region software to be upgraded.** It is deliberately not the release
version. Most grid upgrades ask nothing of a region owner, and gating on a release
version would force a grid-wide upgrade every time the grid shipped a patch, for
changes affecting nothing region-side.

Once the number carries that meaning, **matching is the right rule and needs no
floor.** A floor only earns its keep when the version increments for reasons that
do not concern regions, and this one does not: if it moved, regions must move too.
That is the same conclusion a floor would reach, arrived at more simply.

The client-facing versions in the probe are a **separate axis**, because a change
can break clients without touching regions and the reverse. That gives two
independent contracts rather than one:

| Number | Contract | Increments when |
| --- | --- | --- |
| region protocol | grid ↔ region | region software must be upgraded |
| `protocol` / `minimumProtocol` | grid ↔ client | the client-facing surface changes |

Keeping them separate is what stops a client-only change from evicting every region
on the grid, and a region-only change from invalidating every installed client.

**This simplifies the advertising rule.** A region-side capability becomes
advertisable in the probe's `regions` group at the moment the region protocol
increments to include it, because every non-matching region has already been
refused. "Tell clients every region serves KTX2" and "require the region protocol
that introduced KTX2" are one decision, and the cost lands at the point of
choosing.

#### Distribution and where each number lives

The grid's required region protocol is **deployment policy**, so a value in grid
configuration, updated as part of the upgrade, is the right home — that is an
operator decision and it belongs where operator decisions live.

A region's own protocol version should be **compiled into the region binary**, not
read from a file beside it. The number is an assertion about what the code
implements, so it should ship with that code and be impossible to separate from it.
A file invites the failure that costs the most support time: an operator upgrades
the binary, the stale file stays, and the region now reports a version it does not
implement — a mismatch that presents as inexplicable misbehaviour rather than as a
clean refusal at registration.

#### Three details worth getting right

**Check at lease renewal, not only at first registration.** Otherwise a region that
registered before the protocol incremented keeps renewing indefinitely and the
guarantee quietly rots. Checking on renewal
([`Renew` / `RenewProvisioned`](../grid/internal/regions/store.go:38)) makes
eviction fall out of the lease cycle: increment the protocol, and non-matching
regions drain within one lease period with nobody chasing them.

Because the number moves rarely, this check refuses nothing almost all of the time.
That is intended rather than dead code — it is the mechanism that makes an increment
take effect at all, and it has work to do only on the rare occasion of one.

**Name the version in the refusal.** "Region is running region protocol 1; this
grid requires 2 — upgrade the region software" is actionable; a bare
`registration_refused` is not. Same rule as the client's own probe, pointed the
other way, and a region operator is as much a user here as the person at a viewer.

**Announce an increment before enforcing it.** Under matching semantics the
current number cannot warn anyone — a region that still registers necessarily
matches it. So the registration reply carries the grid's current region protocol
**and, when an increment is scheduled, the upcoming value**, letting region
software that already knows about the new protocol report itself ready and any
other region warn its operator in its own log ahead of the cutover. Matching
semantics are also why this matters more than it would under a floor: there is no
window in which a lagging region still works, so warning ahead of the increment is
the only kindness available. A region owner whose first notice is their region
dropping off the map has been treated badly for no gain.

#### What this does and does not buy

It defends against **stale**, not **hostile**. A region self-reports its version
and regions are untrusted, so a region can lie — and under ADR 0028 that is
expected rather than surprising. It is also not much of an attack: a region that
claims protocol 2 and cannot serve it breaks its own users' sessions, while the
grid's real defenses — grid-owned metadata, verified fetch, vault durability — do
not depend on the claim at all.

So the honest statement in the probe is that **every region this grid leased
claims the required region protocol**, which is far stronger than advisory and
still not a guarantee about any individual region. The client keeps reading the
per-region manifest, for two reasons that survive the match rule: a capability can
be protocol-supported yet disabled by a region's own configuration — voice and
guest access are operator choices, not protocol facts — and a region may misreport
or simply vanish, which the client has to handle regardless.

### The welcome region

The probe names a default landing region as `{name, gridX, gridY}` — no endpoint,
because world entry returns that and the probe should not become a public region
directory.

**Homeworldz had no such concept, and its absence was a latent bug for viewers
rather than merely a gap**: `resolveDestination` used to try the requested
region, then a preferred region id, then fall back to `items[0]` — whatever the
region list happened to return first — so where a viewer with no last location
landed was undefined and changed as regions came and went. Both halves are now
shipped (2026-07-26): the welcome concept (`[grid] welcome_locations`, the
probe's `welcome` field, world entry resolving against the list), and
`resolveDestination`
([grid/internal/httpapi/viewer_login.go:391](../grid/internal/httpapi/viewer_login.go:391))
resolving on the same shared arrival logic, with the first-region fallback
surviving only on grids that configure no welcome list.

**1000,1000 is already the convention**, though only in test fixtures — the
`Welcome` region sits there across
[provisioning](../grid/internal/provisioning/regions_test.go:14),
[map tiles](../grid/internal/httpapi/map_tile_test.go:138), and
[transit](../grid/internal/httpapi/transit_test.go:102) fixtures, and
[identity/store_integration_test.go:22](../grid/internal/identity/store_integration_test.go:22)
states it in a comment.

It should be **explicit grid configuration, not a coordinate hardcoded in the
resolver**. A grid operator has to be able to move their welcome region without a
code change, and a positional rule silently changes meaning the moment somebody
parks something else on that tile.

The setting itself is the **new-arrival list** under "Default and fallback arrival
points" below — this probe field is derived from its first entry rather than
configured separately, so the probe and the resolver cannot disagree about where
new users land. 1000,1000 stays as the documented convention for where a grid
puts its welcome region, not as a default baked into code: with no arrival list
configured, the grid has no welcome region to report and the probe omits the
field, which is honest — inventing one from whatever region sorts first would
just re-create `items[0]` with better packaging.

### The three outcomes must stay distinct

Client ADR 0003 is explicit that conflating these misreports the cause, so the
implementation must keep them separate:

| Condition | What the client reports |
| --- | --- |
| 404, or no `client` object | This grid does not support the client |
| `protocol` below the client's minimum | The grid needs upgrading — name the version |
| Document fine, QUIC fails, WebSocket works | A network condition, not incompatibility |

The second row is why the document reports a version even when it is too old to
use: "upgrade to protocol 2" is actionable and "incompatible" is not.

The probe is checked **before any transport is attempted**, deliberately. An
absent endpoint answers immediately; a QUIC attempt against a region that ignores
UDP on that port may hang until timeout.

### Per-region capabilities are not here

This is a **grid** document. What a *region* supports arrives as a versioned field
in the session-open response below, because regions within one grid are not
uniform — [ADR 0028](adr/0028-untrusted-region-trust-model.md) admits regions
outside the operator's control — and a region crossing must re-resolve them.
Putting them in the probe would be both wrong and stale.

## Arrival on the grid user tier

The client reaches a region through the public `/v1` tier and never through
`/api/v1`, whose service token authorizes access well beyond a single user
([ADR 0007](adr/0007-internal-request-boundary.md)).

- **`POST /v1/tokens` already exists** and needs no change. It issues a bearer
  token ([grid/internal/api/auth_handlers.go:115](../grid/internal/api/auth_handlers.go:115)),
  so there is no new authentication surface to build.
- **`POST /v1/client/session`** is new: world entry. It resolves a destination
  region, returns its endpoint, returns the region's capability manifest as a
  versioned field, and mints a region ticket.
- **`GET /v1/client/channel`** is the grid channel WebSocket upgrade described
  under the communication mechanisms below.

Every `/v1/client/*` route derives the acting user **from the bearer token and
never from the path**. The internal tier addresses users positionally —
`/api/v1/inventory/{userId}` and its neighbours — and mirroring that shape on a
user-facing route would let any authenticated caller read another user's inventory
and last known location. The pattern to follow already exists:
`requireAuth` returns the account by value
([grid/internal/api/auth.go:24](../grid/internal/api/auth.go:24)), so a handler
takes the user from `account.ID` and there is no path segment to get wrong.

### What session open carries

As implemented in
[grid/internal/api/client_session.go](../grid/internal/api/client_session.go).
The request is a `POST /v1/client/session` with the account bearer token from
`POST /v1/tokens` and a body of exactly one JSON object — the decoder rejects
unknown fields and trailing data, so the body behaves as
`additionalProperties: false`:

```json
{ "start": "last" }
```

`start` is the only request field, and it is optional:

- **`"last"`** (also the default when `start` is omitted or empty) — the user's
  stored last location, falling back to the welcome arrival list when it is
  missing or its region is offline.
- **`"home"`** — the stored home location, with the same fallback.
- **`"Region Name/x/y/z"`** — an explicit arrival point. An explicitly named
  region that is not online is a `404`, never a diversion: the user asked for
  somewhere particular. The legacy `uri:Region&x&y&z` spelling is not accepted.

The keywords match case-insensitively. A `start` that parses as none of the
three is a `400 invalid_start`.

The response is `200` with `Cache-Control: no-store`:

```json
{
  "session": {
    "id": "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb",
    "expiresAt": "2026-07-27T08:14:00Z"
  },
  "region": {
    "id": "22222222-2222-4222-8222-222222222222",
    "name": "Welcome",
    "gridX": 1000,
    "gridY": 1000,
    "endpoint": "https://welcome.example",
    "position": [127, 127, 23]
  },
  "ticket": {
    "token": "eyJhbGciOi…",
    "expiresAt": "2026-07-26T20:19:00Z"
  },
  "capabilities": {
    "version": 1,
    "transports": ["websocket"],
    "sessionURL": "wss://welcome.example/session"
  }
}
```

- **`session`** — the row in the shared session store, common to all client
  kinds. `expiresAt` is RFC 3339; the TTL is 12 hours, matching viewer
  sessions, so one expiry and revocation story covers both.
- **`region`** — the resolved destination. `endpoint` is the region's public
  endpoint with any trailing slash trimmed. `position` is `[x, y, z]` in
  region-local metres and is **optional**: it is present when an explicit
  arrival point named coordinates, when a welcome arrival point supplies its
  own, or when a stored location survived resolution — and a stored position
  survives only when the stored region is the region actually selected, so a
  diversion can never leak a stale position into the wrong region. When
  absent, the region chooses the spawn.
- **`ticket`** — the region ticket: a token with audience `homeworldz:region`
  binding this `region.id` and `session.id`, the only credential a region
  ever sees. It is short-lived (`[website] region_ticket_ttl_seconds`,
  default 300) and single-purpose: presenting it on any `/v1` account route
  is refused, and the account token is refused wherever the ticket is
  expected — the audience separation is structural.
- **`capabilities`** — the versioned per-region manifest, resolved for this
  region and re-resolved on every region crossing. `transports` lists what
  this region's session transport serves — `["websocket"]` for a region that
  reported a session endpoint at registration, empty for one that serves
  none — and `sessionURL` (present exactly when `transports` is non-empty)
  is where to connect. The session opens with the same envelope protocol as
  the grid channel: mandatory first-message `auth` carrying the **ticket**
  (not the account token), answered by `hello {region, identity}`, then
  `ping`/`pong`/`error`, and server-initiated `chat {from, message}` for the
  region's public chat. A client treats the manifest as data and adapts
  rather than negotiating.

Errors are the flat object every `/v1` route uses: `{code, message}` plus an
optional **`field`** naming the request field at fault (`"message"`, `"to"`,
…) when the error is about one input rather than the request as a whole — it
is what lets a client highlight the offending input instead of the form.
The table below omits `field` for brevity:

| Status | `code` | Meaning |
| --- | --- | --- |
| 400 | `invalid_json` | The body is not exactly one JSON object, or carries an unknown field |
| 400 | `invalid_start` | `start` is not `last`, `home`, or `Region/x/y/z` |
| 401 | `unauthorized` | Missing or expired bearer token |
| 404 | `destination_unavailable` | The explicitly named region is not online |
| 405 | `method_not_allowed` | Only `POST` is supported |
| 503 | `destination_unavailable` | No online region can accept the session (the implicit paths exhausted the welcome list), or the selected region's endpoint is invalid |
| 503 | `world_entry_unavailable` | The tier is running without the stores world entry needs |

`destination_unavailable` appears at both `404` and `503` deliberately: the
status carries the distinction. `404` says *the region you named* is the
problem; `503` says *the grid right now* is.

### More store wiring than the ADRs assume

Client ADR 0003 states these are "handlers over existing state rather than a
proxy," with only inventory and assets missing. That understates it. The `/v1`
tier is a **separate binary** from the internal tier — `cmd/homeworldz-api` over
`internal/api`, against `cmd/grid` over `internal/httpapi` — and it holds only
accounts, provisioned regions, a narrowed lease store, and presence
([grid/internal/api/api.go:50](../grid/internal/api/api.go:50)).

Missing for world entry:

- **`locations`** — last position and home, which is what a session opens at.
- **`identity`** — session records, if the client's session reuses that table
  rather than getting its own.
- **`LeaseStore.List`** — the interface there is deliberately narrowed to `Get`
  and `DeregisterProvisioned`, and `List` is what destination resolution needs to
  find a region by name.

The legacy equivalent worth reading first is `resolveViewerLogin`
([grid/internal/httpapi/viewer_login.go:211](../grid/internal/httpapi/viewer_login.go:211)),
which is already wire-format-independent and shared between the XML-RPC and LLSD
login paths. Its `loginFields` seam is the natural place to add a third caller —
but it lives in `httpapi`, not `api`, so either the resolution logic moves to a
shared package or the public tier grows its own. That choice is open below.

### Destination resolution is shared; the login flow around it is not

**Decided: the domain logic beneath `resolveViewerLogin` moves to a package both
binaries import, and `resolveViewerLogin` itself stays where it is.**

What moves is destination resolution, the last/home location lookup that feeds it,
the arrival-point selection below, and the provisioned-size lookup.
`resolveDestination`
([grid/internal/httpapi/viewer_login.go:391](../grid/internal/httpapi/viewer_login.go:391))
is already a package-level function over a store interface rather than a method on
`*API`, so it was written in the shape this needs and moving it is close to
mechanical. Both binaries already import `regions`, `provisioning`, and `presence`,
so a shared domain package is the established pattern rather than a new one.

What stays is everything legacy-shaped, and the function makes the case by itself:
it authenticates an MD5 `$1$` password hash, allocates a circuit code, calls
`AssignViewerDestination`, and returns a `loginFields` carrying a seed capability
URL. The client authenticates at `POST /v1/tokens`, has no circuit, and must never
see a seed capability. Sharing the whole function would pull exactly the structure
client ADR 0003 exists to exclude, and would leave the new path's correctness at
the mercy of edits made for a viewer's benefit.

**Proxying the internal tier is rejected**, and not only because client ADR 0003
asks for handlers over existing state. That tier is guarded by a service token
authorizing far more than one user, so the public binary would have to hold a
grid-wide credential in the process facing the open internet. No amount of avoided
duplication is worth that.

### Default and fallback arrival points

Homeworldz had neither; both paths now resolve on the welcome list
(`[grid] welcome_locations`, shipped 2026-07-26 — world entry first, the
viewer login later the same day). `resolveDestination` distinguished three
situations and handled none of them well:

| Situation | Today |
| --- | --- |
| No last location (a new arrival) | `items[0]` |
| Home or last region unavailable | `items[0]`, silently |
| A named region requested but absent | Hard error, no fallback |

`items[0]` is the first row of `List`, ordered `grid_y, grid_x`
([grid/internal/regions/store.go:212](../grid/internal/regions/store.go:212)) — so
it is the south-west-most leased region. That is deterministic but meaningless, and
it moves as regions come and go, which makes "where does a new user land" a
question with no stable answer.

These are the **two cases Halcyon separated**, verified against its source
(`OpenSim/Framework/Communications/Services/LoginService.cs`):

- **`defaultlogins.txt`** — "Default login locations for new users," consulted when
  `LastLogin == 0`: where a first-time user lands.
- **`defaultregions.txt`** — "Default region locations," consulted for a returning
  user whose home or last region cannot be reached: where to divert.

Separating them matters: the region a new arrival should see and the region to
divert someone to when their home is down are different choices, and collapsing
them means an outage in someone's home region silently deposits them at the
newcomer arrival point.

Entries are one arrival point per line, parsed against
`^(?<region>[^&]+)/(?<x>\d+)/(?<y>\d+)/(?<z>\d+)$`:

```
Welcome/127/127/23
Welcome/127/129/23
Welcome/129/127/23
Welcome/129/129/23
```

**Selection is ordered fallback, not rotation.** `PrepareNextRegion` walks the list
with a `foreach` and takes the first entry whose region accepts the login; there is
no counter and no shuffle. Spreading across entries happens only when an earlier
entry fails — the list is primarily a resilience mechanism, with distribution as a
side effect. Worth stating precisely, because it changes what the list means: entry
order is priority order.

**Arrival points, not just regions.** Each entry carries a position, and that has a
consequence worth stating because it changes a signature: `resolveDestination`
returns a `regions.Region` and no position at all — arrival position is currently
left to the region. Adopting arrival points means resolution returns a region
**and** a position, which touches the viewer path too: it would populate
`startLocation` instead of leaving it to `normalizeStart`.

**Configuration, for now and later.** Grid configuration is the interim home for
this, not text files; a grid configuration admin surface on the website is the
intended end state, and reading `defaultregions.txt` from disk would be building
the thing we mean to replace. For now: a single arrival point in the grid
configuration file.

Two decisions make the interim cheap to outgrow. **Parse Halcyon's
`Region/x/y/z` form**, because it is what operators already know from SLURLs and
what the eventual admin UI would show. And **make the setting a list from the
start**, holding one entry today: a single value would have to change shape when
the list grows, whereas a one-element list simply grows in place, and the admin
surface later edits the same field rather than replacing it. Whether new-arrival
and diversion lists stay one setting or two can also wait — starting with one
setting read for both cases loses nothing, since the split is additive.

**Selection: keep Halcyon's ordered fallback, add a random starting index.** The
walk-until-accepted loop is the part that earns its keep — it is what makes the
list a resilience mechanism. Starting the walk at a random index adds the
distribution Halcyon only got as a side effect of failures, and it stays stateless:
a round-robin counter would need coordination across two binaries and a restart
story, and strict alternation buys nothing over random here — the goal is not
landing on every point equally, it is not landing everyone on one.

### The region ticket

The account token **never reaches a region**. It reaches account management
including password change, and ADR 0028 admits regions the operator does not
control, so handing it over would trade the whole account for a scene. World entry
mints a short-lived, region-scoped ticket instead.

ADR 0032 describes this as "a second signer with a distinct audience," which is
the right shape but not a drop-in.
[grid/internal/webtoken/token.go:47](../grid/internal/webtoken/token.go:47) has a
fixed claim set with `DisallowUnknownFields` on verify, and `Sign` takes website
identity arguments — userid, display name, rez date, privileges, auth version.
There is nowhere to put a region id.

**Decided: the ticket carries a `regionId` claim, and the region-ticket signer
carries its own audience.** Both, because they answer different questions.

- The **audience** separates a region ticket from an account token, structurally
  rather than conventionally. `Verify` already rejects a mismatched audience
  ([token.go:156](../grid/internal/webtoken/token.go:156)), so an account route
  refuses a region ticket and a region refuses an account token with no new code.
- The **`regionId` claim** binds the ticket to one region, so one region's ticket
  is refused by another. A distinct audience alone cannot do this without a signer
  instance per region.

This adds an optional field to `payload` and one argument to `Sign`. It is safe
against `DisallowUnknownFields` in both directions as long as the field is
`omitempty`: an account token omits it, a region ticket carries it, and one struct
verifies both. Worth noting because that decoder is strict by design and a
non-optional field would break every existing token.

Nothing here touches LLSD or the UDP circuit protocol. These tokens are the public
tier's own credential and never reach a viewer, whose session identity is the
separate `identity` viewer-session and circuit-code path — so there is no legacy
wire format for a new claim to collide with.

One honesty note: unlike the audience check, the `regionId` comparison is an
explicit check the region has to perform, which means it is a check that can be
forgotten. It needs a test that asserts the refusal, not just one that asserts the
happy path. Client `PLAN.md` already lists cross-region ticket rejection as a
proof-of-concept acceptance item for this reason.

The ticket also wants a short lifetime, which region handoff needs anyway for
clean revocation.

### One session store for every kind of client

**Decided: the client session is a row in the existing `sessions` table, not a
parallel table.** A session — this user is in-world, here, until then — is the
same fact whether a viewer or the Homeworldz client produced it, so it should be
one store that everything downstream shares.

The table already has the right shape. Its row is
`{id, user_id, expires_at, secure_session_id, viewer_circuit_code,
destination_region_id}`
([grid/internal/identity/store.go:31](../grid/internal/identity/store.go:31)), and
the two viewer-specific columns are nullable, filled in later by
`AssignViewerDestination`. The viewer specifics were built as optional attachments
rather than the table's spine; a client session is simply a row whose circuit code
stays null.

Three shared mechanisms fall out of reuse, and each would have to be duplicated by
a second table:

- **Crossing.** The transit machinery validates the session ID when a region hands
  an agent to its neighbour
  ([grid/internal/httpapi/transit.go:30](../grid/internal/httpapi/transit.go:30)).
  Crossing is the one flow both kinds of session must share — an avatar walking
  over a border does not care which program renders it — and with one table the
  client inherits it unchanged.
- **Revocation.** A JWT cannot be revoked, only outlived, so the region ticket
  alone gives "log this user out" no server-side handle. The ticket therefore
  carries the **session ID as a claim**, and the region confirms the session with
  the grid when the client opens its session. Revoking the row then ends a client
  session the same way it already ends a viewer's.
- **Presence.** Keyed by user, not by session, so one online/offline story covers
  both kinds of client with nothing added.

What the client never touches is `CreateViewerSession`, which authenticates the
legacy MD5 password digest. World entry has already authenticated the bearer
token, so it needs a small new entry point — `CreateClientSession(ctx, userID,
duration)` — inserting the row directly through the existing `insertSession`,
which already takes exactly those arguments. The 12-hour viewer TTL is a parameter
at that call site, not a property of the table, so the client's session length is
its own choice.

Client ADR 0003 is not in tension here: its boundary is wire encodings, and a
Postgres table carries no LLSD. The cost being accepted is coupling — the client's
session lifecycle now lives in a table the viewer path also evolves, and any
client-only session state would land as another nullable column. That is the trade
the crossing and revocation machinery is worth, and nullable columns are already
how this table accommodates variation.

## The communication mechanisms

Six paths, all of which now ship — the region session in its observer
milestone. The legacy three are untouched by everything above.

| Path | Used by | Transport | Encoding | State |
| --- | --- | --- | --- | --- |
| Circuit protocol | viewers | LLUDP | legacy binary | Shipped |
| Capability HTTP | viewers | HTTP/1.1 | LLSD | Shipped |
| `EventQueueGet` | viewers | HTTP/1.1 long poll | LLSD | Shipped |
| REST bootstrap | client | HTTPS | JSON | Shipped (probe, tokens, world entry) |
| Grid channel | client | WebSocket | JSON | Shipped (system notices; instant messages with store-and-forward; presence and offers have no producers yet) |
| Region session | client | TLS WebSocket now; WebTransport when the RFC lands ([CLIENT2-TRANSPORT.md](CLIENT2-TRANSPORT.md)) | JSON | Shipped (embodied per [CLIENT2-EMBODIMENT.md](CLIENT2-EMBODIMENT.md) E1: spawn, movement, scene updates, positioned chat; crossings and appearance pending) |

### Why the client holds two channels

The **grid channel** is anchored to the grid and carries what must reach a user
regardless of where they are: instant messages, presence, inventory offers, system
notices. It survives region crossings, which is exactly why it cannot live in a
region.

The **region session** is anchored to one region and carries object and avatar
updates, chat, and movement. Its volume and its interest management are properties
of one region's scene, so it is established per region and torn down on departure.

One channel fails either way. A single region-anchored channel dies on every
crossing, and crossing is a first-class state transition rather than a
reconnection. A single grid-anchored channel forces every scene update through an
extra hop away from the region that generated it, discarding the locality interest
management depends on.

After the bootstrap, **requests and server-initiated messages share the realtime
channel** with correlation identifiers pairing a response to its request. REST is
used for the probe, login, and world entry, and then not again. Long-polling is
dropped outright rather than carried forward: push is what these channels are for.

### Encoding: JSON on both channels, discriminated by first byte

**Decided: both channels start as JSON**, on both channels' merits rather than as
a compromise. The grid channel's traffic — instant messages, presence, inventory
offers — is low-rate and string-heavy, so compactness buys nothing measurable. The
region session's rates are exactly what ROADMAP2's Phase 2 throwaway client exists
to measure; committing to a schema'd binary format ahead of that measurement would
put codegen into Go, C++, and the WASM build on a guess, and client ADR 0002's
native-and-WebAssembly rule makes every such dependency cost double. JSON costs no
dependency anywhere: the browser parses it natively, Go marshals it in the
standard library, and the `/v1` tier it extends is already JSON end to end.

**The escape hatch is the first byte of each message.** A JSON protocol message
begins with a known signature — `{` for the message envelope (`[` and, if comments
were ever allowed, `#` are equally distinctive) — so the encoding of every message
is self-describing at a glance. A future binary encoding announces itself the same
way with any other leading byte, the way protobuf-framed or other tagged formats
would. That yields three properties for free:

- **Additive migration.** If measurement ever shows the hot path needs a packed
  form, those message types switch encodings individually — a receiver routes on
  the first byte, so JSON and binary coexist on one channel with no flag day and
  no version negotiation.
- **Clean refusal.** A receiver seeing a signature byte it does not support says
  so — "unsupported message encoding" — instead of feeding garbage to a JSON
  parser and reporting a syntax error three fields in.
- **Nothing to build now.** The rule costs one documented sentence and one
  `switch` on a byte. The binary format it leaves room for is deferred until
  needed, which may be never.

### What the grid channel carries today

As implemented in
[grid/internal/api/client_channel.go](../grid/internal/api/client_channel.go).
Every message either way is one envelope:

```json
{ "type": "…", "version": 1, "correlationId": "…", "payload": { } }
```

`version` versions the envelope shape. `correlationId` is optional and pairs a
reply with its request; server-initiated messages omit it.

Client → server: **`auth`** (`{token}`, mandatory first message, within 10
seconds — a browser cannot set an `Authorization` header on a WebSocket; the
account token is resolved exactly as REST `requireAuth` resolves it, and a
region ticket is refused on its audience) and **`ping`**.

Server → client: **`hello`** (`{grid, identity}`, confirms auth), **`pong`**
(echoes the ping's `correlationId`), **`error`** (`{code, message}`, answers a
bad message without costing the connection), and **`notification`** —
server-initiated, no correlation:

```json
{ "kind": "system_notice", "message": "maintenance in ten minutes", "sentAt": "2026-07-26T22:40:00Z" }
```

`kind` names the notification family. Two exist:

- **`system_notice`** — produced by `POST /v1/admin/users/{id}/notice`
  (privilege `users`, body `{message}`, reply `{delivered}` counting the
  connections that took it). Best-effort to currently open channels; nothing
  is stored for an offline user, because a notice describes durable state a
  client re-reads on its next connection anyway.
- **`instant_message`** — the first store-and-forward kind, produced by
  `POST /v1/client/messages` (body `{to, message}`, the sender is the bearer
  token's account, reply `{id, sentAt, delivered}`). Payload:
  `{kind, id, from: {id, userid, displayName}, message, sentAt}`. A message
  is **stored before any delivery is attempted**; live delivery reaches the
  recipient's open channels, and otherwise the message replays — in sent
  order, before anything else — on the next channel connection they open.
  The `id` is stable across live delivery and replay so a client can
  de-duplicate. "Handed to a connection" counts as delivered; there are no
  read receipts. **A sender is never sent their own message** — the POST
  reply is the outgoing side of the conversation (it carries the `id` and
  `sentAt` a rendered line needs), so a client that renders conversations
  from channel traffic alone shows only half of every exchange, and a
  self-send test hides exactly that mistake.

Still without producers or tables: inventory offers and friendship requests.

Message envelopes are versioned from day one — `{type, version, correlationId,
payload}` — so a message shape can evolve without a flag day of its own. The
correlation identifier is the request/response pairing mechanism the multiplexed
channel already requires; it lives in the envelope rather than the payload so the
router never parses what it only forwards.

### What this costs region-side

More than the one roadmap checkbox suggests, and it is worth being blunt about it.

The region has **no HTTP library and no TLS**. It hand-rolls HTTP/1.1 over raw
sockets in `main.cpp`, with listeners created at
[region/src/main.cpp:1570](../region/src/main.cpp:1570) and a `select()` loop over
exactly two descriptors at [main.cpp:2263](../region/src/main.cpp:2263) whose
`highest` computation assumes those two. The root
[vcpkg.json](../vcpkg.json) has four packages and none is network-related, and
[region/src/grid_client.cpp:51](../region/src/grid_client.cpp:51) actively rejects
`https://` URLs.

WebTransport requires QUIC over TLS 1.3, and a browser will not open a plaintext
WebSocket from an HTTPS page either, so the fallback needs TLS as well. This is
the region's first networking dependency, first TLS dependency, and a restructure
of its accept loop. It is the largest single item in Phase 1 and the one least
adjacent to anything that already exists.

Grid-side the grid channel is smaller but not free: the Go module has four direct
dependencies and nothing for WebSocket or QUIC, and
[webtoken](../grid/internal/webtoken/token.go:6) hand-rolls JWT specifically to
keep that set minimal. Adding `quic-go` or a WebSocket library is a deliberate
departure from that stance and should be recorded as one.

### Library choices

**The version-floor rule.** Dependency choices confined to one deliverable are
made freely on their merits. A dependency whose version constrains an
**independent deliverable** — both ends of a communication path, or software
run by people the operator does not employ, region owners above all — instead
requires the **lowest long-term-supported version that satisfies the need**, so
no upgrade burden is exported downstream. PostgreSQL is the worked example:
grid-side only, and 18.x is specifically approved as the minimum. The C++
QUIC/WebTransport stack and the shared protocol library below sit on both
sides of the wire, so this rule governs both.

**Go WebSocket: `coder/websocket`, decided now.** The realistic field is two —
`gorilla/websocket`, the long-standing default that was archived in 2022 and later
revived under new maintainers, and `coder/websocket` (formerly
`nhooyr/websocket`), smaller and context-aware throughout, closer to this module's
four-dependency ethos. Hand-rolling RFC 6455 is thinkable for a codebase that
hand-rolled its JWT, but a WebSocket endpoint on the public tier parses hostile
input from the open internet and framing bugs there are security bugs; this is the
place to take the dependency. Nothing downstream changes with the choice, so
deciding early costs nothing.

**C++ region-session transport: decided 2026-07-27 — WebSocket first, on
libwebsockets.** The full comparison and the decision are in
[CLIENT2-TRANSPORT.md](CLIENT2-TRANSPORT.md); the short form is that the
provisional msquic-versus-ngtcp2 slate dissolved on inspection (neither ships
the WebTransport layer browsers speak; the stacks that do each fail a build
constraint), and WebTransport is still a moving draft that the version-floor
rule above cannot pin. The region session ships over TLS + WebSocket
(`transports: ["websocket"]`, RFC 6455, libwebsockets from vcpkg, covering
the listener and the call-home relay leg alike); QUIC/WebTransport reopens
when the RFC publishes or Phase 2 produces rate numbers, whichever comes
first.

### A shared C++ protocol library for the region session

Both ends of the region session are C++ — the region here, the client core in its
own repository — and most of what the region-side implementation needs (envelope
encoding, first-byte discrimination, correlation tracking, message type
definitions, framing over the transport) is work the client core would otherwise
write a second time. **The direction is to build the region-session protocol layer
as a library intended for both ends**, so the two implementations cannot drift:
one definition of every message, one encoder, one set of tests exercising both
roles.

Constraints that shape it, recorded before any code exists:

- **It must satisfy client ADR 0002's rule** — building natively and for
  WebAssembly — or the client cannot adopt it and the point is lost. That
  constrains its dependencies to effectively none beyond the standard library,
  which suits a protocol layer.
- **It carries no transport.** The QUIC/WebSocket stacks differ per end (the
  region's listener, the browser's native implementation, the native client's
  library); the shared layer is everything above the byte stream and nothing
  below.
- **It lives in this repository** with the region that ships first, structured for
  consumption from the client repository later — how it is consumed (vendored,
  submodule, or split out) is decided when the client core exists, per the
  sibling-repo boundary.
- The grid's Go side of the WebSocket fallback and grid channel implements the
  same wire format from its documentation. Two implementations across two
  languages is the floor; the library keeps it from becoming three in one
  language.

### Reaching home-hosted regions: direct where possible, relayed where not

The transport work above assumes a client can dial the region, and for the
audience [ADR 0028](adr/0028-untrusted-region-trust-model.md) is designed around —
regions at home, behind consumer routers and increasingly behind carrier-grade NAT
— that assumption fails. Port forwarding is the legacy world's answer; it is also
the single largest source of "my region doesn't work" support burden in that
world, and under CGNAT it is not merely hard but impossible.

**The direction: a region may serve its session traffic through an outbound
"call home" connection to the grid instead of a listening socket.** The region
dials out — the direction consumer NAT always permits, and the direction the
region already speaks for registration and renewal — holding a persistent
connection to a grid relay. A client's region session then terminates at the
relay, which splices it onto the region's outbound connection. This is not an
ngrok: it carries exactly one protocol, ours, between our client and our region.

What makes this cheap to design in now and expensive to retrofit later is the
arrival path already built above: **the client learns its region endpoint from the
session-open response, as data.** A relayed region hands out a grid relay URL, a
directly reachable region hands out its own — the client cannot tell the
difference and never needs to. The region ticket authenticates the session
identically either way. No client work exists in this feature at all.

The relay also quietly disposes of the hardest part of the TLS question. A region
serving directly must terminate TLS itself, which means a certificate on every
home machine — plausibly grid-issued at lease time, since the grid is already the
trust anchor, but a real design problem. A relayed region needs **no certificate
and no listening socket**: TLS terminates at the grid's edge like every existing
`/v1` connection, and the region's outbound leg is an ordinary client-side TLS
connection. For a home operator the difference is "it works when the region
starts" versus an evening of router configuration.

**The honest cost is bandwidth, and it decides the default, not the feasibility.**
Object updates flowing region-to-clients dominate this traffic by a wide margin,
and every relayed session routes that flow through the grid twice — in from the
region, out to the client — so relay egress scales with relayed sessions and the
grid pays for it. That is a hosting bill, not an architectural failure, and it is
bounded by the obvious policy: **direct is preferred, relay is the fallback**,
chosen per region — a region that is reachable serves its own traffic. The
per-session rates that size the bill are precisely what the Phase 2 prototype
measures, so the relay's economics get real numbers before any of it is built.

**Which mode a region gets is declared on the call-home connection, then
verified.** Direct TLS service stays fully supported — the relay is an option, not
a funnel — and the region itself reports whether it accepts direct connections,
carrying its direct endpoint on the same connection it already holds to the grid.
The report is a claim, not a fact: home operators are routinely wrong about their
own reachability — the port forward that worked last month, the ISP that moved
them behind CGNAT silently — so the grid **verifies by dialing the claimed
endpoint back** before ever handing it to a client. A claim that fails
verification demotes the region to relay and tells the operator why, which turns
the classic silent failure of the legacy world — a region that registered happily
but times out for every visitor — into a diagnosed condition at startup. Session
open then hands each client whichever endpoint the region's verified mode
dictates, and the client, as ever, cannot tell the difference.

Two considerations recorded rather than resolved:

- **True peer-to-peer is rejected for now.** A browser cannot dial an arbitrary
  host except over WebRTC, so genuine P2P would mean a third transport stack with
  ICE negotiation on both ends — and its own relay fallback (TURN) for exactly the
  NAT cases above, converging on what the grid relay already is. Two transports
  are enough.
- **IP exposure.** Direct serving reveals the region owner's home IP to every
  connecting client and each client's IP to the region — the legacy world's status
  quo, and an IP is modest as personal data goes. The relay hides both as a side
  effect. Worth stating in operator-facing documentation so a home operator
  chooses reachability versus privacy knowingly, rather than treating either as a
  guarantee the design does not make.

## The three extensions

Each is described by what the server does, then by **who can see it** — because
the interesting question is which of the three can also be offered to viewers
through their own request mechanism, and the answers differ.

### Modern asset formats at rest

KTX2/Basis for textures and glTF 2.0 for meshes become the authoritative blobs.
JPEG2000 and the Second Life mesh serialization become derived, cache-tier
down-conversions generated when a viewer asks. The region absorbs the legacy
conversion cost so the client carries none of it.

**Where it goes.** Uploads are validated by a magic-byte sniff in
`valid_new_file_inventory_upload_content`
([region/src/viewer_capabilities.cpp:543](../region/src/viewer_capabilities.cpp:543))
and then stored byte-for-byte unmodified
([main.cpp:2693](../region/src/main.cpp:2693)). That call site is the
normalization seam. Note that blobs are content-addressed by SHA-256 at store
time ([region/src/region_storage.cpp:726](../region/src/region_storage.cpp:726))
while the viewer-facing asset UUID is minted earlier, so rewriting bytes changes
the blob hash and not the asset identity — which is precisely the separation
[ADR 0027](adr/0027-asset-blob-instance-separation.md) already established.

Down-conversions are regenerable derived data: cache, never authoritative, and
vault-exempt, following the precedent baked textures set in
[ADR 0029](adr/0029-server-side-appearance-baking.md).

**Textures are much cheaper than meshes.** JPEG2000 encoding already exists and
is already used — `encode_j2c` in [region/src/image.cpp](../region/src/image.cpp),
driven by appearance baking — so the texture down-conversion is mostly wiring. The
Second Life mesh serialization does not exist anywhere in the region; there is no
mesh, LOD, or vertex code at all, and `SimulatorFeatures::mesh` is hardcoded
`false` ([region/include/homeworldz/viewer_capabilities.h:195](../region/include/homeworldz/viewer_capabilities.h:195)).
Doing textures first is the obvious split.

**Who sees it.** Both, and this one needs no negotiation at all. A viewer asking
`GetTexture` gets the derived JPEG2000 and never learns anything changed, so this
is invisible rather than additive. The client gets the authoritative blob through
a new asset route on the modern path. The only viewer-visible change is that
`mesh` eventually flips to `true`, and that is an honest advertisement of new
behavior rather than an extension to opt into.

### Server-side prim meshing

A capability serves meshes for all geometry including prims, so the client never
implements prim tessellation.

**Where it goes.** This is real geometry work with no existing foundation.
`classify_prim_shape`
([region/src/physics_scene.cpp:74](../region/src/physics_scene.cpp:74)) maps path
and profile curves onto eight primitive shapes and **ignores cut, hollow, twist,
taper, and skew entirely**, even though `scene::Entity` stores all of them
([region/include/homeworldz/scene.h:91](../region/include/homeworldz/scene.h:91)).
A real mesher has to handle them. It must also stay consistent with `entity_mass`
([physics_scene.cpp:130](../region/src/physics_scene.cpp:130)), which derives mass
from the same classification, and reconcile with the portable collision
representations of [ADR 0023](adr/0023-portable-mesh-collision-representations.md).

**Who sees it.** The client only, in practice. A viewer does not want meshed
prims — it receives prim parameters and tessellates client-side, and no viewer
would ask for this. It *could* be advertised as a negotiated extension, and the
registry exists for exactly that, but there would be no caller.

There is a worthwhile side effect for viewers though. `physics_shape_convex` is
advertised `false` today because the value is accepted and persisted while the
collision shape is always derived from the prim's own shape
([viewer_capabilities.h:187](../region/include/homeworldz/viewer_capabilities.h:187)).
A mesher is what would let that flag become true honestly.

### Browser-reachable transport

Covered above as a mechanism. As an extension the only thing to add is what it
is **not**: it is not negotiated, not advertised in `SimulatorFeatures`, and not
offered to viewers. LLUDP stays fully authoritative for them. The client learns
the endpoint from the session-open response and arrives already knowing
the modern path is required.

The WebSocket fallback is for networks that block QUIC, **not** a degraded legacy
mode. Conflating the two would report a corporate firewall as an incompatible
grid.

### When an extension does get advertised

Two of the three above never appear in the registry, but the mechanism is still
how anything reaches a viewer, so the rules matter. Extensions are advertised
through a `HomeworldzExtensions` map in `SimulatorFeatures` beside the untouched
`OpenSimExtras`, each listing its own version and the capability names used to opt
in. A grant is whole rather than piecemeal — a partially negotiated extension has
no defined meaning. An unknown requested name is ignored rather than rejected, so a
newer client gets less instead of failing.

Registering one means adding a `RegionExtension` literal to
`available_region_extensions()`
([region/src/region_extensions.cpp:7](../region/src/region_extensions.cpp:7)) and
handling its path prefix in the capability dispatch. Note that
[region/tests/region_extensions_test.cpp:70](../region/tests/region_extensions_test.cpp:70)
asserts the registry is empty, so the first real extension changes that test.

Legacy non-regression is asserted on equality rather than inspection: with nothing
negotiated the seed reply is byte-identical to the pre-extension one, and the
baseline reply is a strict prefix of a negotiated one.

## Build order

The arrival path leads, because a client that cannot authenticate, cannot learn a
region endpoint, and cannot be told what a region can do has nothing to connect a
transport to.

0. **Bump the Go module.** Decided. `grid/go.mod` and `go.work` are on 1.21, which
   predates `ServeMux` method and wildcard patterns, so every route today is a
   prefix registration plus manual `TrimPrefix` and a method switch. Bumping first
   means `/v1/client/*` is written once in the modern style instead of being
   written twice. Note the installed toolchain is go1.21.3: naming a newer version
   in `go.mod` makes Go 1.21 fetch a matching toolchain automatically under the
   default `GOTOOLCHAIN=auto`, but CI and the cloud box should get a real install
   rather than depending on that. Existing routes can stay as they are; this is not
   a rewrite.
1. **The compatibility document.** `GET /v1/version` with the `client` object, a
   `Version` field on `api.Options`, the arrival-point lists in grid configuration
   (one welcome entry to start, from which the probe's `welcome` field derives),
   and the client origin added to `[website] allowed_origins`. Small,
   self-contained, independently testable, and it unblocks the prototype's first
   request.
2. **The version handshake.** The region protocol version compiled into the region
   binary and sent in `regions.Registration`, matched against the grid's required
   value at registration and at renewal, with the reply carrying the grid's current
   number. Both sides start at 1, so **enforcement begins with every region
   matching** — nothing is refused on day one.

   The urgency is in the field, not the enforcement. An increment can only ever be
   enforced if regions were already reporting a version, and retrofitting the
   report across a population of independently operated regions gets harder every
   month it waits. There is no version handshake between grid and region in either
   direction today, which makes this the cheapest it will ever be. It is also what
   the probe's `regions` group depends on, so it belongs beside step 1 rather than
   after it.
3. **World entry.** `POST /v1/client/session`, the store wiring it needs,
   `CreateClientSession` on the shared session store, and the region ticket with
   its own audience and its session-ID claim. Testable on its own — session-open
   shape, and one region's ticket refused by another — but not end to end, because
   it hands out a ticket for a transport that does not exist yet.
4. **The grid channel.** A WebSocket on the existing user-tier service, which is
   already browser-oriented with CORS and rate limiting in place. Provable against
   the prototype without any region work: a server-initiated message that survives
   a region crossing is the claim two channels rest on.
5. **The region session.** TLS, QUIC, the accept-loop restructure, and the
   WebSocket fallback. The big one, and the reason it is fifth rather than first.
6. **Texture normalization**, reusing the existing JPEG2000 encoder for
   down-conversion.
7. **glTF meshes and their down-conversion**, then server-side prim meshing.

Steps 1 through 4 are almost entirely Go on one binary — step 2 needs a version
field from the region and nothing more — and all four are provable against a
browser without a region transport. That is the cheapest possible path to knowing
whether this design survives contact.

## Open decisions

Recorded rather than guessed at, in the manner the client's own plan uses.

- **The C++ QUIC/WebTransport stack** — msquic (+ msh3) against ngtcp2 + nghttp3,
  quiche set aside for its Rust toolchain. Due with step 5, when there is a build
  to measure. The Go WebSocket choice is already made (`coder/websocket`).
- **TLS termination for directly serving home regions.** A relayed region needs no
  certificate; a direct one must terminate TLS itself, and grid-issued per-region
  certificates at lease time are the candidate, the grid being the trust anchor
  already. Due with step 5, and it gates direct serving as surely as the QUIC
  stack does.
- **Relay capacity and economics.** Whether the call-home relay is per-grid
  infrastructure or something regions opt into, and what its egress costs at real
  session rates — sized by the Phase 2 prototype's measurements before any relay
  is built.
- **Whether the region session ever needs a binary encoding** for hot-path scene
  updates. Deferred until the Phase 2 prototype measures real rates; the first-byte
  discrimination rule above means adopting one later is additive rather than a
  migration. May be never.

## References

- [ADR 0032: Region Extensions for the First-Party Client](adr/0032-region-extensions-for-new-client.md)
- [ADR 0030: Client Architecture and Engine Boundary](adr/0030-client-architecture.md)
- [ADR 0016: Firestorm Compatibility Target](adr/0016-firestorm-compatibility-target.md)
  — unaffected; viewers keep LLUDP and LLSD
- [ADR 0028: Untrusted Region Trust Model](adr/0028-untrusted-region-trust-model.md)
  — why the account token must never reach a region
- [ADR 0027: Asset, Blob, and Instance Separation](adr/0027-asset-blob-instance-separation.md)
  and [ADR 0029](adr/0029-server-side-appearance-baking.md) — why a
  down-conversion is another blob for the same asset
- [ROADMAP2.md](ROADMAP2.md) — the phase and milestone record this implements
- Client ADR 0003 and ADR 0004, in the client repository — the client's own
  decisions, which narrow ADR 0032: no legacy serialization at any layer, and two
  push channels in place of the long-poll event queue
