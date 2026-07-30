// GLB → SL-mesh conversion (ADR 0033 M1): the derivation the meshsmith
// worker runs. The canonical GLB is never rewritten; this produces the
// type-49 rendition viewers fetch.
#ifndef HOMEWORLDZ_MESH_CONVERT_H
#define HOMEWORLDZ_MESH_CONVERT_H

#include <array>
#include <cstddef>
#include <span>
#include <string>
#include <vector>

namespace homeworldz::mesh {

struct Conversion {
    bool ok{};
    // Actionable when failed: the reason lands in the job record an operator
    // reads, and may be shown to the creator on a re-request.
    std::string error;
    std::vector<std::byte> sl_mesh;
    std::size_t faces{};
    std::size_t high_triangles{};
    std::size_t lowest_triangles{};
};

// convert_glb builds the sl-mesh rendition: one submesh per material face
// (world transforms applied, primitives sharing a material merged), a LOD
// chain generated with meshoptimizer where the source carries only one level,
// and a convex physics block from the geometry's bounding box — the
// conservative shape, until V-HACD decomposition lands (tracked in the ADR
// 0033 milestone). Assumes the acceptance gate already passed; failures here
// are conversion facts (an over-65,535-vertex face, an unreadable accessor),
// not policy.
Conversion convert_glb(std::span<const std::byte> glb);

// The world-space box the GLB's declared accessor bounds cover, under every
// node transform reachable from the scene — computed from accessor min/max
// corners without loading buffers, so the upload gate can afford it. This is
// the ONE bounds definition: the upload sets the wrapper prim's scale from
// it, and the converter normalizes geometry by it, so the prim renders at
// authored size by construction (viewers scale mesh geometry by prim scale
// over a unit domain).
struct WorldBounds {
    bool ok{};
    std::array<float, 3> center{};
    std::array<float, 3> extent{};
};
WorldBounds declared_world_bounds(std::span<const std::byte> glb);

// gltf_from_sl_mesh derives the `gltf` rendition from a stored type-49 asset:
// ADR 0033 M2's remaining half, which makes viewer-authored meshes readable by
// clients on the modern path — they never learn the legacy serialization, so
// without this every Firestorm-uploaded object is a placeholder to them.
//
// The high LOD becomes one glTF primitive per submesh (a material face).
// Geometry is emitted in the asset's own coordinates, which for a viewer-
// authored mesh is the normalized unit domain — so a renderer applies the
// object's scale over it, per ADR 0033 "Scale".
//
// COORDINATES: emitted in region axes (Z up), not glTF's Y-up convention.
// This matches the forward converter, which reads uploaded GLB coordinates as
// region coordinates without rotating them, so the two directions round-trip.
// It is a deviation from the glTF convention and is stated rather than
// silent; whether the pipeline should instead rotate on both sides is an open
// question recorded in ADR 0033.
struct GltfConversion {
    bool ok{};
    std::string error;
    std::vector<std::byte> glb;
    std::size_t primitives{};
    std::size_t vertices{};
    std::size_t triangles{};
};
GltfConversion gltf_from_sl_mesh(std::span<const std::byte> asset);

// The generator tag stored with renditions this converter produces, bumped
// when output changes so regeneration can find what it supersedes.
inline constexpr const char* generator = "meshsmith/0.5";

} // namespace homeworldz::mesh

#endif
