# HomeWorldz 2.0 Roadmap

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

Checkboxes describe the present state, not a promise of a release date. No
implementation work in this document has started; the only completed items are
the decisions themselves.

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
here are a **real dependency chain**, and deliberately so. The region extensions
come first, a cheap throwaway browser client validates them second, and only
then is the C++ core worth building. This ordering exists to avoid the failure
mode of writing a large client against an unproven protocol surface.

Phases 5 and 6 are independent of the client work and can proceed in parallel
with it.

## Decisions recorded

- [x] Choose C++ with an engine-neutral core, reject Unreal on license grounds
  for an MIT project, and adopt Godot 4 via GDExtension as the first frontend
  (ADR 0030).
- [x] Adopt SLua compatibility as the baseline for Lua scripting, superseding an
  earlier plan for a HomeWorldz-specific subset, and record the gates that decide
  whether the SLua implementation is reused or its semantics reimplemented
  (ADR 0031).
- [x] Define the additive, negotiated region extension model and the modern
  asset formats it serves (ADR 0032).

## Phase 1: Region Extension Foundation

Region-side work only. No new client exists yet, and Firestorm behavior must not
change.

### Extension negotiation

- [ ] Advertise a HomeWorldz extension feature map through the
  `SimulatorFeatures` capability, so a client opts into each extension and a
  client that does not understand one never sees it.
- [ ] Establish named seed capabilities as the single mechanism for adding
  extensions, with no changes to baseline message semantics or wire formats.
- [ ] Version the extension map and define how an unknown or withdrawn extension
  degrades.

### Browser-reachable transport

- [ ] Implement a WebTransport/QUIC session capability carrying login, the event
  stream, and object updates, with a WebSocket fallback for environments that
  block QUIC.
- [ ] Keep LLUDP fully authoritative for legacy viewers; the new transport is an
  added path and never a migration.
- [ ] Collapse the three legacy transports (LLUDP, capability HTTP, long-poll
  event queue) into the single multiplexed stream for clients that take it.
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

- [ ] Connect from a browser over the WebTransport session capability,
  authenticate, and hold a session.
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

- [ ] Implement the client half of the WebTransport session: login, event
  stream, and object updates.
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

**SLua compatibility is the baseline**: HomeWorldz targets everything SLua
supports, with no subtractions. **Reusing the SLua implementation** — Linden
Lab's MIT-licensed fork of Luau, which already provides execution-state
serialization (Ares), preemptive scheduling hooks, per-script memory limits, and
sandboxing — is the expected means, and is what the evaluation milestone below
tests. A failed gate changes how the baseline is delivered, not whether: the
fallback in ADR 0031 is a purpose-built runtime that still implements the full
SLua surface.

### SLua evaluation and gates

- [ ] Determine whether Ares serialization reaches the granularity crossings
  require — suspension mid-handler, including while a host function is on the
  stack — given that Lua cannot normally yield across a C-call boundary. This is
  the decisive gate.
- [ ] Determine whether Luau's native CodeGen must be disabled for serialization
  to hold, and measure what that costs.
- [ ] Confirm per-script memory limits can extend to the owner, object, and
  parcel aggregates and wall-clock guards SCRIPTING.md requires.
- [ ] Confirm HomeWorldz's asynchronous host-operation tokens survive an Ares
  round trip.
- [ ] Prototype SLua embedded in the region behind the ADR 0021 boundary, driven
  by a bounded per-tick instruction slice.
- [ ] Record the implementation outcome — reuse or purpose-built — in ADR 0031,
  resolving its verification gates one way or the other. The compatibility
  baseline is settled either way.

### Integration behind the script runtime boundary

- [ ] Add a `source_language` field to the compiled `Program` container **before**
  bytecode caching hardens in production.
- [ ] Vendor and pin SLua into the C++ region build, isolating the version so an
  upstream API change cannot break a running grid.
- [ ] Share the scheduler, instruction fuel, host-function registry, and resource
  accounting with Falcon rather than duplicating them.
- [ ] Implement the `ll` host surface against the authoritative HomeWorldz scene,
  with the same bounded-work and no-ambient-capability rules Falcon's hosts obey.
- [ ] Represent slow host operations as serializable continuations, delivering
  completion as a script event on the region thread.
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
- [ ] Document any HomeWorldz addition as a namespaced extension — the surface
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
