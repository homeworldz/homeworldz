// The Second Life mesh asset format (type 49): a binary-LLSD header naming
// zlib-compressed LOD and physics blocks. This is the `sl-mesh` rendition of
// ADR 0033 — derived from the canonical GLB by the conversion worker, served
// to viewers, never touched by the modern client.
//
// The module carries both the writer and a reader: the reader is what makes
// the writer testable as a round trip today, and it is the parser the M2
// direction (deriving gltf from a Firestorm-uploaded sl-mesh) needs anyway.
//
// Geometry constraints mirror the wire format: positions and texture
// coordinates quantize to 16-bit values over a published domain, normals over
// the fixed [-1, 1] domain, and indices are 16-bit — so a submesh holds at
// most 65,535 vertices, one submesh per material face.
#ifndef HOMEWORLDZ_SLMESH_H
#define HOMEWORLDZ_SLMESH_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace homeworldz::slmesh {

struct Submesh {
    std::vector<std::array<float, 3>> positions;
    // Empty means "not carried"; when present, sized like positions.
    std::vector<std::array<float, 3>> normals;
    std::vector<std::array<float, 2>> texcoords;
    // Triangle list, three indices per triangle, into this submesh's vertices.
    std::vector<std::uint16_t> indices;
};

// One level of detail: a submesh per material face, in material order. The
// order must be identical across levels — the viewer matches faces by index.
using Level = std::vector<Submesh>;

struct Mesh {
    // high is mandatory; the others fall back to the next-present level when
    // a serializer chooses to omit them. This converter always writes all
    // four.
    Level high;
    Level medium;
    Level low;
    Level lowest;
    // The convex physics hull's vertices (a single hull, the format's
    // "BoundingVerts" shape).
    std::vector<std::array<float, 3>> physics_hull;
};

// serialize renders the asset bytes: binary-LLSD header, then the compressed
// blocks the header's offsets name. Empty on invalid input (no high level, a
// submesh with no triangles, or an index out of range).
std::vector<std::byte> serialize(const Mesh& mesh);

// parse reads asset bytes back into the model, quantization applied — a
// round trip through serialize/parse reproduces geometry to quantization
// precision. nullopt when the bytes are not a well-formed mesh asset.
std::optional<Mesh> parse(std::span<const std::byte> content);

} // namespace homeworldz::slmesh

#endif
