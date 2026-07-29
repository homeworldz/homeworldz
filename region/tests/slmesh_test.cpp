#include "homeworldz/slmesh.h"

#include <cmath>
#include <cstdlib>

namespace {

bool near(float a, float b, float tolerance) { return std::fabs(a - b) <= tolerance; }

homeworldz::slmesh::Submesh quad(float size, bool carry_extras) {
    homeworldz::slmesh::Submesh submesh;
    submesh.positions = {{0, 0, 0}, {size, 0, 0}, {size, size, 0.5f * size}, {0, size, 0}};
    if (carry_extras) {
        submesh.normals = {{0, 0, 1}, {0, 0, 1}, {0.707f, 0, 0.707f}, {0, 0, 1}};
        submesh.texcoords = {{0, 0}, {2, 0}, {2, 2}, {0, 2}};
    }
    submesh.indices = {0, 1, 2, 0, 2, 3};
    return submesh;
}

} // namespace

int main() {
    using namespace homeworldz::slmesh;

    // Two material faces at high detail, one at the lower levels — face
    // counts may differ between levels, order must not.
    Mesh mesh;
    mesh.high = {quad(2.0f, true), quad(1.0f, false)};
    mesh.medium = {quad(2.0f, false), quad(1.0f, false)};
    mesh.low = {quad(2.0f, false), quad(1.0f, false)};
    mesh.lowest = {quad(2.0f, false), quad(1.0f, false)};
    mesh.physics_hull = {{0, 0, 0}, {2, 0, 0}, {2, 2, 1}, {0, 2, 0},
                         {0, 0, 1}, {2, 0, 1}, {2, 2, 0}, {0, 2, 1}};

    const auto bytes = serialize(mesh);
    if (bytes.empty()) return 1;
    const auto parsed = parse(bytes);
    if (!parsed) return 2;
    if (parsed->high.size() != 2 || parsed->medium.size() != 2 ||
        parsed->physics_hull.size() != 8)
        return 3;

    // Geometry survives to quantization precision (the domain spans 2 m, so
    // one step is 2/65535 ≈ 0.00003; allow a generous margin).
    const auto& face = parsed->high.front();
    if (face.positions.size() != 4 || face.indices != mesh.high.front().indices) return 4;
    for (std::size_t vertex = 0; vertex < 4; ++vertex)
        for (int axis = 0; axis < 3; ++axis)
            if (!near(face.positions[vertex][axis], mesh.high.front().positions[vertex][axis],
                      0.001f))
                return 5;
    if (face.normals.size() != 4 || !near(face.normals[2][0], 0.707f, 0.001f)) return 6;
    if (face.texcoords.size() != 4 || !near(face.texcoords[2][0], 2.0f, 0.001f)) return 7;
    for (std::size_t vertex = 0; vertex < 8; ++vertex)
        for (int axis = 0; axis < 3; ++axis)
            if (!near(parsed->physics_hull[vertex][axis], mesh.physics_hull[vertex][axis], 0.001f))
                return 8;

    // The second face carries no normals or texcoords, and stays that way.
    if (!parsed->high[1].normals.empty() || !parsed->high[1].texcoords.empty()) return 9;

    // Invalid meshes serialize to nothing rather than to broken bytes: a
    // missing level, an empty hull, an out-of-range index.
    Mesh missing_level = mesh;
    missing_level.low.clear();
    if (!serialize(missing_level).empty()) return 10;
    Mesh no_hull = mesh;
    no_hull.physics_hull.clear();
    if (!serialize(no_hull).empty()) return 11;
    Mesh bad_index = mesh;
    bad_index.high.front().indices[0] = 9;
    if (!serialize(bad_index).empty()) return 12;

    // Garbage does not parse.
    const char* noise = "certainly not a mesh asset";
    if (parse(std::span(reinterpret_cast<const std::byte*>(noise), 26))) return 13;
    return 0;
}
