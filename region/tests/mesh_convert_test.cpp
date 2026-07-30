#include "homeworldz/image.h"
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

    // The wrapper prim's scale comes from the declared world bounds, in region
    // axes: the source quad lies in glTF's XY plane (its thin axis is glTF Z),
    // and a region is Z-up, so in-world it is thin in Y and stands 1 metre
    // tall. Getting this wrong lays every upright model on its side, and a
    // square quad cannot show it - only the thin axis can (client core's
    // cube-proves-size / triangle-proves-orientation lesson, 2026-07-30).
    const auto bounds = homeworldz::mesh::declared_world_bounds(glb(json, bin));
    if (!bounds.ok || std::fabs(bounds.center[0] - 10.5f) > 0.001f ||
        std::fabs(bounds.extent[0] - 1.0f) > 0.001f ||
        std::fabs(bounds.extent[2] - 1.0f) > 0.001f)
        return 10;
    if (bounds.extent[1] > 0.01f) return 10;  // thin axis is region Y

    // The output is a well-formed type-49 asset whose geometry is normalized
    // by those same bounds — the unit domain the prim scale stretches back to
    // authored size. The far corner lands at (+0.5, +0.5) in the two axes the
    // quad spans, which after the axis map are region X and Z.
    const auto parsed = homeworldz::slmesh::parse(conversion.sl_mesh);
    if (!parsed || parsed->high.size() != 1) return 3;
    const auto& face = parsed->high.front();
    if (face.positions.size() != 4 || face.indices.size() != 6) return 4;
    bool found_corner = false;
    for (const auto& position : face.positions) {
        if (std::fabs(position[0]) > 0.501f || std::fabs(position[2]) > 0.501f) return 5;
        if (std::fabs(position[0] - 0.5f) < 0.001f && std::fabs(position[2] - 0.5f) < 0.001f)
            found_corner = true;
    }
    if (!found_corner) return 5;

    // The source has no normals, so the converter computed them: a quad lying
    // in glTF's XY plane faces glTF +Z, which in region axes is -Y.
    if (face.normals.size() != 4 || std::fabs(std::fabs(face.normals[0][1]) - 1.0f) > 0.01f)
        return 11;
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

    // Texture extraction (ADR 0033 M3), and the property that matters most
    // about it: face N means the same face to the converter and to the
    // TextureEntry built from this. Two materials, the first textured and the
    // second not, so a mismatch in ordering shows as the wrong face being
    // textured rather than as a count that happens to agree.
    {
        // A 1x1 PNG, hand-assembled: signature, IHDR, IDAT (a zlib stored
        // block), IEND. Real bytes so the extractor's mime and size checks run
        // against something a decoder would accept.
        // A valid 1x1 RGBA PNG (generated, CRCs and zlib stream real), so the
        // decode assertion below tests stb rather than tolerating a broken
        // fixture - the first version of this array parsed as a texture and
        // failed to decode, which is the fixture lying about being evidence.
        const std::vector<std::uint8_t> png{
            0x89,0x50,0x4e,0x47,0x0d,0x0a,0x1a,0x0a,0x00,0x00,0x00,0x0d,
            0x49,0x48,0x44,0x52,0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x01,
            0x08,0x06,0x00,0x00,0x00,0x1f,0x15,0xc4,0x89,0x00,0x00,0x00,
            0x0d,0x49,0x44,0x41,0x54,0x78,0x9c,0x63,0xf8,0xcf,0xc0,0xf0,
            0x1f,0x00,0x05,0x00,0x01,0xff,0x89,0x99,0x3d,0x1d,0x00,0x00,
            0x00,0x00,0x49,0x45,0x4e,0x44,0xae,0x42,0x60,0x82};
        const float positions[12] = {0,0,0, 1,0,0, 1,1,0, 0,1,0};
        const std::uint16_t indices[6] = {0,1,2, 0,2,3};
        std::vector<std::uint8_t> bin(sizeof positions + sizeof indices + png.size());
        std::memcpy(bin.data(), positions, sizeof positions);
        std::memcpy(bin.data() + sizeof positions, indices, sizeof indices);
        std::memcpy(bin.data() + sizeof positions + sizeof indices, png.data(), png.size());
        const auto image_offset = sizeof positions + sizeof indices;
        const std::string textured_json =
            std::string(R"({"asset":{"version":"2.0"},)") +
            R"("buffers":[{"byteLength":)" + std::to_string(bin.size()) + R"(}],)" +
            R"("bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":48},)" +
            R"({"buffer":0,"byteOffset":48,"byteLength":12},)" +
            R"({"buffer":0,"byteOffset":)" + std::to_string(image_offset) +
            R"(,"byteLength":)" + std::to_string(png.size()) + R"(}],)" +
            R"("accessors":[{"bufferView":0,"componentType":5126,"count":4,"type":"VEC3",)" +
            R"("min":[0,0,0],"max":[1,1,0]},)" +
            R"({"bufferView":1,"componentType":5123,"count":6,"type":"SCALAR"}],)" +
            R"("images":[{"bufferView":2,"mimeType":"image/png"}],)" +
            R"("samplers":[{}],"textures":[{"source":0,"sampler":0}],)" +
            R"("materials":[{"pbrMetallicRoughness":{"baseColorTexture":{"index":0}}},{}],)" +
            R"("meshes":[{"primitives":[)" +
            R"({"attributes":{"POSITION":0},"indices":1,"material":0},)" +
            R"({"attributes":{"POSITION":0},"indices":1,"material":1}]}],)" +
            R"("nodes":[{"mesh":0}],"scenes":[{"nodes":[0]}],"scene":0})";
        const auto textured = glb(textured_json, bin);
        const auto extraction = homeworldz::mesh::extract_textures(textured);
        if (!extraction.ok) return 20;
        if (extraction.textures.size() != 1) return 21;
        if (extraction.textures[0].mime != "image/png") return 22;
        if (extraction.textures[0].bytes.size() != png.size()) return 23;
        // Face 0 is the textured material, face 1 is the bare one.
        if (extraction.face_textures.size() != 2) return 24;
        if (extraction.face_textures[0] != 0 || extraction.face_textures[1] != -1) return 25;
        // And the converter agrees about how many faces there are, in that
        // order - the shared traversal is the whole point.
        const auto textured_conversion = homeworldz::mesh::convert_glb(textured);
        if (!textured_conversion.ok) return 26;
        if (textured_conversion.faces != extraction.face_textures.size()) return 27;
        // The embedded PNG decodes, so what the viewer pipeline will re-encode
        // as JPEG2000 is a real image rather than bytes that merely survived.
        const auto decoded = homeworldz::image::decode_png_or_jpeg(png);
        if (!decoded || decoded->width != 1 || decoded->height != 1) return 28;
    }
    return 0;
}
