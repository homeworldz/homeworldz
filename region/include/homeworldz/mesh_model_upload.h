#pragma once

// The viewer mesh model upload of ADR 0033 M2: the NewFileAgentInventory
// capability's "mesh" branch, spoken by Firestorm's Upload Model floater.
//
// The flow is two POSTs. The fee request carries item metadata (name,
// destination folders, permission masks) plus asset_resources whose texture
// binaries are empty; the region answers with a one-shot uploader URL. The
// upload POST to that URL carries asset_resources alone, textures included —
// so the region keeps the metadata from the first POST keyed by the URL it
// minted.
//
// The mesh binaries are complete type-49 payloads written by the viewer
// itself, physics included. Per the ADR's read-never-encode contract the
// region validates that they parse and stores them verbatim as canonical
// assets; there is nothing to convert.

#include "homeworldz/scene.h"
#include "homeworldz/viewer_protocol.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace homeworldz::mesh_model {

// Bounds a model upload must satisfy, alongside the route's own byte cap.
// Faces beyond eight cannot be named by a TextureEntry, and the texture
// count matches the published GLB acceptance policy.
inline constexpr std::size_t max_meshes = 64;
inline constexpr std::size_t max_instances = 64;
inline constexpr std::size_t max_textures = 16;
inline constexpr std::size_t max_faces = 8;

struct Metadata {
    std::string folder_id;
    std::string texture_folder_id;
    std::string name;
    std::string description;
    std::uint32_t everyone_permissions{};
    std::uint32_t group_permissions{};
    std::uint32_t next_permissions{};
};

struct Face {
    int image{-1}; // index into textures; -1 when the face has none
    std::array<float, 4> diffuse_color{1.0f, 1.0f, 1.0f, 1.0f};
};

struct Instance {
    std::array<float, 3> position{};
    std::array<float, 4> rotation{0.0f, 0.0f, 0.0f, 1.0f}; // x, y, z, w
    std::array<float, 3> scale{1.0f, 1.0f, 1.0f};
    int mesh{-1};
    std::string name;
    std::uint8_t material{3}; // wood, the uploader's fixed choice
    std::uint8_t physics_shape_type{2};
    std::vector<Face> faces;
};

struct Resources {
    std::vector<std::vector<std::byte>> meshes;
    std::vector<std::vector<std::byte>> textures;
    std::vector<Instance> instances;
};

struct FeeRequest {
    Metadata metadata;
    Resources resources;
};

// Parse and validate the fee request body (the full document: metadata and
// asset_resources). Texture binaries are permitted to be empty here — the
// viewer sends them that way. Nothing plus a reason on refusal.
struct FeeParse {
    bool ok{};
    // Whether the body was a mesh fee request at all. False sends the caller
    // back to the plain NewFileAgentInventory path; true with ok false is a
    // mesh upload to refuse with the error.
    bool mesh_request{};
    std::string error;
    FeeRequest request;
};
FeeParse parse_fee_request(std::string_view xml);

// Parse and validate the upload body (asset_resources alone). Every mesh
// must be a parseable type-49 payload; every non-empty texture must be
// JPEG2000. Same refusal contract as the fee parse.
struct UploadParse {
    bool ok{};
    std::string error;
    Resources resources;
};
UploadParse parse_upload(std::string_view xml);

// The TextureEntry for one instance: faces with an image index draw their
// uploaded texture, the rest the fallback; diffuse colors ride along. The
// texture span maps image indexes to stored asset UUIDs; an index without a
// stored texture (empty at upload) falls back too.
std::vector<std::byte> instance_texture_entry(
    const viewer::Uuid& fallback_texture, std::span<const Face> faces,
    std::span<const std::optional<viewer::Uuid>> textures);

// Quaternion helpers for turning instance world transforms into the root-
// relative ones a linkset stores. Quaternions are (x, y, z, w).
std::array<float, 4> quaternion_conjugate(const std::array<float, 4>& value);
std::array<float, 4> quaternion_multiply(
    const std::array<float, 4>& left, const std::array<float, 4>& right);
std::array<float, 3> quaternion_rotate(
    const std::array<float, 4>& rotation, const std::array<float, 3>& value);
// The scene stores rotations as the vector part of a normalized quaternion
// with a non-negative w; negate when needed, then drop w.
scene::Vector3 packed_rotation(const std::array<float, 4>& value);

// Replies. The fee reply's zero costs are the truth of this grid — there is
// no upload economy — not placeholders.
std::string fee_response_xml(std::string_view uploader);
// A refusal in the shape the viewer's upload error path reads
// (error.message / error.identifier), sized for showing a creator verbatim.
std::string error_response_xml(std::string_view message);

} // namespace homeworldz::mesh_model
