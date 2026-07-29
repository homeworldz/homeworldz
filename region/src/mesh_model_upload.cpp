#include "homeworldz/mesh_model_upload.h"

#include "homeworldz/llsd_xml.h"
#include "homeworldz/mesh_acceptance.h"
#include "homeworldz/slmesh.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace homeworldz::mesh_model {

namespace {

std::string llsd_text(const llsd::Value& map, std::string_view key) {
    const auto* value = map.find(key);
    if (value == nullptr) return {};
    return value->text;
}

bool read_floats(const llsd::Value* value, std::span<float> out) {
    if (value == nullptr || value->type != llsd::Value::Type::array ||
        value->elements.size() != out.size())
        return false;
    for (std::size_t index = 0; index < out.size(); ++index) {
        const auto element = static_cast<float>(value->elements[index].as_real());
        if (!std::isfinite(element)) return false;
        out[index] = element;
    }
    return true;
}

bool looks_like_j2c(std::span<const std::byte> content) {
    const auto byte = [&](std::size_t index) {
        return static_cast<unsigned char>(content[index]);
    };
    const bool codestream = content.size() >= 4 &&
        byte(0) == 0xff && byte(1) == 0x4f && byte(2) == 0xff && byte(3) == 0x51;
    const bool jp2 = content.size() >= 12 &&
        byte(0) == 0 && byte(1) == 0 && byte(2) == 0 && byte(3) == 12 &&
        std::memcmp(content.data() + 4, "jP  ", 4) == 0 &&
        byte(8) == 0x0d && byte(9) == 0x0a && byte(10) == 0x87 && byte(11) == 0x0a;
    return codestream || jp2;
}

// Read asset_resources. Textures may be empty binaries at the fee stage;
// meshes are validated as parseable type-49 only when `require_meshes_parse`
// (the upload stage) — the fee stage checks bounds alone, since the viewer
// sends identical mesh bytes both times and the paid-for parse happens once.
bool read_resources(const llsd::Value& map, Resources& out, bool upload_stage,
                    std::string& error) {
    const auto* mesh_list = map.find("mesh_list");
    const auto* texture_list = map.find("texture_list");
    const auto* instance_list = map.find("instance_list");
    if (mesh_list == nullptr || mesh_list->type != llsd::Value::Type::array ||
        mesh_list->elements.empty()) {
        error = "the upload names no meshes";
        return false;
    }
    if (instance_list == nullptr || instance_list->type != llsd::Value::Type::array ||
        instance_list->elements.empty()) {
        error = "the upload names no instances";
        return false;
    }
    if (mesh_list->elements.size() > max_meshes) {
        error = "the upload exceeds " + std::to_string(max_meshes) + " meshes";
        return false;
    }
    if (instance_list->elements.size() > max_instances) {
        error = "the upload exceeds " + std::to_string(max_instances) + " instances";
        return false;
    }
    const std::size_t texture_count =
        texture_list != nullptr && texture_list->type == llsd::Value::Type::array
            ? texture_list->elements.size() : 0;
    if (texture_count > max_textures) {
        error = "the upload exceeds " + std::to_string(max_textures) + " textures";
        return false;
    }

    for (const auto& entry : mesh_list->elements) {
        if (entry.type != llsd::Value::Type::binary || entry.binary.empty()) {
            error = "a mesh_list entry carries no bytes";
            return false;
        }
        if (upload_stage && !slmesh::parse(entry.binary)) {
            error = "a mesh payload is not a readable SL mesh asset";
            return false;
        }
        out.meshes.push_back(entry.binary);
    }
    for (std::size_t index = 0; index < texture_count; ++index) {
        const auto& entry = texture_list->elements[index];
        if (entry.type != llsd::Value::Type::binary) {
            error = "a texture_list entry is not binary";
            return false;
        }
        if (!entry.binary.empty()) {
            if (entry.binary.size() > mesh::max_image_bytes) {
                error = "a texture exceeds the published image size cap";
                return false;
            }
            if (upload_stage && !looks_like_j2c(entry.binary)) {
                error = "a texture is not JPEG2000";
                return false;
            }
        }
        out.textures.push_back(entry.binary);
    }

    for (const auto& entry : instance_list->elements) {
        if (entry.type != llsd::Value::Type::map) {
            error = "an instance_list entry is not a map";
            return false;
        }
        Instance instance;
        if (!read_floats(entry.find("position"), instance.position) ||
            !read_floats(entry.find("rotation"), instance.rotation) ||
            !read_floats(entry.find("scale"), instance.scale)) {
            error = "an instance transform is missing or malformed";
            return false;
        }
        const auto* mesh = entry.find("mesh");
        instance.mesh = mesh == nullptr ? -1 : static_cast<int>(mesh->as_integer(-1));
        if (instance.mesh < 0 ||
            static_cast<std::size_t>(instance.mesh) >= out.meshes.size()) {
            error = "an instance names a mesh the upload does not carry";
            return false;
        }
        instance.name = llsd_text(entry, "mesh_name");
        if (instance.name.size() > 255) instance.name.resize(255);
        if (const auto* material = entry.find("material"))
            instance.material = static_cast<std::uint8_t>(
                std::clamp<std::int64_t>(material->as_integer(3), 0, 255));
        if (const auto* shape = entry.find("physics_shape_type"))
            instance.physics_shape_type = static_cast<std::uint8_t>(
                std::clamp<std::int64_t>(shape->as_integer(2), 0, 2));
        if (const auto* faces = entry.find("face_list");
            faces != nullptr && faces->type == llsd::Value::Type::array) {
            if (faces->elements.size() > max_faces) {
                error = "an instance exceeds " + std::to_string(max_faces) + " faces";
                return false;
            }
            for (const auto& face_entry : faces->elements) {
                Face face;
                if (face_entry.type == llsd::Value::Type::map) {
                    if (const auto* image = face_entry.find("image")) {
                        const auto index = image->as_integer(-1);
                        if (index < 0 || static_cast<std::size_t>(index) >= texture_count) {
                            error = "a face names a texture the upload does not carry";
                            return false;
                        }
                        face.image = static_cast<int>(index);
                    }
                    float color[4];
                    if (read_floats(face_entry.find("diffuse_color"), color))
                        for (std::size_t channel = 0; channel < 4; ++channel)
                            face.diffuse_color[channel] = std::clamp(color[channel], 0.0f, 1.0f);
                }
                instance.faces.push_back(face);
            }
        }
        out.instances.push_back(std::move(instance));
    }
    return true;
}

// The TextureEntry per-face exception bitfield: seven bits per byte, most
// significant group first, high bit marking continuation.
void append_face_bitfield(std::vector<std::byte>& out, std::uint32_t mask) {
    std::array<std::uint8_t, 5> groups{};
    std::size_t count = 0;
    do {
        groups[count++] = static_cast<std::uint8_t>(mask & 0x7fu);
        mask >>= 7;
    } while (mask != 0);
    for (std::size_t index = count; index > 1; --index)
        out.push_back(static_cast<std::byte>(groups[index - 1] | 0x80u));
    out.push_back(static_cast<std::byte>(groups[0]));
}

void append_f32(std::vector<std::byte>& out, float value) {
    std::array<std::byte, sizeof value> raw{};
    std::memcpy(raw.data(), &value, sizeof value);
    out.insert(out.end(), raw.begin(), raw.end());
}

} // namespace

FeeParse parse_fee_request(std::string_view xml) {
    FeeParse result;
    const auto document = llsd::parse_xml(xml);
    if (!document || document->type != llsd::Value::Type::map) {
        result.error = "the fee request is not an LLSD map";
        return result;
    }
    if (llsd_text(*document, "asset_type") != "mesh") {
        result.error = "the fee request is not a mesh upload";
        return result;
    }
    result.mesh_request = true;
    auto& metadata = result.request.metadata;
    metadata.folder_id = llsd_text(*document, "folder_id");
    metadata.texture_folder_id = llsd_text(*document, "texture_folder_id");
    metadata.name = llsd_text(*document, "name");
    metadata.description = llsd_text(*document, "description");
    if (!viewer::parse_uuid(metadata.folder_id)) {
        result.error = "the fee request names no destination folder";
        return result;
    }
    if (!viewer::parse_uuid(metadata.texture_folder_id))
        metadata.texture_folder_id = metadata.folder_id;
    if (metadata.name.empty()) metadata.name = "mesh model";
    if (metadata.name.size() > 255) metadata.name.resize(255);
    if (metadata.description.size() > 1024) metadata.description.resize(1024);
    if (const auto* mask = document->find("everyone_mask"))
        metadata.everyone_permissions =
            static_cast<std::uint32_t>(mask->as_integer());
    if (const auto* mask = document->find("group_mask"))
        metadata.group_permissions = static_cast<std::uint32_t>(mask->as_integer());
    if (const auto* mask = document->find("next_owner_mask"))
        metadata.next_permissions = static_cast<std::uint32_t>(mask->as_integer());
    const auto* resources = document->find("asset_resources");
    if (resources == nullptr || resources->type != llsd::Value::Type::map) {
        result.error = "the fee request carries no asset_resources";
        return result;
    }
    if (!read_resources(*resources, result.request.resources, false, result.error))
        return result;
    result.ok = true;
    return result;
}

UploadParse parse_upload(std::string_view xml) {
    UploadParse result;
    const auto document = llsd::parse_xml(xml);
    if (!document || document->type != llsd::Value::Type::map) {
        result.error = "the upload is not an LLSD map";
        return result;
    }
    if (!read_resources(*document, result.resources, true, result.error))
        return result;
    result.ok = true;
    return result;
}

std::vector<std::byte> instance_texture_entry(
    const viewer::Uuid& fallback_texture, std::span<const Face> faces,
    std::span<const std::optional<viewer::Uuid>> textures) {
    const auto face_texture = [&](const Face& face) -> viewer::Uuid {
        if (face.image >= 0 && static_cast<std::size_t>(face.image) < textures.size() &&
            textures[static_cast<std::size_t>(face.image)])
            return *textures[static_cast<std::size_t>(face.image)];
        return fallback_texture;
    };

    std::vector<std::byte> output;
    // Textures: the fallback as the default, then one exception per distinct
    // uploaded texture covering every face that uses it.
    output.insert(output.end(), fallback_texture.begin(), fallback_texture.end());
    std::vector<std::pair<viewer::Uuid, std::uint32_t>> exceptions;
    for (std::size_t index = 0; index < faces.size() && index < max_faces; ++index) {
        const auto texture = face_texture(faces[index]);
        if (texture == fallback_texture) continue;
        const auto found = std::find_if(exceptions.begin(), exceptions.end(),
            [&](const auto& exception) { return exception.first == texture; });
        if (found == exceptions.end())
            exceptions.emplace_back(texture, 1u << index);
        else
            found->second |= 1u << index;
    }
    for (const auto& [texture, mask] : exceptions) {
        append_face_bitfield(output, mask);
        output.insert(output.end(), texture.begin(), texture.end());
    }
    output.push_back(std::byte{});

    // Colors, stored inverted: white default, per-face exceptions where the
    // upload's diffuse color differs.
    const auto inverted = [](const std::array<float, 4>& color) {
        std::array<std::byte, 4> raw{};
        for (std::size_t channel = 0; channel < 4; ++channel)
            raw[channel] = static_cast<std::byte>(
                255 - static_cast<int>(std::lround(color[channel] * 255.0f)));
        return raw;
    };
    output.insert(output.end(), 4, std::byte{});
    std::vector<std::pair<std::array<std::byte, 4>, std::uint32_t>> color_exceptions;
    for (std::size_t index = 0; index < faces.size() && index < max_faces; ++index) {
        const auto color = inverted(faces[index].diffuse_color);
        if (color == std::array<std::byte, 4>{}) continue;
        const auto found = std::find_if(color_exceptions.begin(), color_exceptions.end(),
            [&](const auto& exception) { return exception.first == color; });
        if (found == color_exceptions.end())
            color_exceptions.emplace_back(color, 1u << index);
        else
            found->second |= 1u << index;
    }
    for (const auto& [color, mask] : color_exceptions) {
        append_face_bitfield(output, mask);
        output.insert(output.end(), color.begin(), color.end());
    }
    output.push_back(std::byte{});

    // The remaining sections keep the canonical defaults of
    // default_texture_entry: unit repeats, zero offsets and rotation, no
    // bump, no media, no glow, no render material.
    append_f32(output, 1.0f);
    output.push_back(std::byte{});
    append_f32(output, 1.0f);
    output.push_back(std::byte{});
    for (int field = 0; field < 3; ++field) {
        output.insert(output.end(), 2, std::byte{});
        output.push_back(std::byte{});
    }
    for (int field = 0; field < 3; ++field) {
        output.push_back(std::byte{});
        output.push_back(std::byte{});
    }
    output.insert(output.end(), 16, std::byte{});
    return output;
}

std::array<float, 4> quaternion_conjugate(const std::array<float, 4>& value) {
    return {-value[0], -value[1], -value[2], value[3]};
}

std::array<float, 4> quaternion_multiply(
    const std::array<float, 4>& left, const std::array<float, 4>& right) {
    return {
        left[3] * right[0] + left[0] * right[3] + left[1] * right[2] - left[2] * right[1],
        left[3] * right[1] - left[0] * right[2] + left[1] * right[3] + left[2] * right[0],
        left[3] * right[2] + left[0] * right[1] - left[1] * right[0] + left[2] * right[3],
        left[3] * right[3] - left[0] * right[0] - left[1] * right[1] - left[2] * right[2]};
}

std::array<float, 3> quaternion_rotate(
    const std::array<float, 4>& rotation, const std::array<float, 3>& value) {
    const std::array<float, 4> pure{value[0], value[1], value[2], 0.0f};
    const auto rotated = quaternion_multiply(
        quaternion_multiply(rotation, pure), quaternion_conjugate(rotation));
    return {rotated[0], rotated[1], rotated[2]};
}

scene::Vector3 packed_rotation(const std::array<float, 4>& value) {
    auto normalized = value;
    const auto length = std::sqrt(
        normalized[0] * normalized[0] + normalized[1] * normalized[1] +
        normalized[2] * normalized[2] + normalized[3] * normalized[3]);
    if (length > 0.0001f)
        for (auto& component : normalized) component /= length;
    else
        normalized = {0.0f, 0.0f, 0.0f, 1.0f};
    if (normalized[3] < 0.0f)
        for (auto& component : normalized) component = -component;
    return {normalized[0], normalized[1], normalized[2]};
}

std::string fee_response_xml(std::string_view uploader) {
    // Zero is this grid's real price, not a stub; the cost breakdown keys are
    // the ones the viewer's fee observer reads.
    return "<?xml version=\"1.0\"?><llsd><map><key>state</key><string>upload</string>"
           "<key>uploader</key><uri>" + std::string(uploader) + "</uri>"
           "<key>upload_price</key><integer>0</integer>"
           "<key>data</key><map>"
           "<key>resource_cost</key><integer>0</integer>"
           "<key>model_streaming_cost</key><integer>0</integer>"
           "<key>simulation_cost</key><integer>0</integer>"
           "<key>physics_cost</key><integer>0</integer>"
           "</map></map></llsd>";
}

std::string error_response_xml(std::string_view message) {
    std::string escaped;
    escaped.reserve(message.size());
    for (const char character : message) {
        switch (character) {
        case '<': escaped += "&lt;"; break;
        case '>': escaped += "&gt;"; break;
        case '&': escaped += "&amp;"; break;
        default: escaped.push_back(character);
        }
    }
    return "<?xml version=\"1.0\"?><llsd><map><key>state</key><string>error</string>"
           "<key>error</key><map>"
           "<key>identifier</key><string>MeshUploadRefused</string>"
           "<key>message</key><string>" + escaped + "</string>"
           "</map></map></llsd>";
}

} // namespace homeworldz::mesh_model
