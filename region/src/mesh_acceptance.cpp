#include "homeworldz/mesh_acceptance.h"

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#include <cstring>
#include <string_view>

namespace homeworldz::mesh {
namespace {

bool extension_allowed(std::string_view name) {
    for (const auto allowed : allowed_extensions)
        if (name == allowed) return true;
    return false;
}

Acceptance refuse(std::string reason) { return {false, std::move(reason), 0, 0, 0}; }

// data: URIs are self-contained; anything else reaches outside the file the
// creator uploaded, which the canonical blob must never do (ADR 0033).
bool external_uri(const char* uri) {
    if (uri == nullptr || *uri == '\0') return false;
    return std::strncmp(uri, "data:", 5) != 0;
}

} // namespace

std::string acceptance_policy_json() {
    std::string extensions;
    for (const auto allowed : allowed_extensions) {
        if (!extensions.empty()) extensions += ',';
        extensions += '"';
        extensions += allowed;
        extensions += '"';
    }
    return "{\"format\":\"glb\",\"uploadPath\":\"" + std::string(upload_path) +
        "\",\"maxFileBytes\":" + std::to_string(max_glb_bytes) +
        ",\"maxTriangles\":" + std::to_string(max_triangles) +
        ",\"maxMaterials\":" + std::to_string(max_materials) +
        ",\"maxTextures\":" + std::to_string(max_textures) +
        ",\"maxImageBytes\":" + std::to_string(max_image_bytes) +
        ",\"maxRigInfluences\":" + std::to_string(max_rig_influences) +
        // Which limits are in force now and which describe a capability not yet
        // switched on. maxRigInfluences was published ahead of M4 so importers
        // read the number rather than encode it, but a reader seeing a rig limit
        // beside "rigged": false reasonably calls that a contradiction - the
        // client core did, 2026-08-04. Saying so in the payload costs one key and
        // removes the guess.
        ",\"skeleton\":\"" + std::string(rigged_skeleton) + "\"" +
        ",\"skeletonJoints\":" + std::to_string(rigged_skeleton_joints) +
        ",\"forwardLooking\":[" +
        std::string(rigged_accepted ? ""
            : "\"maxRigInfluences\",\"skeleton\",\"skeletonJoints\"") + "]" +
        ",\"draco\":" + (draco_accepted ? "true" : "false") +
        ",\"rigged\":" + (rigged_accepted ? "true" : "false") +
        ",\"allowedExtensions\":[" + extensions + "]}";
}

Acceptance validate_glb(std::span<const std::byte> content) {
    if (content.size() > max_glb_bytes)
        return refuse("file is " + std::to_string(content.size()) +
                      " bytes; the limit is " + std::to_string(max_glb_bytes));
    if (content.size() < 12 || std::memcmp(content.data(), "glTF", 4) != 0)
        return refuse("not a GLB container");

    cgltf_options options{};
    cgltf_data* data = nullptr;
    if (cgltf_parse(&options, content.data(), content.size(), &data) != cgltf_result_success)
        return refuse("the GLB does not parse as glTF 2.0");
    struct Free {
        cgltf_data* data;
        ~Free() { cgltf_free(data); }
    } freer{data};
    if (data->file_type != cgltf_file_type_glb)
        return refuse("only the GLB container is accepted");
    if (cgltf_validate(data) != cgltf_result_success)
        return refuse("the glTF structure fails validation");

    // Refused, not ignored: an extension only one client understands renders
    // differently on the client that understands more (ADR 0033).
    for (cgltf_size index = 0; index < data->extensions_used_count; ++index) {
        const std::string_view name = data->extensions_used[index];
        if (!extension_allowed(name))
            return refuse("extension " + std::string(name) + " is not accepted");
    }
    for (cgltf_size index = 0; index < data->extensions_required_count; ++index) {
        const std::string_view name = data->extensions_required[index];
        if (!extension_allowed(name))
            return refuse("required extension " + std::string(name) + " is not accepted");
    }

    // Self-containment: the canonical blob must never reach outside itself.
    for (cgltf_size index = 0; index < data->buffers_count; ++index)
        if (external_uri(data->buffers[index].uri))
            return refuse("buffers must be embedded; external buffer URIs are not accepted");
    for (cgltf_size index = 0; index < data->images_count; ++index) {
        const auto& image = data->images[index];
        if (external_uri(image.uri))
            return refuse("images must be embedded; external image URIs are not accepted");
        if (image.buffer_view != nullptr && image.buffer_view->size > max_image_bytes)
            return refuse("an embedded image is " + std::to_string(image.buffer_view->size) +
                          " bytes; the limit is " + std::to_string(max_image_bytes));
    }

    if (!rigged_accepted && data->skins_count != 0)
        return refuse("rigged mesh is not accepted yet (ADR 0033 M4); upload the static mesh");

    Acceptance result;
    result.materials = static_cast<std::uint32_t>(data->materials_count);
    result.textures = static_cast<std::uint32_t>(data->textures_count);
    if (result.materials > max_materials)
        return refuse(std::to_string(result.materials) + " materials; the limit is " +
                      std::to_string(max_materials));
    if (result.textures > max_textures)
        return refuse(std::to_string(result.textures) + " textures; the limit is " +
                      std::to_string(max_textures));

    std::uint64_t triangles = 0;
    for (cgltf_size mesh_index = 0; mesh_index < data->meshes_count; ++mesh_index) {
        const auto& mesh_value = data->meshes[mesh_index];
        for (cgltf_size primitive_index = 0; primitive_index < mesh_value.primitives_count;
             ++primitive_index) {
            const auto& primitive = mesh_value.primitives[primitive_index];
            if (primitive.type != cgltf_primitive_type_triangles)
                return refuse("only triangle primitives are accepted");
            cgltf_size vertices = 0;
            if (primitive.indices != nullptr) {
                vertices = primitive.indices->count;
            } else {
                for (cgltf_size attribute = 0; attribute < primitive.attributes_count; ++attribute) {
                    if (primitive.attributes[attribute].type == cgltf_attribute_type_position) {
                        vertices = primitive.attributes[attribute].data->count;
                        break;
                    }
                }
            }
            triangles += vertices / 3;
        }
    }
    if (triangles == 0)
        return refuse("the file contains no triangles");
    if (triangles > max_triangles)
        return refuse(std::to_string(triangles) + " triangles; the limit is " +
                      std::to_string(max_triangles));
    result.triangles = static_cast<std::uint32_t>(triangles);
    result.accepted = true;
    return result;
}

} // namespace homeworldz::mesh
