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

// The generator tag stored with renditions this converter produces, bumped
// when output changes so regeneration can find what it supersedes.
inline constexpr const char* generator = "meshsmith/0.2";

} // namespace homeworldz::mesh

#endif
