#include "homeworldz/mesh_acceptance.h"
#include "homeworldz/avatar_joints.h"

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
        ",\"maxJointsPerMesh\":" + std::to_string(max_joints_per_mesh) +
        ",\"forwardLooking\":[" +
        std::string(rigged_accepted ? ""
            : "\"maxRigInfluences\",\"skeleton\",\"skeletonJoints\","
              "\"maxJointsPerMesh\"") + "]" +
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

    // Rig validation runs before the not-yet-accepted refusal, so a creator
    // preparing content against the published policy learns what is actually
    // wrong with their rig rather than only that rigs are not accepted. Same
    // reasoning that published the skeleton and the limits ahead of M4: a
    // re-rig cannot recover from being aimed wrong, and finding out late costs
    // whoever authored it.
    for (cgltf_size skin_index = 0; skin_index < data->skins_count; ++skin_index) {
        const auto& skin = data->skins[skin_index];
        // Names before the count, deliberately. The viewer's own limit counts
        // *recognized* joints (fslocalmeshimportbase.cpp, enforceRigJointLimit),
        // not the declared list, and a body rigged to another skeleton fails
        // both at once: a MakeHuman export declares 163 joints, none of which
        // are ours. Reporting "163 joints; the limit is 110" would send its
        // author to trim joints when the actual problem is the whole skeleton.
        // Refusing unknown names first makes the two counts identical by the
        // time the limit is applied, so this is also correct by construction
        // rather than by coincidence.
        for (cgltf_size joint_index = 0; joint_index < skin.joints_count; ++joint_index) {
            const auto* node = skin.joints[joint_index];
            const std::string_view name = node != nullptr && node->name != nullptr ? node->name : "";
            if (name.empty())
                return refuse("a skin binds an unnamed joint; every joint must name one of the " +
                              std::to_string(rigged_skeleton_joints) + " " +
                              std::string(rigged_skeleton) + " joints");
            // The name is what a viewer resolves, and it resolves aliases and
            // attachment points as well as canonical bones - so this accepts
            // every spelling a viewer would, including the `hip` and `abdomen`
            // that Blender and Avastar emit. Naming the offending joint matters
            // because the creator hearing it may be several tools away from the
            // file.
            if (!is_riggable_joint(name))
                return refuse("a skin binds joint \"" + std::string(name) +
                              "\", which is not a joint of the " +
                              std::string(rigged_skeleton) + " skeleton");
        }
        if (skin.joints_count > max_joints_per_mesh)
            return refuse("a skin binds " + std::to_string(skin.joints_count) +
                          " joints; the limit is " + std::to_string(max_joints_per_mesh));
    }
    // More than four influences per vertex arrives as a second joint/weight
    // set. glTF numbers them JOINTS_0, JOINTS_1 and so on, four to a set, so
    // any index above zero is a fifth influence by definition.
    for (cgltf_size mesh_index = 0; mesh_index < data->meshes_count; ++mesh_index) {
        const auto& mesh_value = data->meshes[mesh_index];
        for (cgltf_size primitive_index = 0; primitive_index < mesh_value.primitives_count;
             ++primitive_index) {
            const auto& primitive = mesh_value.primitives[primitive_index];
            for (cgltf_size attribute = 0; attribute < primitive.attributes_count; ++attribute) {
                const auto& value = primitive.attributes[attribute];
                if ((value.type == cgltf_attribute_type_joints ||
                     value.type == cgltf_attribute_type_weights) && value.index > 0)
                    return refuse("a primitive declares more than " +
                                  std::to_string(max_rig_influences) +
                                  " influences per vertex");
            }
        }
    }
    if (!rigged_accepted && data->skins_count != 0)
        return refuse("rigged mesh is not accepted yet (ADR 0033 M4); upload the static mesh");

    // A morph target at zero costs nothing: the base geometry is the intended
    // default and is what the converter emits. A non-zero default weight means
    // the intended shape is the morphed one, and serving the base would be
    // serving a different mesh without saying so.
    if (!nonzero_morph_weights_accepted) {
        const auto declared_nonzero = [](const float* weights, std::size_t count) {
            for (std::size_t index = 0; index < count; ++index)
                if (weights[index] < -1e-6F || weights[index] > 1e-6F) return true;
            return false;
        };
        for (std::size_t index = 0; index < data->meshes_count; ++index)
            if (declared_nonzero(data->meshes[index].weights,
                                 data->meshes[index].weights_count))
                return refuse("a mesh declares a non-zero morph target weight; the"
                              " shape served would be the unmorphed base. Bake the"
                              " morphs into the vertices and export again");
        for (std::size_t index = 0; index < data->nodes_count; ++index)
            if (declared_nonzero(data->nodes[index].weights,
                                 data->nodes[index].weights_count))
                return refuse("a node declares a non-zero morph target weight; the"
                              " shape served would be the unmorphed base. Bake the"
                              " morphs into the vertices and export again");
    }

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
