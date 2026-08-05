#include "homeworldz/mesh_acceptance.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {

// Builds a real GLB container around the given glTF JSON and binary chunk —
// the validator must be exercised against actual container bytes, not stubs.
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

// One triangle: three float3 positions in the binary chunk.
const char* const triangle_json_head =
    R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":36}],)"
    R"("bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36}],)"
    R"("accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3",)"
    R"("min":[0,0,0],"max":[1,1,0]}],)"
    R"("meshes":[{"primitives":[{"attributes":{"POSITION":0}}]}],)"
    R"("nodes":[{"mesh":0}],"scenes":[{"nodes":[0]}],"scene":0)";

std::vector<std::uint8_t> triangle_bin() {
    const float positions[9] = {0, 0, 0, 1, 0, 0, 1, 1, 0};
    std::vector<std::uint8_t> bin(sizeof positions);
    std::memcpy(bin.data(), positions, sizeof positions);
    return bin;
}

} // namespace

int main() {
    using homeworldz::mesh::validate_glb;

    // A minimal valid GLB is accepted, with its triangle counted.
    const auto valid = glb(std::string(triangle_json_head) + "}", triangle_bin());
    const auto accepted = validate_glb(valid);
    if (!accepted.accepted || accepted.triangles != 1 || accepted.materials != 0)
        return 1;

    // Not a GLB at all, and truncated garbage, are refused without parsing.
    const std::string text = "not a mesh";
    if (validate_glb(std::span(reinterpret_cast<const std::byte*>(text.data()),
                               text.size())).accepted)
        return 2;

    // An unknown extension is refused, not ignored — used or required.
    const auto unknown_used = glb(std::string(triangle_json_head) +
        R"(,"extensionsUsed":["EXT_meshopt_compression"]})", triangle_bin());
    const auto refused_used = validate_glb(unknown_used);
    if (refused_used.accepted ||
        refused_used.reason.find("EXT_meshopt_compression") == std::string::npos)
        return 3;
    const auto draco = glb(std::string(triangle_json_head) +
        R"(,"extensionsUsed":["KHR_draco_mesh_compression"],)"
        R"("extensionsRequired":["KHR_draco_mesh_compression"]})", triangle_bin());
    if (validate_glb(draco).accepted) return 4;

    // An allowlisted extension passes.
    const auto allowed = glb(std::string(triangle_json_head) +
        R"(,"extensionsUsed":["KHR_materials_unlit"]})", triangle_bin());
    if (!validate_glb(allowed).accepted) return 5;

    // External buffer URIs break self-containment and are refused.
    const auto external = glb(
        R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":36,"uri":"http://example.com/x.bin"}],)"
        R"("bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36}],)"
        R"("accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3",)"
        R"("min":[0,0,0],"max":[1,1,0]}],)"
        R"("meshes":[{"primitives":[{"attributes":{"POSITION":0}}]}],)"
        R"("nodes":[{"mesh":0}],"scenes":[{"nodes":[0]}],"scene":0})", {});
    const auto refused_external = validate_glb(external);
    if (refused_external.accepted ||
        refused_external.reason.find("external") == std::string::npos)
        return 6;

    // Rigged mesh is refused until M4, with the reason saying so.
    const auto rigged = glb(std::string(triangle_json_head) +
        R"(,"skins":[{"joints":[0]}]})", triangle_bin());
    const auto refused_rigged = validate_glb(rigged);
    if (refused_rigged.accepted ||
        refused_rigged.reason.find("rigged") == std::string::npos)
        return 7;

    // The published policy carries the same numbers the validator enforces.
    const auto policy = homeworldz::mesh::acceptance_policy_json();
    if (policy.find("\"uploadPath\":\"/session/uploads/mesh\"") == std::string::npos ||
        policy.find("\"maxTriangles\":262144") == std::string::npos ||
        policy.find("\"maxRigInfluences\":4") == std::string::npos ||
        // A rig limit published beside "rigged": false reads as a contradiction
        // unless the payload says which limits are not yet in force. It is named
        // rather than removed, because an importer should read the number instead
        // of encoding its own (client core, 2026-08-04).
        // The skeleton a re-rig must target, named because glTF binds joints by
        // node index rather than by name: a client drawing arbitrary skeletons is
        // unconstrained, a viewer uses its own and no other, so one body rigged to
        // these names serves both families (client core, 2026-08-04).
        policy.find("\"skeleton\":\"second-life-avatar\"") == std::string::npos ||
        policy.find("\"skeletonJoints\":71") == std::string::npos ||
        policy.find("\"forwardLooking\":[\"maxRigInfluences\",\"skeleton\","
                    "\"skeletonJoints\"]") == std::string::npos ||
        policy.find("\"draco\":false") == std::string::npos ||
        policy.find("\"rigged\":false") == std::string::npos ||
        policy.find("KHR_texture_transform") == std::string::npos)
        return 8;
    return 0;
}
