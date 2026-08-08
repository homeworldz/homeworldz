#include "homeworldz/slmesh.h"

#include <cmath>
#include <cstdlib>
#include <string_view>

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

    // A rigged mesh carries a skin block and per-vertex weights. The reader
    // here does not parse skin yet, so this asserts what the writer emits:
    // the block is named in the header, the joint table is in it, and the
    // geometry blocks are unchanged by its presence.
    Mesh rigged = mesh;
    Skin skin;
    skin.joints = {"mPelvis", "mTorso"};
    skin.inverse_bind = {
        {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1},
        {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, -1, 1}};
    rigged.skin = skin;
    for (auto& submesh : rigged.high) {
        submesh.influences.assign(submesh.positions.size(), {});
        for (auto& list : submesh.influences) {
            list.push_back({0, 0.75f});
            list.push_back({1, 0.25f});
        }
    }
    const auto rigged_bytes = serialize(rigged);
    if (rigged_bytes.empty()) return 30;
    const std::string_view blob(reinterpret_cast<const char*>(rigged_bytes.data()),
                                rigged_bytes.size());
    // Only the block *name* is searchable here: the header is uncompressed
    // LLSD, while the block it points at is deflated. An earlier version of
    // this test searched the same bytes for "joint_names" and "Weights" and
    // passed, which it could only ever have done by luck — on a small,
    // repetitive block zlib sometimes emits stored literals. The round trip
    // below is the real check, and it did not exist when this was written.
    if (blob.find("skin") == std::string_view::npos) return 31;
    // The geometry still parses with the extra block present.
    const auto rigged_parsed = parse(rigged_bytes);
    if (!rigged_parsed || rigged_parsed->high.size() != 2) return 33;

    // The skin now round-trips, which the earlier version of this test could
    // not check because the reader did not parse it. Asserting emitted bytes
    // was the honest thing to do then; it is the weaker thing to do now.
    if (!rigged_parsed->skin) return 34;
    const auto& back = *rigged_parsed->skin;
    if (back.joints.size() != 2 || back.joints[0] != "mPelvis" || back.joints[1] != "mTorso")
        return 35;
    if (back.inverse_bind.size() != 2) return 36;
    // The second matrix carried a distinguishing cell, so this checks the
    // matrices survive in order rather than merely in count — two identities
    // would round-trip indistinguishably whichever way they were shuffled.
    if (std::fabs(back.inverse_bind[1][14] - -1.0f) > 1e-6f) return 37;
    if (!back.alternate_inverse_bind.empty()) return 38;

    // The shape a real body actually has, which the two-joint fixture above
    // does not: a larger joint table, and weights on every level rather than
    // only the highest. The Second Life reference body converted and then
    // failed to parse back, and neither difference was covered.
    {
        Mesh many = mesh;
        Skin wide;
        for (int joint = 0; joint < 21; ++joint) {
            wide.joints.push_back("mSpine" + std::to_string(joint % 4 + 1));
            std::array<float, 16> matrix{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
            matrix[13] = static_cast<float>(joint);
            wide.inverse_bind.push_back(matrix);
        }
        many.skin = wide;
        for (auto* level : {&many.high, &many.medium, &many.low, &many.lowest})
            for (auto& submesh : *level) {
                submesh.influences.assign(submesh.positions.size(), {});
                for (std::size_t vertex = 0; vertex < submesh.influences.size(); ++vertex)
                    submesh.influences[vertex].push_back(
                        {static_cast<std::uint8_t>(vertex % 21), 1.0f});
            }
        const auto many_bytes = serialize(many);
        if (many_bytes.empty()) return 39;
        const auto many_parsed = parse(many_bytes);
        if (!many_parsed) return 40;
        if (!many_parsed->skin || many_parsed->skin->joints.size() != 21) return 41;
        if (many_parsed->high.size() != many.high.size()) return 42;
    }

    // A joint table and matrix list of different lengths describes nothing
    // coherent, so it is refused rather than written.
    Mesh mismatched = rigged;
    mismatched.skin->inverse_bind.pop_back();
    if (!serialize(mismatched).empty()) return 43;

    return 0;
}
