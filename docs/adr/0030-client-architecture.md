# ADR 0030: Client Architecture and Engine Boundary

Status: Accepted

HomeWorldz will build a first-party open-source client. This is **additive**:
ADR 0016's Firestorm/Second Life protocol compatibility target stands, legacy
viewers stay first-class, and the new client is a second front door to the same
regions rather than a replacement. The region-side extensions it relies on are
[ADR 0032](0032-region-extensions-for-new-client.md).

Zero-install browser access is a primary goal, not a later port. It shapes every
choice below.

## Decision

The client is **C++** with an engine-neutral domain core, and **Godot 4 via
GDExtension** as its first frontend.

Godot rather than Unreal, primarily on licensing. HomeWorldz is **MIT**. Unreal
is source-available under a proprietary EULA: engine source may be shared only
between Epic licensees, so every contributor would need to accept the EULA and
link an Epic account to build the client. The 5% royalty's application to
**region hosting revenue** is ambiguous and would require written clarification
from Epic, whose terms can change at renewal, and who competes directly in this
space. Godot and `godot-cpp` are MIT, and because a GDExtension is a separate
shared library across a **C ABI**, there is no linking or copyleft entanglement
and no compiler lock-step with the engine — stock Godot binaries load it.

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
importers, baking. A viewer has no offline content: everything arrives over the
wire at runtime, and the world itself is the editor. So the engine's greatest
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
