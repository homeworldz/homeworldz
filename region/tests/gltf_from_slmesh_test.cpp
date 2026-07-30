// The `gltf` rendition, checked geometry against geometry rather than
// metadata against expectation: build a known type-49 asset, derive a GLB,
// parse the GLB back with the same reader the upload gate uses, and compare
// counts and bounds to the source.
//
// Counts are asserted non-zero as well as equal, because a converter that
// emits an empty mesh and a comparison that reads zero-equals-zero is a pass
// about nothing (client core's observation from its own empty-render trap,
// 2026-07-30).

#include "homeworldz/mesh_convert.h"
#include "homeworldz/slmesh.h"

#include <cmath>
#include <cstdio>

int main() {
    // Two faces so the per-submesh primitive split is exercised: a quad and a
    // triangle, with normals and texcoords on the first and neither on the
    // second (a submesh may legitimately carry only positions).
    homeworldz::slmesh::Submesh quad;
    quad.positions = {{-0.5f, -0.5f, 0.0f}, {0.5f, -0.5f, 0.0f},
                      {0.5f, 0.5f, 0.25f}, {-0.5f, 0.5f, 0.25f}};
    quad.normals = {{0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f},
                    {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}};
    quad.texcoords = {{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}};
    quad.indices = {0, 1, 2, 0, 2, 3};
    homeworldz::slmesh::Submesh triangle;
    triangle.positions = {{-0.25f, 0.0f, -0.5f}, {0.25f, 0.0f, -0.5f},
                          {0.0f, 0.4f, -0.5f}};
    triangle.indices = {0, 1, 2};

    homeworldz::slmesh::Mesh source;
    source.high = {quad, triangle};
    source.medium = source.high;
    source.low = source.high;
    source.lowest = source.high;
    source.physics_hull = {{-0.5f, -0.5f, -0.5f}, {0.5f, -0.5f, -0.5f},
                           {-0.5f, 0.5f, -0.5f}, {0.5f, 0.5f, -0.5f},
                           {-0.5f, -0.5f, 0.5f}, {0.5f, -0.5f, 0.5f},
                           {-0.5f, 0.5f, 0.5f}, {0.5f, 0.5f, 0.5f}};
    const auto stored = homeworldz::slmesh::serialize(source);
    if (stored.empty()) return 1;

    // What the source actually holds, read back through the parser so the
    // comparison uses quantized values on both sides.
    const auto reference = homeworldz::slmesh::parse(stored);
    if (!reference) return 2;
    std::size_t reference_vertices = 0, reference_triangles = 0;
    float low[3]{1e9f, 1e9f, 1e9f}, high[3]{-1e9f, -1e9f, -1e9f};
    for (const auto& submesh : reference->high) {
        reference_vertices += submesh.positions.size();
        reference_triangles += submesh.indices.size() / 3;
        for (const auto& position : submesh.positions)
            for (int axis = 0; axis < 3; ++axis) {
                low[axis] = (std::min)(low[axis], position[axis]);
                high[axis] = (std::max)(high[axis], position[axis]);
            }
    }

    const auto derived = homeworldz::mesh::gltf_from_sl_mesh(stored);
    if (!derived.ok) {
        std::printf("conversion failed: %s\n", derived.error.c_str());
        return 3;
    }
    // Non-zero first: an empty conversion must fail here, not match an empty
    // expectation further down.
    if (reference_vertices == 0 || reference_triangles == 0) return 4;
    if (derived.primitives != 2) return 5;
    if (derived.vertices != reference_vertices) return 6;
    if (derived.triangles != reference_triangles) return 7;

    // The bytes are a real GLB, and the same bounds reader the upload gate
    // uses agrees with the source's extent — geometry compared to geometry,
    // through an independent path rather than the converter's own counters.
    const auto bounds = homeworldz::mesh::declared_world_bounds(derived.glb);
    if (!bounds.ok) return 8;
    for (int axis = 0; axis < 3; ++axis) {
        const auto expected_extent = (std::max)(high[axis] - low[axis], 0.001f);
        const auto expected_center = (low[axis] + high[axis]) * 0.5f;
        if (std::fabs(bounds.extent[axis] - expected_extent) > 0.01f) return 9;
        if (std::fabs(bounds.center[axis] - expected_center) > 0.01f) return 10;
    }

    // And the round trip closes: the derived GLB converts back to a type-49
    // whose geometry still matches, which is the property that makes the two
    // directions safe to run in sequence.
    const auto round_trip = homeworldz::mesh::convert_glb(derived.glb);
    if (!round_trip.ok) {
        std::printf("round trip failed: %s\n", round_trip.error.c_str());
        return 11;
    }
    if (round_trip.high_triangles != reference_triangles) return 12;

    // A flat fixture, because the round trip above cannot fail on axes: if
    // both directions rotated by nothing, or by any pair of inverses, the
    // comparison still passes. Only an asset with an unmistakable thin axis
    // answers "did the emitted glTF actually get rotated" — a cube or a
    // unit-box mesh agrees with every orientation equally (client core, whose
    // flat plates are the only assets on the live grid able to witness a
    // regeneration, 2026-07-30).
    //
    // declared_world_bounds maps glTF axes back to region axes on the way in,
    // so a source flat in region Z must come back flat in region Z. If emit
    // forgot to rotate, the flat axis would land in region Y instead.
    {
        homeworldz::slmesh::Submesh plate;
        plate.positions = {{-0.5f, -0.5f, 0.0f}, {0.5f, -0.5f, 0.0f},
                           {0.5f, 0.5f, 0.0f}, {-0.5f, 0.5f, 0.0f}};
        plate.indices = {0, 1, 2, 0, 2, 3};
        homeworldz::slmesh::Mesh flat;
        flat.high = {plate};
        flat.medium = flat.high;
        flat.low = flat.high;
        flat.lowest = flat.high;
        flat.physics_hull = source.physics_hull;
        const auto flat_stored = homeworldz::slmesh::serialize(flat);
        if (flat_stored.empty()) return 14;
        const auto flat_derived = homeworldz::mesh::gltf_from_sl_mesh(flat_stored);
        if (!flat_derived.ok) return 15;
        const auto flat_bounds = homeworldz::mesh::declared_world_bounds(flat_derived.glb);
        if (!flat_bounds.ok) return 16;
        if (flat_bounds.extent[2] > 0.01f) return 17;         // thin axis stayed region Z
        if (flat_bounds.extent[0] < 0.9f) return 18;          // and the wide axes are wide
        if (flat_bounds.extent[1] < 0.9f) return 19;
    }

    // Refusals carry a reason rather than empty bytes.
    const std::vector<std::byte> garbage{std::byte{'g'}, std::byte{'l'},
                                         std::byte{'T'}, std::byte{'F'}};
    const auto refused = homeworldz::mesh::gltf_from_sl_mesh(garbage);
    if (refused.ok || refused.error.empty()) return 13;
    return 0;
}
