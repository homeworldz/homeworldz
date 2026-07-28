# ADR 0033: Mesh Pipeline — glTF Canonical, Derived Viewer Renditions

Status: Accepted

Homeworldz stores mesh content in **glTF 2.0** (as GLB, the single-file
binary container) as its canonical format, and serves legacy viewers the
**Second Life mesh format** (asset type 49: an LLSD header naming
zlib-compressed LOD and physics blocks) as a **derived rendition** generated
server-side. The Homeworldz client uploads GLB directly and renders it
directly; Firestorm keeps its native upload and fetch paths and never learns
it is not on a Second Life asset server. Neither client family is asked to
speak the other's format.

## Why glTF is the canonical form

glTF 2.0 is the interchange format the rest of the industry converged on:
every DCC tool exports it (Blender, Maya, 3ds Max, Daz Studio, Substance),
every engine imports it, PBR metallic-roughness materials are native to it
rather than bolted on, and its GLB container carries geometry, materials,
textures, skins, and animations in one verifiable blob. The Second Life mesh
format is none of these things — it is a viewer-internal serialization whose
only ecosystem is Second Life viewers — so it is the wrong thing to preserve
and the right thing to derive.

The fidelity gap between the two families is smaller than it once was:
Firestorm in the pinned compatibility range (ADR 0016) renders **glTF PBR
materials natively** (the viewer "GLTF Materials" project, material asset
type 57 carrying glTF material JSON). Geometry must still be served as SL
mesh, but the material model no longer has to be downgraded to
diffuse/normal/specular for viewers that speak PBR.

## Canonical blobs and renditions

A mesh asset's **canonical blob is the creator's original upload**, byte
for byte — a GLB from the Homeworldz client, or an SL-mesh payload from a
Firestorm upload. The pipeline never destroys the source it was given; every
other form is derived from it and can be regenerated.

**Renditions** are derived encodings of an asset recorded against it:

- `gltf` — the GLB. Identical to the canonical blob for modern-client
  uploads; derived (geometry, materials, rigging as far as it maps) for
  Firestorm uploads.
- `sl-mesh` — the type-49 payload viewers fetch. Identical to the canonical
  blob for Firestorm uploads; derived for GLB uploads, including generated
  LOD chain and physics blocks.
- `sl-material` — glTF material JSON (type 57) extracted per material, so
  PBR-capable viewers render the same surfaces the modern client does.
- `j2c-texture` — textures extracted from the GLB and converted to JPEG2000
  texture assets for the viewer pipeline (KTX2 at rest arrives with the
  Phase 10 formats work and becomes one more rendition kind).

This extends ADR 0027 without changing it: the asset still names exactly one
(canonical) blob; renditions live in their own table
(`asset_renditions(asset_id, kind, blob_id, generator, generated_at)`) whose
blobs are ordinary registry blobs. The generator column names the tool
version that produced the rendition, so a better simplifier or converter can
regenerate everything it superseded, idempotently.

**Renditions are derived data and exempt from vault durability**, exactly as
baked textures are (ADR 0026): the canonical blob is what the
inventory-commit invariant protects, and everything else is recomputable
from it. For the same reason, the canonical GLB **keeps its textures
embedded**: the asset's durable closure stays trivially complete (one blob),
and the extracted texture assets that viewers need are regenerable
renditions rather than durability obligations. The storage cost of embedded
duplicates is accepted; blob-layer deduplication (ADR 0027's byte-comparison
dedup) reclaims it later without a design change.

## Where conversion runs

A dedicated grid-side worker — C++, in this repo, sharing the region's build
stack — consumes a conversion queue the grid writes at upload time and
produces renditions into the registry. Grid-side because regions are
untrusted (ADR 0028) and must not define what other users' viewers render;
a worker process rather than in-band in the grid binary because mesh
processing is unbounded CPU that must not sit on the serving path. An asset
is usable to the modern client the moment its GLB exists — GLB renders
as-is — and becomes visible to viewers when its `sl-mesh` rendition lands;
the region answers a viewer's mesh fetch for an unconverted asset with
not-yet rather than never.

Dependencies, all vcpkg ports, chosen smallest-first: **cgltf** (GLB
parse/write), **meshoptimizer** (LOD simplification when the source has no
explicit LODs; sources that carry them — MSFT_lod or the `_LOD0..3` naming
convention — are honored instead), **v-hacd** (convex decomposition for the
physics blocks and the Jolt collision source), and the zlib/openjpeg already
in the tree. Draco-compressed GLBs are rejected in v1 rather than half
supported; the modern client decompresses at import if it ever accepts
Draco input.

Validation is the upload gate, with actionable refusals: size within the
vault blob cap, triangle budgets per LOD, texture dimension caps, material
count caps, and a glTF extension allowlist (the KHR material and texture
extensions; anything unknown is refused, not ignored, so content never
renders differently on the client that understands more).

**The acceptance policy is published, not mirrored** (client core pushback,
2026-07-28 — the movement-constants lesson applied before the mistake is
made). A client that validates at import time must refuse exactly what
upload would refuse, and two hand-maintained copies of one policy drift: a
grid that widens its allowlist has no way to tell a client that kept the old
copy, and the creator meets a refusal no error message explains. So the
whole gate — the extension allowlist, the Draco stance, the triangle,
texture, and material caps, and the rig limits (the Bento influence maximum
included, fixed as it is: published beats permanent) — is served in the
region capability manifest (ADR 0032), the same surface that already tells a
client what a region can do. The concrete field shape is defined with M1,
because the numbers are; the contract that they are read, never encoded, is
decided now. Import-time validation in the Homeworldz client reads the same
block, so a creator with an over-weighted FBX rig hears about it while the
source file is still in front of them, not after the upload.

## Upload paths

- **Homeworldz client → GLB.** An HTTP upload capability on the region (the
  established viewer-asset upload shape), validated, registered, vaulted at
  inventory commit like any asset, renditions queued.
- **Firestorm → SL mesh.** The standard viewer mesh-upload capability and
  fee flow, accepted as-is per ADR 0016. The upload is canonical; a `gltf`
  rendition is derived so the modern client renders viewer-authored content
  too.
- **Other formats — FBX, OBJ, DAE, STL — convert in the Homeworldz client at
  import, not on the server.** The client imports locally (assimp/ufbx
  class libraries), shows the creator what the conversion produced, and
  uploads the resulting GLB. This keeps the server pipeline narrow (two
  input formats, both verifiable), puts the fidelity feedback loop where the
  creator is, and adds formats without server deployments. A server-side
  import service for browser uploads on the management site can reuse the
  same entry later; it is explicitly optional.
- **Daz3D** content arrives through Daz Studio's own glTF or FBX export and
  the client import path above. Native DSON/DUF ingestion is out of scope —
  its rigging and morph semantics are a moving product surface, not a
  format — and Daz's interactive-license terms for exported content are the
  creator's obligation, surfaced in the import UI rather than policed by
  the pipeline.

## Rigging, in phases

Static mesh ships first and proves the pipeline. Rigged mesh follows, as a
mapping problem stated honestly: viewer-rendered rigged mesh must weight to
the Second Life Bento skeleton (its bone names, at most four influences per
vertex), so the converter maps glTF skins onto that skeleton and refuses
rigs that do not map rather than guessing. Morph targets and glTF animation
clips are deferred until the modern client's avatar work needs them; they
ride along in the canonical GLB untouched either way, because the canonical
blob is never rewritten.

## Milestones

- **M1 — static mesh, both directions.** GLB upload capability, validation,
  renditions table, worker with sl-mesh derivation (LOD generation, V-HACD
  physics), region mesh serving to viewers, mesh rez as scene content on the
  Jolt collision source. Firestorm sees a GLB-authored static object.
- **M2 — Firestorm uploads.** Viewer mesh-upload capability with the fee
  stub, `gltf` derivation from SL mesh.
- **M3 — materials and textures.** `sl-material` and `j2c-texture`
  renditions, so PBR surfaces match across client families.
- **M4 — rigged mesh.** Bento mapping for attachments and body wearables.
- **M5 — import breadth.** Client-side FBX/OBJ/DAE import (client repo),
  documented Daz export path, optional web import service.

The asset-format items in Phase 10 (KTX2 at rest, mesh prims server-side)
build on this pipeline rather than preceding it: prim meshing becomes one
more producer of `gltf` renditions, and KTX2 one more texture rendition
kind.
