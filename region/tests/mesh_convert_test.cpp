#include "homeworldz/mesh_convert.h"
#include "homeworldz/slmesh.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {

// The same GLB builder the acceptance test uses: real container bytes.
std::vector<std::byte> glb(std::string json, const std::vector<std::uint8_t>& bin) {
    while (json.size() % 4 != 0) json.push_back(' ');
    std::vector<std::uint8_t> padded_bin = bin;
    while (!padded_bin.empty() && padded_bin.size() % 4 != 0) padded_bin.push_back(0);
    const auto append_u32 = [](std::vector<std::uint8_t>& out, std::uint32_t value) {
        out.push_back(static_cast<std::uint8_t>(value));
        out.push_back(static_cast<std::uint8_t>(value >> 8));
        out.push_back(static_cast<std::uint8_t>(value >> 16));
        out.push_back(static_cast<std::uint8_t>(value >> 24));
    };
    std::vector<std::uint8_t> out;
    out.insert(out.end(), {'g', 'l', 'T', 'F'});
    append_u32(out, 2);
    const std::uint32_t total = 12 + 8 + static_cast<std::uint32_t>(json.size()) +
        (padded_bin.empty() ? 0 : 8 + static_cast<std::uint32_t>(padded_bin.size()));
    append_u32(out, total);
    append_u32(out, static_cast<std::uint32_t>(json.size()));
    out.insert(out.end(), {'J', 'S', 'O', 'N'});
    out.insert(out.end(), json.begin(), json.end());
    if (!padded_bin.empty()) {
        append_u32(out, static_cast<std::uint32_t>(padded_bin.size()));
        out.insert(out.end(), {'B', 'I', 'N', 0});
        out.insert(out.end(), padded_bin.begin(), padded_bin.end());
    }
    std::vector<std::byte> bytes(out.size());
    std::memcpy(bytes.data(), out.data(), out.size());
    return bytes;
}

} // namespace

int main() {
    // A quad (two triangles, four vertices, indexed) under a node that
    // translates it by +10 on x: the conversion must apply the transform.
    const float positions[12] = {0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0};
    const std::uint16_t indices[6] = {0, 1, 2, 0, 2, 3};
    std::vector<std::uint8_t> bin(sizeof positions + sizeof indices);
    std::memcpy(bin.data(), positions, sizeof positions);
    std::memcpy(bin.data() + sizeof positions, indices, sizeof indices);
    const std::string json =
        R"({"asset":{"version":"2.0"},)"
        R"("buffers":[{"byteLength":60}],)"
        R"("bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":48},)"
        R"({"buffer":0,"byteOffset":48,"byteLength":12}],)"
        R"("accessors":[{"bufferView":0,"componentType":5126,"count":4,"type":"VEC3",)"
        R"("min":[0,0,0],"max":[1,1,0]},)"
        R"({"bufferView":1,"componentType":5123,"count":6,"type":"SCALAR"}],)"
        R"("meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1}]}],)"
        R"("nodes":[{"mesh":0,"translation":[10,0,0]}],)"
        R"("scenes":[{"nodes":[0]}],"scene":0})";

    const auto conversion = homeworldz::mesh::convert_glb(glb(json, bin));
    if (!conversion.ok) return 1;
    if (conversion.faces != 1 || conversion.high_triangles != 2) return 2;

    // The wrapper prim's scale comes from the declared world bounds: the quad
    // spans 1x1 translated to x 10..11, so center (10.5, 0.5, 0), extent
    // (1, 1, ~0).
    const auto bounds = homeworldz::mesh::declared_world_bounds(glb(json, bin));
    if (!bounds.ok || std::fabs(bounds.center[0] - 10.5f) > 0.001f ||
        std::fabs(bounds.extent[0] - 1.0f) > 0.001f ||
        std::fabs(bounds.extent[1] - 1.0f) > 0.001f)
        return 10;

    // The output is a well-formed type-49 asset whose geometry is normalized
    // by those same bounds — the unit domain the prim scale stretches back to
    // authored size. The far corner lands at (+0.5, +0.5).
    const auto parsed = homeworldz::slmesh::parse(conversion.sl_mesh);
    if (!parsed || parsed->high.size() != 1) return 3;
    const auto& face = parsed->high.front();
    if (face.positions.size() != 4 || face.indices.size() != 6) return 4;
    bool found_corner = false;
    for (const auto& position : face.positions) {
        if (std::fabs(position[0]) > 0.501f || std::fabs(position[1]) > 0.501f) return 5;
        if (std::fabs(position[0] - 0.5f) < 0.001f && std::fabs(position[1] - 0.5f) < 0.001f)
            found_corner = true;
    }
    if (!found_corner) return 5;

    // The source has no normals, so the converter computed them: a flat quad
    // in the XY plane gets straight-up normals.
    if (face.normals.size() != 4 || std::fabs(face.normals[0][2] - 1.0f) > 0.01f) return 11;
    // No texcoords in the source either, so the converter synthesized them,
    // and they vary per vertex — constant UVs are what NaN a viewer's
    // tangent math.
    if (face.texcoords.size() != 4) return 12;
    bool uv_varies = false;
    for (std::size_t vertex = 1; vertex < 4; ++vertex)
        if (std::fabs(face.texcoords[vertex][0] - face.texcoords[0][0]) > 0.01f ||
            std::fabs(face.texcoords[vertex][1] - face.texcoords[0][1]) > 0.01f)
            uv_varies = true;
    if (!uv_varies) return 13;

    // Every level is present and non-empty; the physics hull is the unit box.
    if (parsed->medium.empty() || parsed->low.empty() || parsed->lowest.empty()) return 6;
    if (parsed->physics_hull.size() != 8) return 7;
    float max_x = -1e9f;
    for (const auto& vertex : parsed->physics_hull) max_x = (std::max)(max_x, vertex[0]);
    if (std::fabs(max_x - 0.5f) > 0.001f) return 8;

    // Geometry-free input fails with a reason, never with bytes.
    const auto empty = homeworldz::mesh::convert_glb(glb(
        R"({"asset":{"version":"2.0"},"scenes":[{"nodes":[]}],"scene":0})", {}));
    if (empty.ok || empty.error.empty()) return 9;
    return 0;
}
