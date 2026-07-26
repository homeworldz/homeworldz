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

## The inversion worth recording

Across all three areas the **region carries the legacy shims and the new client
implements only the modern path**. That is the opposite of every previous
third-party Second Life viewer, which had to reproduce twenty years of client
behavior faithfully. It is possible only because Homeworldz owns both ends, and
it is what keeps the new client small enough to also run in a browser.

## Relationship to other ADRs

- **ADR 0016** — the compatibility target stands; all extensions are additive
  and negotiated.
- **ADR 0014 / 0026 / 0027 / 0029** — down-converted assets are derived,
  cache-tier, and vault-exempt.
- **ADR 0023** — portable mesh and collision representations, which the glTF and
  server-side-meshing paths must respect.
- **ADR 0030** — the client that consumes these extensions.
