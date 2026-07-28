// The mesh acceptance gate of ADR 0033: what this region accepts as an
// uploaded GLB, with the policy itself published to clients rather than
// mirrored by them — an importing client must refuse exactly what upload
// would refuse, and two hand-maintained copies of one policy drift. The
// numbers here are therefore the single definition: the validator enforces
// them and the session hello serves them, from the same symbols.
#ifndef HOMEWORLDZ_MESH_ACCEPTANCE_H
#define HOMEWORLDZ_MESH_ACCEPTANCE_H

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace homeworldz::mesh {

// A GLB larger than this is refused before parsing. Well under the vault
// blob cap: a mesh source this size is a packaging problem, not content.
inline constexpr std::uint64_t max_glb_bytes = 32ull << 20;
// Visual triangles across every primitive of every mesh in the file.
inline constexpr std::uint32_t max_triangles = 262144;
// Materials per file: matches the per-prim face limit viewers render.
inline constexpr std::uint32_t max_materials = 8;
// Distinct textures per file.
inline constexpr std::uint32_t max_textures = 16;
// Bytes of any single embedded image.
inline constexpr std::uint64_t max_image_bytes = 8ull << 20;
// Bento skinning allows at most this many influences per vertex. Published
// now even though rigged mesh lands with M4 (ADR 0033), so importing clients
// already read it rather than encode it.
inline constexpr std::uint32_t max_rig_influences = 4;
// Draco-compressed GLBs are refused in v1 rather than half supported.
inline constexpr bool draco_accepted = false;
// Rigged mesh (skins) is refused until M4; refusing is honest, guessing at a
// skeleton mapping is not.
inline constexpr bool rigged_accepted = false;

// The glTF extensions this gate accepts. Anything else — used or required —
// is refused, not ignored, so content never renders differently on the
// client that understands more.
inline constexpr std::string_view allowed_extensions[] = {
    "KHR_materials_emissive_strength",
    "KHR_materials_ior",
    "KHR_materials_specular",
    "KHR_materials_unlit",
    "KHR_texture_transform",
};

// Where a session client uploads: POST, body is the GLB, authorized by the
// same region ticket the WebSocket authenticates with, as a bearer token —
// one credential, both transports.
inline constexpr std::string_view upload_path = "/session/uploads/mesh";

// The acceptance policy as the JSON object served in the session hello
// (the read-never-encode contract of ADR 0033).
std::string acceptance_policy_json();

struct Acceptance {
    bool accepted{};
    // Actionable when refused: names the rule and the offending value, since
    // the creator hearing this may be several tools away from the file.
    std::string reason;
    std::uint32_t triangles{};
    std::uint32_t materials{};
    std::uint32_t textures{};
};

// validate_glb applies the gate to an uploaded GLB: container and version,
// self-containment (no external buffer or image URIs), the extension
// allowlist, and the caps above.
Acceptance validate_glb(std::span<const std::byte> content);

} // namespace homeworldz::mesh

#endif
