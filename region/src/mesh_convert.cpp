#include "homeworldz/mesh_convert.h"

#include "homeworldz/slmesh.h"

#include <cgltf.h>
#include <meshoptimizer.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <map>
#include <string>

namespace homeworldz::mesh {
namespace {

Conversion fail(std::string reason) {
    Conversion result;
    result.error = std::move(reason);
    return result;
}

// One material face being accumulated: vertices across every primitive (and
// node instance) that shares the material, indices into them.
struct Face {
    std::vector<std::array<float, 3>> positions;
    std::vector<std::array<float, 3>> normals;
    std::vector<std::array<float, 2>> texcoords;
    std::vector<std::uint32_t> indices;
    bool any_missing_normals{};
    bool any_missing_texcoords{};
};

// glTF is +Y up; a Homeworldz region is +Z up. The two directions of this
// pipeline apply this map and its inverse, so an asset authored to the glTF
// convention stands upright in-world and a derived glTF is conformant for
// every tool that reads it (ADR 0033 "Coordinates"). Applied after node
// transforms, which are expressed in the source's own frame.
void to_region_axes(std::array<float, 3>& value) {
    const float x = value[0], y = value[1], z = value[2];
    value[0] = x;
    value[1] = -z;
    value[2] = y;
}

void transform_point(const float matrix[16], std::array<float, 3>& point) {
    const float x = point[0], y = point[1], z = point[2];
    point[0] = matrix[0] * x + matrix[4] * y + matrix[8] * z + matrix[12];
    point[1] = matrix[1] * x + matrix[5] * y + matrix[9] * z + matrix[13];
    point[2] = matrix[2] * x + matrix[6] * y + matrix[10] * z + matrix[14];
}

void transform_direction(const float matrix[16], std::array<float, 3>& direction) {
    const float x = direction[0], y = direction[1], z = direction[2];
    direction[0] = matrix[0] * x + matrix[4] * y + matrix[8] * z;
    direction[1] = matrix[1] * x + matrix[5] * y + matrix[9] * z;
    direction[2] = matrix[2] * x + matrix[6] * y + matrix[10] * z;
    const float length = std::sqrt(direction[0] * direction[0] + direction[1] * direction[1] +
                                   direction[2] * direction[2]);
    if (length > 0.0f)
        for (int axis = 0; axis < 3; ++axis) direction[axis] /= length;
}

// simplify produces a level's index list at roughly ratio of the source
// triangles, over the same vertex buffer. meshoptimizer may stop early when
// the error bound binds; whatever it returns is real geometry.
std::vector<std::uint32_t> simplify(const Face& face, double ratio) {
    const std::size_t target = (std::max<std::size_t>)(
        static_cast<std::size_t>(static_cast<double>(face.indices.size()) * ratio) / 3 * 3, 3);
    std::vector<std::uint32_t> out(face.indices.size());
    float error = 0.0f;
    const auto produced = meshopt_simplify(
        out.data(), face.indices.data(), face.indices.size(), &face.positions[0][0],
        face.positions.size(), sizeof(float) * 3, target, 0.05f,
        meshopt_SimplifyLockBorder, &error);
    out.resize(produced);
    if (out.size() < 3) out.assign(face.indices.begin(), face.indices.begin() + 3);
    return out;
}

// to_submesh compacts a face + index list into the u16-indexed submesh the
// wire format takes, dropping vertices the level no longer references.
std::optional<slmesh::Submesh> to_submesh(const Face& face,
                                          const std::vector<std::uint32_t>& indices) {
    slmesh::Submesh submesh;
    std::vector<std::uint32_t> remap(face.positions.size(),
                                     std::numeric_limits<std::uint32_t>::max());
    for (const auto index : indices) {
        if (index >= face.positions.size()) return std::nullopt;
        if (remap[index] == std::numeric_limits<std::uint32_t>::max()) {
            remap[index] = static_cast<std::uint32_t>(submesh.positions.size());
            submesh.positions.push_back(face.positions[index]);
            if (!face.any_missing_normals) submesh.normals.push_back(face.normals[index]);
            if (!face.any_missing_texcoords) submesh.texcoords.push_back(face.texcoords[index]);
        }
        if (submesh.positions.size() > 65535) return std::nullopt;
        submesh.indices.push_back(static_cast<std::uint16_t>(remap[index]));
    }
    return submesh;
}

void accumulate_declared_bounds(const cgltf_data* data, std::array<float, 3>& low,
                                std::array<float, 3>& high, bool& any) {
    const cgltf_scene* scene = data->scene != nullptr ? data->scene
        : (data->scenes_count != 0 ? &data->scenes[0] : nullptr);
    if (scene == nullptr) return;
    std::vector<const cgltf_node*> pending(scene->nodes, scene->nodes + scene->nodes_count);
    while (!pending.empty()) {
        const auto* node = pending.back();
        pending.pop_back();
        for (cgltf_size child = 0; child < node->children_count; ++child)
            pending.push_back(node->children[child]);
        if (node->mesh == nullptr) continue;
        float world[16];
        cgltf_node_transform_world(node, world);
        for (cgltf_size primitive_index = 0; primitive_index < node->mesh->primitives_count;
             ++primitive_index) {
            const auto& primitive = node->mesh->primitives[primitive_index];
            for (cgltf_size attribute = 0; attribute < primitive.attributes_count; ++attribute) {
                const auto& value = primitive.attributes[attribute];
                if (value.type != cgltf_attribute_type_position || value.data == nullptr ||
                    !value.data->has_min || !value.data->has_max)
                    continue;
                for (int corner = 0; corner < 8; ++corner) {
                    std::array<float, 3> point{
                        (corner & 1) != 0 ? value.data->max[0] : value.data->min[0],
                        (corner & 2) != 0 ? value.data->max[1] : value.data->min[1],
                        (corner & 4) != 0 ? value.data->max[2] : value.data->min[2]};
                    transform_point(world, point);
                    to_region_axes(point);
                    for (int axis = 0; axis < 3; ++axis) {
                        low[axis] = (std::min)(low[axis], point[axis]);
                        high[axis] = (std::max)(high[axis], point[axis]);
                    }
                    any = true;
                }
            }
        }
    }
}

} // namespace

WorldBounds declared_world_bounds(std::span<const std::byte> glb) {
    cgltf_options options{};
    cgltf_data* data = nullptr;
    if (cgltf_parse(&options, glb.data(), glb.size(), &data) != cgltf_result_success) return {};
    struct Free {
        cgltf_data* data;
        ~Free() { cgltf_free(data); }
    } freer{data};
    std::array<float, 3> low{std::numeric_limits<float>::max(),
                             std::numeric_limits<float>::max(),
                             std::numeric_limits<float>::max()};
    std::array<float, 3> high{std::numeric_limits<float>::lowest(),
                              std::numeric_limits<float>::lowest(),
                              std::numeric_limits<float>::lowest()};
    bool any = false;
    accumulate_declared_bounds(data, low, high, any);
    if (!any) return {};
    WorldBounds bounds;
    bounds.ok = true;
    for (int axis = 0; axis < 3; ++axis) {
        bounds.center[axis] = (low[axis] + high[axis]) * 0.5f;
        bounds.extent[axis] = (std::max)(high[axis] - low[axis], 0.001f);
    }
    return bounds;
}

// The order faces come out in: materials as the scene walk first encounters
// them, the null material counting as one of its own. Factored out because two
// callers depend on agreeing about it exactly - the converter, which emits one
// face per material, and texture extraction, which must name a texture for
// face N. Two copies of this walk would be two things that must agree and
// eventually would not.
std::vector<const cgltf_material*> ordered_materials(const cgltf_data* data) {
    std::vector<const cgltf_material*> order;
    const cgltf_scene* scene = data->scene != nullptr ? data->scene
        : (data->scenes_count != 0 ? &data->scenes[0] : nullptr);
    if (scene == nullptr) return order;
    std::vector<const cgltf_node*> pending(scene->nodes, scene->nodes + scene->nodes_count);
    while (!pending.empty()) {
        const auto* node = pending.back();
        pending.pop_back();
        for (cgltf_size child = 0; child < node->children_count; ++child)
            pending.push_back(node->children[child]);
        if (node->mesh == nullptr) continue;
        for (cgltf_size index = 0; index < node->mesh->primitives_count; ++index) {
            const auto* material = node->mesh->primitives[index].material;
            if (std::find(order.begin(), order.end(), material) == order.end())
                order.push_back(material);
        }
    }
    return order;
}

TextureExtraction extract_textures(std::span<const std::byte> glb) {
    TextureExtraction result;
    const auto fail = [&](std::string reason) {
        result.ok = false;
        result.error = std::move(reason);
        return result;
    };
    cgltf_options options{};
    cgltf_data* data = nullptr;
    if (cgltf_parse(&options, glb.data(), glb.size(), &data) != cgltf_result_success ||
        data == nullptr)
        return fail("the GLB does not parse");
    struct Free {
        cgltf_data* data;
        ~Free() { cgltf_free(data); }
    } freer{data};
    if (cgltf_load_buffers(&options, data, nullptr) != cgltf_result_success)
        return fail("the GLB's embedded buffers do not load");

    // One entry per distinct image, so a texture shared by several materials is
    // stored and converted once and the faces name the same asset.
    std::map<const cgltf_image*, int> index_of;
    for (const auto* material : ordered_materials(data)) {
        int texture = -1;
        const cgltf_image* image = nullptr;
        if (material != nullptr && material->has_pbr_metallic_roughness)
            if (const auto* view = material->pbr_metallic_roughness.base_color_texture.texture;
                view != nullptr)
                image = view->image;
        if (image != nullptr) {
            if (const auto found = index_of.find(image); found != index_of.end()) {
                texture = found->second;
            } else if (image->buffer_view == nullptr) {
                // The acceptance gate requires self-containment, so an image
                // reachable only by URI is a gate escape rather than content.
                return fail("a texture is not embedded in the GLB");
            } else {
                const auto* view = image->buffer_view;
                if (view->buffer == nullptr || view->buffer->data == nullptr)
                    return fail("a texture's buffer is unreadable");
                const auto* bytes = static_cast<const std::byte*>(view->buffer->data) +
                                    view->offset;
                SourceTexture stored;
                stored.mime = image->mime_type != nullptr ? image->mime_type : "";
                if (stored.mime != "image/png" && stored.mime != "image/jpeg")
                    return fail("a texture is neither PNG nor JPEG");
                stored.bytes.assign(bytes, bytes + view->size);
                if (stored.bytes.empty()) return fail("a texture carries no bytes");
                texture = static_cast<int>(result.textures.size());
                index_of.emplace(image, texture);
                result.textures.push_back(std::move(stored));
            }
        }
        result.face_textures.push_back(texture);
    }
    result.ok = true;
    return result;
}

Conversion convert_glb(std::span<const std::byte> glb) {
    cgltf_options options{};
    cgltf_data* data = nullptr;
    if (cgltf_parse(&options, glb.data(), glb.size(), &data) != cgltf_result_success)
        return fail("the GLB does not parse as glTF 2.0");
    struct Free {
        cgltf_data* data;
        ~Free() { cgltf_free(data); }
    } freer{data};
    if (cgltf_load_buffers(&options, data, nullptr) != cgltf_result_success)
        return fail("the GLB's embedded buffers do not load");

    // Faces in first-encounter material order; the null material is a face of
    // its own. The gate already capped materials at eight. The order comes from
    // ordered_materials() rather than from this loop's own accounting, so
    // extract_textures() names textures for the same faces this emits.
    std::map<const cgltf_material*, std::size_t> face_of;
    std::vector<Face> faces;
    for (const auto* material : ordered_materials(data)) {
        face_of.emplace(material, faces.size());
        faces.emplace_back();
    }

    const cgltf_scene* scene = data->scene != nullptr ? data->scene
        : (data->scenes_count != 0 ? &data->scenes[0] : nullptr);
    if (scene == nullptr) return fail("the GLB has no scene");

    // Walk every node reachable from the scene, meshes transformed to world
    // space so a multi-part model keeps its arrangement.
    std::vector<const cgltf_node*> pending(scene->nodes, scene->nodes + scene->nodes_count);
    while (!pending.empty()) {
        const auto* node = pending.back();
        pending.pop_back();
        for (cgltf_size child = 0; child < node->children_count; ++child)
            pending.push_back(node->children[child]);
        if (node->mesh == nullptr) continue;
        float world[16];
        cgltf_node_transform_world(node, world);
        for (cgltf_size primitive_index = 0; primitive_index < node->mesh->primitives_count;
             ++primitive_index) {
            const auto& primitive = node->mesh->primitives[primitive_index];
            if (primitive.type != cgltf_primitive_type_triangles)
                return fail("only triangle primitives are accepted");
            const cgltf_accessor* position_accessor = nullptr;
            const cgltf_accessor* normal_accessor = nullptr;
            const cgltf_accessor* texcoord_accessor = nullptr;
            for (cgltf_size attribute = 0; attribute < primitive.attributes_count; ++attribute) {
                const auto& value = primitive.attributes[attribute];
                if (value.type == cgltf_attribute_type_position) position_accessor = value.data;
                if (value.type == cgltf_attribute_type_normal) normal_accessor = value.data;
                if (value.type == cgltf_attribute_type_texcoord && value.index == 0)
                    texcoord_accessor = value.data;
            }
            if (position_accessor == nullptr) return fail("a primitive carries no positions");

            auto [where, inserted] = face_of.try_emplace(primitive.material, faces.size());
            if (inserted) faces.emplace_back();
            auto& face = faces[where->second];
            const auto base = static_cast<std::uint32_t>(face.positions.size());

            for (cgltf_size vertex = 0; vertex < position_accessor->count; ++vertex) {
                std::array<float, 3> position{};
                if (!cgltf_accessor_read_float(position_accessor, vertex, position.data(), 3))
                    return fail("a position accessor is unreadable");
                transform_point(world, position);
                to_region_axes(position);
                face.positions.push_back(position);
                if (normal_accessor != nullptr) {
                    std::array<float, 3> normal{};
                    if (!cgltf_accessor_read_float(normal_accessor, vertex, normal.data(), 3))
                        return fail("a normal accessor is unreadable");
                    transform_direction(world, normal);
                    to_region_axes(normal);
                    face.normals.push_back(normal);
                } else {
                    face.normals.push_back({0, 0, 1});
                    face.any_missing_normals = true;
                }
                if (texcoord_accessor != nullptr) {
                    std::array<float, 2> texcoord{};
                    if (!cgltf_accessor_read_float(texcoord_accessor, vertex, texcoord.data(), 2))
                        return fail("a texture-coordinate accessor is unreadable");
                    face.texcoords.push_back(texcoord);
                } else {
                    face.texcoords.push_back({0, 0});
                    face.any_missing_texcoords = true;
                }
            }
            if (primitive.indices != nullptr) {
                for (cgltf_size index = 0; index < primitive.indices->count; ++index)
                    face.indices.push_back(base + static_cast<std::uint32_t>(
                        cgltf_accessor_read_index(primitive.indices, index)));
            } else {
                for (cgltf_size index = 0; index < position_accessor->count; ++index)
                    face.indices.push_back(base + static_cast<std::uint32_t>(index));
            }
        }
    }
    if (faces.empty()) return fail("the GLB contains no triangle geometry");

    // Sources without normals get computed ones — per-vertex averages of the
    // adjoining face normals — because a mesh without normals lights
    // unpredictably in viewers, and refusing would be worse.
    for (auto& face : faces) {
        if (!face.any_missing_normals) continue;
        std::vector<std::array<float, 3>> accumulated(face.positions.size(), {0, 0, 0});
        for (std::size_t triangle = 0; triangle + 2 < face.indices.size(); triangle += 3) {
            const auto& a = face.positions[face.indices[triangle]];
            const auto& b = face.positions[face.indices[triangle + 1]];
            const auto& c = face.positions[face.indices[triangle + 2]];
            const std::array<float, 3> ab{b[0] - a[0], b[1] - a[1], b[2] - a[2]};
            const std::array<float, 3> ac{c[0] - a[0], c[1] - a[1], c[2] - a[2]};
            const std::array<float, 3> cross{ab[1] * ac[2] - ab[2] * ac[1],
                                             ab[2] * ac[0] - ab[0] * ac[2],
                                             ab[0] * ac[1] - ab[1] * ac[0]};
            for (int corner = 0; corner < 3; ++corner)
                for (int axis = 0; axis < 3; ++axis)
                    accumulated[face.indices[triangle + corner]][axis] += cross[axis];
        }
        for (auto& normal : accumulated) {
            const float length = std::sqrt(normal[0] * normal[0] + normal[1] * normal[1] +
                                           normal[2] * normal[2]);
            if (length > 0.0f)
                for (int axis = 0; axis < 3; ++axis) normal[axis] /= length;
            else
                normal = {0, 0, 1};
        }
        face.normals = std::move(accumulated);
        face.any_missing_normals = false;
    }

    // Sources without texture coordinates get computed ones too, and this one
    // is load-bearing: a viewer zero-fills missing TexCoord0, its tangent
    // computation then divides by a zero UV determinant, and NaN tangents
    // stop the triangles rasterizing at all — faces select and silhouette but
    // never draw (observed live on Firestorm, 2026-07-29). Real SL uploads
    // always carry UVs, so viewers never exercise the missing-UV path. The
    // sheared planar map (x+y, y+z) keeps per-triangle UV variation on every
    // axis-aligned plane; only planes normal to (1,-1,1) degrade, and those
    // still vary numerically.
    for (auto& face : faces) {
        if (!face.any_missing_texcoords) continue;
        face.texcoords.clear();
        for (const auto& position : face.positions)
            face.texcoords.push_back({position[0] + position[1] + 0.5f,
                                      position[1] + position[2] + 0.5f});
        face.any_missing_texcoords = false;
    }

    // Normalize to the unit domain by the same declared bounds the upload
    // used for the wrapper prim's scale (declared_world_bounds): geometry
    // spans [-0.5, 0.5] per axis, and the prim scale stretches it back to
    // authored size. Viewers render mesh this way; so do we.
    const auto bounds = declared_world_bounds(glb);
    if (!bounds.ok) return fail("the GLB declares no position bounds");
    for (auto& face : faces)
        for (auto& position : face.positions)
            for (int axis = 0; axis < 3; ++axis)
                position[axis] = (position[axis] - bounds.center[axis]) / bounds.extent[axis];

    // The LOD chain. Ratios follow the viewer's expectations of scale steps;
    // whatever the simplifier genuinely achieves is what ships.
    slmesh::Mesh mesh;
    Conversion result;
    result.faces = faces.size();
    for (const auto& face : faces) {
        if (face.indices.size() % 3 != 0) return fail("a face's triangle list is ragged");
        const auto high = to_submesh(face, face.indices);
        if (!high) return fail("a material face exceeds 65535 vertices");
        result.high_triangles += high->indices.size() / 3;
        mesh.high.push_back(*high);
        const std::array<std::pair<slmesh::Level*, double>, 3> levels{
            {{&mesh.medium, 0.5}, {&mesh.low, 0.25}, {&mesh.lowest, 0.1}}};
        for (const auto& [level, ratio] : levels) {
            const auto simplified = simplify(face, ratio);
            const auto submesh = to_submesh(face, simplified);
            if (!submesh) return fail("simplification produced an invalid level");
            if (level == &mesh.lowest) result.lowest_triangles += submesh->indices.size() / 3;
            level->push_back(*submesh);
        }
    }

    // Physics: the normalized unit box as a single convex hull — the
    // conservative shape until V-HACD decomposition lands (ADR 0033). In the
    // normalized domain that box is exactly [-0.5, 0.5]^3, scaled by the prim.
    for (int corner = 0; corner < 8; ++corner)
        mesh.physics_hull.push_back({(corner & 1) != 0 ? 0.5f : -0.5f,
                                     (corner & 2) != 0 ? 0.5f : -0.5f,
                                     (corner & 4) != 0 ? 0.5f : -0.5f});

    result.sl_mesh = slmesh::serialize(mesh);
    if (result.sl_mesh.empty()) return fail("the converted mesh failed to serialize");
    result.ok = true;
    return result;
}

} // namespace homeworldz::mesh
