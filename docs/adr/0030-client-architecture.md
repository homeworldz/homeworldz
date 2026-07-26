# ADR 0030: Client Architecture and Engine Boundary

Status: Accepted

This ADR records **current expectation and intent**, not a commitment. No
implementation work has started, and the choices below are expected to be revised
as evidence arrives.

Homeworldz expects to build a first-party open-source client. This is **additive**:
ADR 0016's Firestorm/Second Life protocol compatibility target stands, legacy
viewers stay first-class, and the new client is a second front door to the same
regions rather than a replacement. The region-side extensions it relies on are
[ADR 0032](0032-region-extensions-for-new-client.md).

Zero-install browser access is a primary goal, not a later port. It shapes every
choice below.

## Decision

The client is **C++** with an engine-neutral domain core, and **Godot 4 via
GDExtension** as its first frontend.

Godot rather than Unreal, for two reasons in that order.

**Fit.** Zero-install browser access is a primary goal, and there is no credible
Unreal web export, so Unreal could serve at most one of the two targets. Godot
is capable, actively developed, and well matched to this workload, and
GDExtension's **C ABI** boundary means stock engine binaries load our code with
no compiler lock-step. Unreal is a highly capable engine and its rendering is
excellent; it is simply aimed at a different problem than a browser-reachable
client for streamed user content.

**Licensing simplicity.** Godot and `godot-cpp` are MIT, as Homeworldz is, so
there is nothing to weigh: no royalty terms, no per-contributor agreement, no
linking or copyleft entanglement. Unreal is more license-encumbered than
alternatives like Godot — source-available under a proprietary EULA rather than
an open-source license. That is a legitimate model many successful projects
accept, but it brings terms to evaluate and revisit (contributor access to
engine source, royalty scope against region hosting revenue, changes at
renewal) that simply do not arise with a permissively licensed engine. For an
open-source project expecting outside contributors, not having to weigh them at
all is worth more here than the capability difference.

## The layering rule

The anti-lock-in mechanism is the boundary, not the engine choice. The core owns
the protocol client, scene graph, interest management, asset cache and
streaming, avatar system, and client-side prediction, and exposes **no engine or
renderer types in its public API**. Godot is one consumer of that core; a
direct-WebGPU native frontend and a WASM/browser frontend are peers, not ports.

This is the same pattern as [ADR 0015](0015-physics-world-boundary.md) for
physics and [ADR 0021](0021-script-runtime-boundary.md) for the script runtime,
applied a third time.

## Own the domain, borrow the platform

A game engine's chief value is its **offline content pipeline** — editor,
importers, baking. This client has no offline content: everything arrives over
the wire at runtime, and the world itself is the editor. So the engine's greatest
strength is largely unusable here while its weakest area, streaming arbitrary
runtime content, is the central requirement. The core therefore depends on
focused libraries rather than an engine:

| Layer | Dependency |
| --- | --- |
| GPU | **WebGPU** via Dawn or wgpu-native |
| Window / input | SDL3 (zlib) |
| glTF | fastgltf or cgltf |
| Textures | libktx / basis_universal (KTX2, Basis) |
| Mesh / animation | meshoptimizer, ozz-animation |
| Client prediction | **Jolt** — the region's engine, per ADR 0015 |
| UI | Dear ImGui (development), RmlUi or DOM overlay (user) |
| Voice | WebRTC |
| Transport | WebTransport/QUIC via msquic or quiche |

All are permissively licensed and compile to WASM, so the same core serves both
frontends.

## Graphics abstraction

**WebGPU is the only graphics API the renderer targets.** Browsers provide it
natively; on desktop the identical code runs against Dawn or wgpu-native over
Vulkan, Metal, or D3D12. One renderer codebase covers both.

OpenGL is explicitly rejected: it is frozen at 4.1 on macOS, deprecated, and
offers no browser path (WebGL is a separate target with its own limits).

**Known rough edge.** GDExtension on Godot's own web export is finicky —
extensions must be compiled to WASM against a matching Emscripten version, with
separate threads and no-threads variants. The browser path is therefore a direct
WebGPU frontend over the shared core, not a Godot web export.

## Content formats

The client consumes **glTF 2.0** meshes and **KTX2/Basis** textures, and
implements only the modern path — notably it never implements prim
tessellation. The region-side commitment to serve those formats, and to
down-convert for legacy viewers, is ADR 0032.

## Visual scripting

Non-coder authoring is served by a **Blockly-style node editor that emits Lua or
p-code** (see [ADR 0031](0031-lua-scripting-subset.md)), not by engine-native
visual scripting. Unreal Blueprints cannot be authored at runtime in a shipped
client — they are an editor feature compiled ahead of packaging — and any
engine-native graph tool would tie user content to an engine, violating the
layering rule above. Emitting into the script runtime instead keeps authored
content portable across frontends and reuses the existing scheduler, resource
accounting, and crossing machinery.

## Relationship to other ADRs

- **ADR 0016** — the Firestorm/SL compatibility target stands; this client is
  additive.
- **ADR 0015 / 0021** — the same engine-neutral boundary pattern.
- **ADR 0031** — the scripting and visual-scripting frontend this client exposes.
- **ADR 0032** — the region extensions this client consumes; client and region
  concerns are deliberately split across the two.
