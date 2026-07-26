# Homeworldz 2.0 Roadmap

This roadmap describes direction **beyond** the implementation sequence in
[`ROADMAP.md`](ROADMAP.md): a first-party client of our own, region protocol
extensions to serve it, and a second scripting language. It is organized the
same way — phases, milestones within each phase, and major work items within
each milestone.

Everything here follows from three architectural decisions:

- [ADR 0030: Client Architecture and Engine Boundary](adr/0030-client-architecture.md)
- [ADR 0031: Sandboxed Lua Scripting and SLua Compatibility](adr/0031-lua-scripting-subset.md)
- [ADR 0032: Region Extensions for the First-Party Client](adr/0032-region-extensions-for-new-client.md)

This document records **current expectation and intent**, not commitments.
Direction here is expected to change as evidence arrives — Phase 5 already
reflects one such change — and phases carry explicit gates where an outcome would
redirect the work.

Checkboxes describe the present state, not a promise of a release date. The only
implementation work that has started is Phase 1's extension negotiation
mechanism, which serves legacy viewers rather than the new client; the remaining
completed items are the decisions themselves.

## Relationship to the 1.0 roadmap

2.0 is **additive and does not replace 1.0**. Firestorm and compatible viewers
remain first-class throughout
([ADR 0016](adr/0016-firestorm-compatibility-target.md)), every region extension
is negotiated so legacy viewers never see it, and the 1.0 roadmap continues as
the primary sequence.

2.0 also *depends* on 1.0 rather than competing with it. The region, parcel,
crossing, inventory, and asset layers it builds on are 1.0 work, and the script
runtime boundary that makes a second language possible is 1.0's Phase 4. In
particular:

- Lua work should not begin before 1.0 Phase 4 has a real LSL host-function
  library and a stable snapshot ABI — the Lua runtime is a peer of Falcon, not a
  replacement, and reworking Falcon's foundations twice is the outcome to avoid.
- Region extensions assume the asset, blob, and instance separation already
  established by ADRs 0014, 0026, and 0027.

The one item with a genuine deadline is the `source_language` field in the
compiled `Program` (Phase 5). Adding it before bytecode assets are cached in
production is nearly free; adding it afterward means an ABI bump and a cache
invalidation.

## Ordering and gates

Unlike the 1.0 phases, which are parallel workstreams, the first three phases
here are a **real dependency chain**, and deliberately so. The grid arrival path
and region extensions come first, a cheap throwaway browser client validates them
second, and only then is the C++ core worth building. This ordering exists to
avoid the failure mode of writing a large client against an unproven protocol
surface.

Within Phase 1 the arrival path leads. A client that cannot authenticate, cannot
learn a region endpoint, and cannot be told what a region can do has nothing to
connect a transport to.

Phases 5 and 6 are independent of the client work and can proceed in parallel
with it.

## Decisions recorded

- [x] Choose C++ with an engine-neutral core and adopt Godot 4 via GDExtension as
  the first frontend — capable, permissively licensed, and compatible with a
  browser target, where Unreal is more license-encumbered than alternatives like
  Godot and has no credible web export (ADR 0030).
- [x] Adopt SLua compatibility as the baseline for Lua scripting, superseding an
  earlier plan for a Homeworldz-specific subset, and record the gates that decide
  whether the SLua implementation is reused or its semantics reimplemented
  (ADR 0031).
- [x] Define the additive, negotiated region extension model and the modern
  asset formats it serves (ADR 0032).

## Phase 1: Region Extension Foundation

Region and grid work. No new client exists yet, and Firestorm behavior must not
change.

The grid work is not incidental. [ADR 0032](adr/0032-region-extensions-for-new-client.md)
places the client's arrival on the grid's public user tier rather than behind
extension negotiation, so the client reaches the modern surface through `/v1`
before a region transport is ever attempted. That milestone comes first here
because it gates every other one: without it the client cannot authenticate,
cannot learn a region endpoint, and cannot be told what a region can do.

### Client arrival on the grid user tier

The path the first-party client takes to reach a region, per ADR 0032's "How the
first-party client arrives" and the server consequences in
[client ADR 0004](https://github.com/homeworldz/client/blob/main/docs/adr/0004-client-transport-and-push-channels.md).
None of this is negotiated: the client requires the modern path and treats its
absence as an incompatible grid.

- [ ] Serve an unauthenticated compatibility document reporting the grid's
  version and capabilities, so a client can satisfy its declared minimum protocol
  version before attempting a transport. Checked first deliberately: an absent
  endpoint answers immediately, while a QUIC attempt against a region that
  ignores it may hang until timeout.
- [ ] Add user-scoped `/v1/client/*` world-entry routes deriving the acting user
  **from the bearer token and never from the path**. The internal tier addresses
  users positionally (`/api/v1/inventory/{userId}`); mirroring that shape on a
  user-facing route would let any authenticated caller read another user's
  inventory and last known location.
- [ ] Mint short-lived, region-scoped tickets from a second signer with a
  distinct audience, so the account token — which reaches account management
  including password change — never reaches a region.
  [ADR 0028](adr/0028-untrusted-region-trust-model.md) admits regions outside the
  operator's control, which is what makes this structural rather than tidy.
- [ ] Return per-region capabilities as data in the session-open response, as a
  versioned field. Regions within one grid are not uniform, and a region crossing
  re-resolves them.
- [ ] Add the browser frontend's origin to the CORS allowlist.
- [ ] Keep `POST /v1/tokens` as the login endpoint; no new authentication
  surface is needed for this client.

### Extension negotiation

**Negotiation exists to protect viewers, and the first-party client does not
participate in it.** A browser has no fallback — it cannot open a raw UDP socket,
so a region that cannot serve the modern transport cannot serve the client at all
— and `SimulatorFeatures` and the seed reply are LLSD, which the client is
specifically built never to read. What negotiation buys is that a Firestorm-class
viewer never sees an extension it would not understand.

The registry of available extensions is **deliberately empty**: the mechanism
advertises capability, never intent, so an extension appears only as part of its
own implementation.

- [x] Advertise a Homeworldz extension feature map through the
  `SimulatorFeatures` capability, so a viewer opts into each extension and one
  that does not understand an extension never sees it. `SimulatorFeatures` carries
  a `HomeworldzExtensions` map beside the untouched `OpenSimExtras`; each
  advertised extension lists its own version and the capability names used to opt
  in. Opting in happens in the seed request, which the region previously ignored
  and now parses.
- [x] Establish named seed capabilities as the single mechanism for adding
  extensions, with no changes to baseline message semantics or wire formats.
  Negotiated capabilities are appended to the seed reply after the baseline set,
  each at its own region path. An extension is granted whole rather than
  piecemeal, since a partially negotiated extension has no defined meaning.
- [x] Version the extension map and define how an unknown or withdrawn extension
  degrades. The map carries its own version, distinct from any extension's. A
  requested name the region does not know is ignored rather than rejected, so a
  newer client gets less instead of failing; a withdrawn extension is absent from
  the advertisement and is not granted even to a client that still asks, whose
  stale capability URL then 404s.

Legacy non-regression is asserted on equality rather than by inspection: with no
extension negotiated, the seed reply is byte-identical to the pre-extension one,
and the baseline reply is a strict prefix of a negotiated one. Live Firestorm
acceptance is still outstanding and remains tracked below.

### Browser-reachable transport

Not a negotiated capability. The client arrives already knowing the modern path
is required and learns the region endpoint from the grid user tier above, so
there is no seed capability to ask for and no fallback to a legacy transport.

The client holds **two persistent channels rather than one**
([client ADR 0004](https://github.com/homeworldz/client/blob/main/docs/adr/0004-client-transport-and-push-channels.md)):
a low-rate grid channel for account and social traffic that must survive a region
crossing, and a per-region session for scene traffic. A single region-anchored
channel would die on every crossing; a single grid-anchored one would force every
scene update through an extra hop.

- [ ] Implement the region session transport over WebTransport/QUIC, carrying the
  event stream and object updates, with a WebSocket fallback. The fallback is for
  networks that block QUIC, **not** a degraded legacy mode — conflating the two
  would report a corporate firewall as an incompatible grid. New C++ listener
  work: nothing region-side speaks WebSocket or QUIC today.
- [ ] Implement the grid channel as a WebSocket on the existing user-tier
  service, carrying instant messages, presence, and inventory offers — the
  traffic that must reach a user regardless of which region they occupy, and
  therefore cannot live in one.
- [ ] Keep LLUDP fully authoritative for legacy viewers; the new transport is an
  added path and never a migration.
- [ ] Replace, for clients on the modern path, the three transports a legacy
  session uses (LLUDP, capability HTTP, long-poll event queue). Long-polling is
  dropped outright rather than carried forward: push is what these channels are
  for.
- [ ] Establish request/response over the realtime channel with correlation
  identifiers, so REST is used only to get started rather than alongside the
  session.
- [ ] Define reliable and unreliable stream usage, interest-managed update
  batching, and per-session flow control.

### Modern asset formats at rest

- [ ] Normalize texture uploads to KTX2/Basis as the authoritative blob, and
  generate JPEG2000 as a derived, cache-tier down-conversion on legacy request.
- [ ] Normalize mesh uploads to glTF 2.0 as the authoritative blob, and generate
  the Second Life mesh serialization as a derived down-conversion.
- [ ] Treat every down-conversion as regenerable derived data — cache, never
  authoritative, and vault-exempt — following the precedent baked textures set
  in ADR 0029.
- [ ] Generate server-side LOD chains for glTF meshes rather than trusting
  uploader-supplied levels.

### Server-side geometry

- [ ] Provide a capability that serves meshes for all geometry including prims,
  so a client need never implement prim tessellation.
- [ ] Keep serving prim parameters unchanged to legacy viewers, which continue
  tessellating client-side.
- [ ] Reconcile server-meshed geometry with the portable collision
  representations in [ADR 0023](adr/0023-portable-mesh-collision-representations.md).

### Legacy non-regression

- [ ] Establish an automated Firestorm acceptance run that proves login, terrain,
  objects, appearance, inventory, and crossings are unchanged with every
  extension enabled.
- [ ] Measure and bound the added cost of legacy down-conversion so a
  Firestorm-only region pays close to nothing for extensions it never serves.

## Phase 2: Browser Proof of Concept

A deliberately disposable client whose only purpose is to prove the Phase 1
surface before committing to a C++ core.

### Throwaway client

- [ ] Authenticate through the grid user tier, resolve a region endpoint, and
  hold a WebTransport region session — exercising the arrival path end to end
  rather than a negotiated capability.
- [ ] Render terrain, static objects, and KTX2 textures with an existing
  high-level web renderer, with no shared code intended to survive.
- [ ] Move an avatar under server authority and observe another participant
  moving.

### Acceptance gate

- [ ] Demonstrate a browser join from a cold cache within a few seconds on a
  residential connection.
- [ ] Record which extensions proved insufficient or wrongly shaped, and revise
  Phase 1 before any core work begins.
- [ ] Decide explicitly whether the web renderer is sufficient on its own; a
  capable enough result narrows or removes later phases rather than adding to
  them.

## Phase 3: Engine-Neutral Client Core

The durable C++ library both frontends consume. Per ADR 0030 it exposes no
engine or renderer types.

### Core boundary

- [ ] Define the core's public API with no engine, windowing, or renderer types,
  enforced by an automated check rather than convention.
- [ ] Build the core for native targets and for WASM via Emscripten from one
  source tree.
- [ ] Establish the dependency set — SDL3, fastgltf or cgltf, libktx or
  basis_universal, meshoptimizer, ozz-animation, Jolt — and verify each builds
  for both targets.

### Protocol and replication

- [ ] Implement the client half of both channels: the grid channel for account
  and social traffic, and the region session for the event stream and object
  updates.
- [ ] Implement interest management, snapshot-plus-delta replication, and
  server-authoritative reconciliation.
- [ ] Handle region crossings and teleports as first-class client state
  transitions rather than reconnections.

### Assets and streaming

- [ ] Implement a streaming asset cache with a disk backend natively and an OPFS
  backend in the browser.
- [ ] Load glTF meshes and KTX2 textures with GPU-ready transcode and no
  blocking decode on the frame thread.
- [ ] Enforce a memory budget disciplined enough for the browser's WASM ceiling,
  with eviction driven by interest management.

### Avatars and prediction

- [ ] Implement the avatar system on glTF skeletons and animation, driven by
  ozz-animation.
- [ ] Implement client-side prediction and a character controller on Jolt, the
  same engine the region uses per
  [ADR 0015](adr/0015-physics-world-boundary.md).
- [ ] Render attachments, sitting, and vehicle states consistently with region
  authority.

## Phase 4: Client Frontends

Two peers over one core, not a primary and a port.

### Godot desktop frontend

- [ ] Bind the core as a GDExtension through `godot-cpp`, keeping the C ABI
  boundary so stock Godot binaries load it with no compiler lock-step.
- [ ] Implement scene presentation, input, and camera control in Godot over core
  state.
- [ ] Build the user interface, with Dear ImGui reserved for development tooling
  and a user-facing toolkit for shipped UI.
- [ ] Ship installable desktop builds for Windows, macOS, and Linux.

### Browser frontend

- [ ] Implement the renderer against WebGPU as the only graphics API, running on
  Dawn or wgpu-native natively and on the browser's implementation on the web.
- [ ] Deliver zero-install join from a URL, including direct links to a region
  and landing point.
- [ ] Support guest and unauthenticated preview access where the grid permits it.
- [ ] Establish a mobile-browser baseline for viewport, input, and memory.

### Voice and presence

- [ ] Implement WebRTC voice, shared by both frontends.
- [ ] Render nearby-avatar presence, chat, and voice indicators.

## Phase 5: Lua Scripting Runtime

Independent of the client work. Follows [ADR 0031](adr/0031-lua-scripting-subset.md)
and sits behind the ADR 0021 boundary as a peer of Falcon.

**SLua compatibility is the baseline**: Homeworldz targets everything SLua
supports, with no subtractions. **Reusing the SLua implementation** — Linden
Lab's MIT-licensed fork of Luau, which already provides execution-state
serialization (Ares), preemptive scheduling hooks, per-script memory limits, and
sandboxing — is the expected means, and is what the evaluation milestone below
tests. A failed gate changes how the baseline is delivered, not whether: the
fallback in ADR 0031 is a purpose-built runtime that still implements the full
SLua surface.

### SLua evaluation and gates

Source review on 2026-07-25, recorded in ADR 0031, found the decisive gates
pass: C call frames and their continuations serialize, Luau's CodeGen need not be
disabled, and preemption is an injected valueless yield. What remains is
empirical confirmation.

- [ ] Build SLua and embed it behind the ADR 0021 boundary, driven by a bounded
  per-tick instruction slice.
- [ ] Confirm empirically that a thread yielded **inside a Homeworldz host call**
  survives an Ares round trip — the decisive gate, so far passing on source
  review alone.
- [ ] Measure CodeGen's effect on snapshot size, restore time, and region tick
  cost, now that disabling it appears unnecessary.
- [ ] Layer owner, object, and parcel aggregates and wall-clock guards on top of
  SLua's per-script logical memory limits.
- [ ] Record the implementation outcome — reuse or purpose-built — in ADR 0031.
  The compatibility baseline is settled either way.

### Integration behind the script runtime boundary

- [ ] Add a `source_language` field to the compiled `Program` container **before**
  bytecode caching hardens in production.
- [ ] Vendor and pin SLua into the C++ region build, isolating the version so an
  upstream API change cannot break a running grid.
- [ ] Share the scheduler, instruction fuel, host-function registry, and resource
  accounting with Falcon rather than duplicating them.
- [ ] Implement the `ll` host surface against the authoritative Homeworldz scene,
  with the same bounded-work and no-ambient-capability rules Falcon's hosts obey.
- [ ] Represent slow host operations as serializable continuations, delivering
  completion as a script event on the region thread.
- [ ] Register every `ll` host function as a C closure in the Ares permanents
  table, and every suspendable one with a registered continuation, so a script
  yielded inside a host call can still cross a border.
- [ ] Adopt SLua's base-image diffing for crossing snapshots so only the delta
  travels, and keep host-side state in a separate payload outside the VM.
- [ ] Resolve module `require` against inventory contents, and extend bytecode
  cache keys to cover the resolved dependency closure — ADR 0021's source-hash
  model assumes a script has no dependencies.
- [ ] Report Lua compile diagnostics through the same viewer capability path
  Falcon already uses, with line and column locations.

### SLua source compatibility

- [ ] Expose Linden functions under the `ll` namespace in PascalCase (`ll.Say`,
  `ll.GetPos`), matching SLua rather than LSL's `llSay` spelling.
- [ ] Support `LLEvents:on` event registration, `LLTimers`, `lljson`, and
  `bit32`.
- [ ] Support Luau gradual typing and type annotations.
- [ ] Apply SLua's 128 KB logical memory limit rather than LSL's 64 KB.
- [ ] Support metatables and metatable-based OOP, reversing the earlier exclusion
  now that upstream serialization covers the resulting object graphs.
- [ ] Adopt Second Life's protocol-level language selection when a compatible
  viewer path exists; use a first-line `--!lua` pragma in the interim, since
  Firestorm cannot yet compile SLua.
- [ ] Track SLua as it evolves out of open beta, treating the moving baseline as
  ongoing work rather than a one-time port.
- [ ] Document any Homeworldz addition as a namespaced extension — the surface
  may be added to, never subtracted from.

### Crossing parity

- [ ] Produce and restore crossing snapshots including cyclic table graphs,
  closures, upvalues, metatables, and live coroutines.
- [ ] Demonstrate a Lua-scripted attachment and a Lua-scripted vehicle crossing a
  region border mid-execution, matching the LSL acceptance tests.
- [ ] Benchmark snapshot size and restore time against equivalent LSL scripts.
- [ ] Establish resource-exhaustion and hostile-script tests against the adopted
  VM, not only the language surface.

## Phase 6: Creator Tooling

### Visual scripting

- [ ] Build a Blockly-style node editor that emits Lua or p-code, so non-coder
  authoring reuses the scripting runtime rather than an engine's graph tools.
- [ ] Run the editor in the browser frontend and in the desktop frontend from one
  implementation.
- [ ] Round-trip generated scripts as ordinary inventory script assets.

### Modern content pipeline

- [ ] Support direct glTF upload with a documented Blender round-trip, replacing
  the Collada path for creators using the new client.
- [ ] Support PBR materials end to end, from upload through region storage to
  both frontends.
- [ ] Provide in-world creation and editing tools against glTF geometry rather
  than prim primitives.

## Publishing note

[`ROADMAP.md`](ROADMAP.md) is mirrored to the public website by `syncweb.mjs`,
which the website renders from a single hard-coded import. Publishing this
document would additionally require a `syncweb.mjs` entry and a route on the
website side; until then it is repository-only, and running `node syncweb.mjs`
does not pick it up.
