# ADR 0032: Region Extensions for the First-Party Client

Status: Accepted

This ADR records **current expectation and intent**, not a commitment, and is
expected to be revised as evidence arrives.

**Implementation state.** The extension model below — the `SimulatorFeatures`
advertisement, named seed capabilities, map versioning, and the degradation
rules — is implemented in `region/src/region_extensions.cpp` and the region's
capability layer. The registry of available extensions is empty: it advertises
what the region can serve, so an extension is registered as part of its own
implementation rather than ahead of it. The three extensions this ADR
anticipates — modern asset formats at rest, server-side prim meshing, and the
browser-reachable transport — are **not built**, and none is advertised.

The first-party client ([ADR 0030](0030-client-architecture.md)) needs region
behavior that the Second Life protocol does not provide: modern asset formats,
geometry it does not have to tessellate, and a transport a browser can reach.
These are **region** decisions, recorded separately from the client's own
architecture.

Nothing here weakens [ADR 0016](0016-firestorm-compatibility-target.md). Every
extension is additive and invisible to legacy viewers.

## The extension model

Baseline protocol semantics are untouched. New behavior is negotiated
per-feature through the **`SimulatorFeatures`** capability and named **seed
capabilities**: a client that understands an extension asks for it, and one that
does not never sees it. No wire-format change, no version negotiation.

This is the mechanism Second Life itself used to ship mesh, materials, PBR, and
WebRTC voice, used as intended rather than worked around.

Negotiation exists to protect **viewers**, and **the client does not participate
in it.** Negotiation implies a fallback and a browser has none: it cannot open a
raw UDP socket, so a region that cannot serve the modern transport cannot serve
the client at all. `SimulatorFeatures` and the seed reply are also LLSD, so
negotiating through them would put an LLSD reader into the one client whose
purpose is to carry no legacy serialization.

The client requires the modern path and reaches it as described in "How the
first-party client arrives" below, per
[client ADR 0003](https://github.com/homeworldz/client/blob/main/docs/adr/0003-no-legacy-serialization-in-the-client.md).

## Modern formats at rest, legacy on demand

Assets are **normalized at upload**: **KTX2/Basis** for textures and **glTF 2.0**
for meshes are stored as the authoritative blobs. **JPEG2000 and the Second Life
mesh serialization are generated as derived, cache-tier down-conversions** when a
legacy viewer asks for them.

This inverts the usual arrangement — the region absorbs the legacy conversion
cost so the new client carries none of it — and it is consistent with existing
asset decisions:

- [ADR 0014](0014-content-addressed-assets.md) — blobs stay content-addressed.
- [ADR 0027](0027-asset-blob-instance-separation.md) — the asset, blob, and
  instance layers are unchanged; a down-conversion is another blob for the same
  asset.
- [ADR 0029](0029-server-side-appearance-baking.md) and
  [ADR 0026](0026-vault-authoritative-inventory-assets.md) — regenerable derived
  data is cache, never authoritative, and vault-exempt. Down-conversions follow
  the precedent baked textures already set.

## Server-side prim meshing

A capability lets a client request **meshes for all geometry**, including prims.
The new client therefore never implements prim tessellation at all. Firestorm
keeps receiving prim parameters and tessellating client-side exactly as today.

## Browser-reachable transport

A **WebTransport/QUIC session capability, with a WebSocket fallback**, carries
login, the event stream, and object updates. This is not a convenience: a browser
cannot open a raw UDP socket, so **LLUDP cannot serve a web client at all**, and
zero-install browser access is a primary goal of ADR 0030.

LLUDP remains the transport for legacy viewers. This is an added path, not a
replacement, and it also collapses the three transports a legacy session uses
(LLUDP, capability HTTP, and the long-poll event queue) into one multiplexed
stream for clients that take it.

The first-party client does not find this transport by negotiating for it. It
arrives already knowing the modern path is required, and learns the endpoint from
the grid's user tier as described below. The **WebSocket fallback is for networks
that block QUIC**, not a degraded legacy mode — conflating the two would report a
corporate firewall as an incompatible grid.

## How the first-party client arrives

The client reaches the modern surface through the grid's **public user tier**, the
`/v1` service, and never through `/api/v1`, whose service token authorizes access
well beyond a single user.

- **Compatibility is a probe, not a negotiation.** An unauthenticated version and
  capability document either satisfies the client's declared minimum protocol
  version or the grid is reported incompatible. It is checked before a transport
  is attempted, because an absent endpoint answers at once while a QUIC attempt
  against a region that ignores it may hang until timeout.
- **Login already exists.** `POST /v1/tokens` issues a bearer token, so no new
  authentication endpoint is needed for this client.
- **World entry needs new user-scoped `/v1/client/*` routes.** They must derive
  the acting user **from the token and never from the path**. The internal tier
  addresses users positionally, and mirroring that shape on a user-facing route
  would let any authenticated caller read another user's inventory and last known
  location.
- **Region credentials are separate from account credentials.** World entry mints
  a short-lived, region-scoped ticket rather than forwarding the account token,
  which reaches account management including password change.
  [ADR 0028](0028-untrusted-region-trust-model.md) admits regions outside the
  operator's control, so that token must never reach one. The user tier's signer
  already carries an audience and a lifetime, so a second signer with a distinct
  audience makes the separation structural rather than conventional.
- **Per-region capabilities arrive as data**, a versioned field in the
  session-open response. Regions within one grid are not uniform, and a region
  crossing re-resolves them.

Notifications that must outlive a region — instant messages, presence changes,
inventory offers — are a **grid** concern rather than a region extension. They are
recorded in
[client ADR 0004](https://github.com/homeworldz/client/blob/main/docs/adr/0004-client-transport-and-push-channels.md)
rather than here, along with the reason the client holds two channels instead of
one.

## The inversion worth recording

Across all three areas the **region carries the legacy shims and the new client
implements only the modern path**. That is the opposite of every previous
third-party Second Life viewer, which had to reproduce twenty years of client
behavior faithfully. It is possible only because Homeworldz owns both ends, and
it is what keeps the new client small enough to also run in a browser.

## Relationship to other ADRs

- **ADR 0016** — the compatibility target stands; every extension is additive,
  and negotiation is how a viewer is kept from seeing one.
- **ADR 0014 / 0026 / 0027 / 0029** — down-converted assets are derived,
  cache-tier, and vault-exempt.
- **ADR 0023** — portable mesh and collision representations, which the glTF and
  server-side-meshing paths must respect.
- **ADR 0030** — the client that consumes these extensions.
- **Client ADR 0003 and ADR 0004** — the client's own decisions, which narrow
  this one: no legacy serialization at any layer, the modern path required rather
  than negotiated, and two push channels in place of the long-poll event queue.
  Both state the server-side consequences above.
