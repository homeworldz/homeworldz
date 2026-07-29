#include "homeworldz/mesh_model_upload.h"

#include "homeworldz/object_asset.h"
#include "homeworldz/slmesh.h"
#include "homeworldz/viewer_protocol.h"

#include <cmath>
#include <string>
#include <vector>

namespace {

std::string base64(std::span<const std::byte> content) {
    static constexpr char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    std::size_t index = 0;
    while (index + 2 < content.size()) {
        const auto a = static_cast<unsigned char>(content[index]);
        const auto b = static_cast<unsigned char>(content[index + 1]);
        const auto c = static_cast<unsigned char>(content[index + 2]);
        out.push_back(alphabet[a >> 2]);
        out.push_back(alphabet[((a & 0x3) << 4) | (b >> 4)]);
        out.push_back(alphabet[((b & 0xf) << 2) | (c >> 6)]);
        out.push_back(alphabet[c & 0x3f]);
        index += 3;
    }
    if (index + 1 == content.size()) {
        const auto a = static_cast<unsigned char>(content[index]);
        out.push_back(alphabet[a >> 2]);
        out.push_back(alphabet[(a & 0x3) << 4]);
        out += "==";
    } else if (index + 2 == content.size()) {
        const auto a = static_cast<unsigned char>(content[index]);
        const auto b = static_cast<unsigned char>(content[index + 1]);
        out.push_back(alphabet[a >> 2]);
        out.push_back(alphabet[((a & 0x3) << 4) | (b >> 4)]);
        out.push_back(alphabet[(b & 0xf) << 2]);
        out.push_back('=');
    }
    return out;
}

// A minimal but complete type-49 payload: one triangle at every level.
std::vector<std::byte> tiny_slmesh() {
    homeworldz::slmesh::Mesh mesh;
    homeworldz::slmesh::Submesh face;
    face.positions = {{-0.5f, -0.5f, 0.0f}, {0.5f, -0.5f, 0.0f}, {0.0f, 0.5f, 0.0f}};
    face.normals = {{0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}};
    face.texcoords = {{0.0f, 0.0f}, {1.0f, 0.0f}, {0.5f, 1.0f}};
    face.indices = {0, 1, 2};
    mesh.high = {face};
    mesh.medium = {face};
    mesh.low = {face};
    mesh.lowest = {face};
    mesh.physics_hull = {
        {-0.5f, -0.5f, -0.5f}, {0.5f, -0.5f, -0.5f}, {-0.5f, 0.5f, -0.5f},
        {0.5f, 0.5f, -0.5f}, {-0.5f, -0.5f, 0.5f}, {0.5f, -0.5f, 0.5f},
        {-0.5f, 0.5f, 0.5f}, {0.5f, 0.5f, 0.5f}};
    return homeworldz::slmesh::serialize(mesh);
}

std::string real_triplet(float x, float y, float z) {
    return "<array><real>" + std::to_string(x) + "</real><real>" + std::to_string(y) +
           "</real><real>" + std::to_string(z) + "</real></array>";
}

std::string quaternion(float x, float y, float z, float w) {
    return "<array><real>" + std::to_string(x) + "</real><real>" + std::to_string(y) +
           "</real><real>" + std::to_string(z) + "</real><real>" + std::to_string(w) +
           "</real></array>";
}

std::string resources_xml(const std::string& mesh_base64, const std::string& texture_base64) {
    return "<key>mesh_list</key><array><binary>" + mesh_base64 + "</binary></array>"
           "<key>texture_list</key><array><binary>" + texture_base64 + "</binary></array>"
           "<key>instance_list</key><array>"
           "<map>"
           "<key>position</key>" + real_triplet(10.0f, 20.0f, 30.0f) +
           "<key>rotation</key>" + quaternion(0.0f, 0.0f, 0.0f, 1.0f) +
           "<key>scale</key>" + real_triplet(2.0f, 2.0f, 2.0f) +
           "<key>material</key><integer>3</integer>"
           "<key>physics_shape_type</key><integer>2</integer>"
           "<key>mesh</key><integer>0</integer>"
           "<key>mesh_name</key><string>part one</string>"
           "<key>face_list</key><array><map>"
           "<key>image</key><integer>0</integer>"
           "<key>diffuse_color</key><array><real>1</real><real>1</real><real>1</real><real>1</real></array>"
           "<key>fullbright</key><boolean>false</boolean>"
           "</map></array>"
           "</map>"
           "<map>"
           "<key>position</key>" + real_triplet(11.0f, 20.0f, 30.0f) +
           "<key>rotation</key>" + quaternion(0.0f, 0.0f, 0.7071068f, 0.7071068f) +
           "<key>scale</key>" + real_triplet(1.0f, 1.0f, 1.0f) +
           "<key>mesh</key><integer>0</integer>"
           "<key>face_list</key><array><map>"
           "<key>diffuse_color</key><array><real>1</real><real>0</real><real>0</real><real>1</real></array>"
           "</map></array>"
           "</map>"
           "</array>"
           "<key>metric</key><string>MUT_Unspecified</string>";
}

} // namespace

int main() {
    const auto mesh_bytes = tiny_slmesh();
    const auto mesh_base64 = base64(mesh_bytes);

    // The fee request: full metadata, resources whose texture bytes are empty
    // — exactly what the viewer sends.
    const std::string fee_xml =
        "<?xml version=\"1.0\"?><llsd><map>"
        "<key>folder_id</key><uuid>0d842f5e-3a5e-4353-adb7-e571b4a19626</uuid>"
        "<key>texture_folder_id</key><uuid>1b8f2b19-1252-4dd5-9268-3a1a4a4a5f57</uuid>"
        "<key>asset_type</key><string>mesh</string>"
        "<key>inventory_type</key><string>object</string>"
        "<key>name</key><string>Test Model</string>"
        "<key>description</key><string>(No Description)</string>"
        "<key>next_owner_mask</key><integer>581632</integer>"
        "<key>group_mask</key><integer>0</integer>"
        "<key>everyone_mask</key><integer>0</integer>"
        "<key>asset_resources</key><map>" + resources_xml(mesh_base64, "") + "</map>"
        "</map></llsd>";
    const auto fee = homeworldz::mesh_model::parse_fee_request(fee_xml);
    if (!fee.mesh_request) return 1;
    if (!fee.ok) return 2;
    if (fee.request.metadata.name != "Test Model" ||
        fee.request.metadata.folder_id != "0d842f5e-3a5e-4353-adb7-e571b4a19626" ||
        fee.request.metadata.texture_folder_id != "1b8f2b19-1252-4dd5-9268-3a1a4a4a5f57" ||
        fee.request.metadata.next_permissions != 581632u) return 3;
    if (fee.request.resources.meshes.size() != 1 ||
        fee.request.resources.instances.size() != 2 ||
        fee.request.resources.textures.size() != 1 ||
        !fee.request.resources.textures[0].empty()) return 4;
    const auto& second = fee.request.resources.instances[1];
    if (second.mesh != 0 || std::fabs(second.position[0] - 11.0f) > 0.001f ||
        std::fabs(second.rotation[2] - 0.7071068f) > 0.001f) return 5;
    if (fee.request.resources.instances[0].faces.size() != 1 ||
        fee.request.resources.instances[0].faces[0].image != 0 ||
        second.faces[0].image != -1 ||
        std::fabs(second.faces[0].diffuse_color[1]) > 0.001f) return 6;

    // A plain texture upload is not a mesh request; a mesh request without a
    // folder is one, refused.
    const auto texture_fee = homeworldz::mesh_model::parse_fee_request(
        "<llsd><map><key>asset_type</key><string>texture</string></map></llsd>");
    if (texture_fee.mesh_request || texture_fee.ok) return 7;
    const auto folderless = homeworldz::mesh_model::parse_fee_request(
        "<llsd><map><key>asset_type</key><string>mesh</string>"
        "<key>name</key><string>x</string></map></llsd>");
    if (!folderless.mesh_request || folderless.ok || folderless.error.empty()) return 8;

    // The upload body: asset_resources alone, textures now carrying bytes.
    // A JPEG2000 codestream signature satisfies the sniff.
    const std::vector<std::byte> j2c{std::byte{0xff}, std::byte{0x4f}, std::byte{0xff},
                                     std::byte{0x51}, std::byte{0x00}, std::byte{0x00}};
    const std::string upload_xml =
        "<?xml version=\"1.0\"?><llsd><map>" +
        resources_xml(mesh_base64, base64(j2c)) + "</map></llsd>";
    const auto upload = homeworldz::mesh_model::parse_upload(upload_xml);
    if (!upload.ok) return 9;
    if (upload.resources.meshes.size() != 1 || upload.resources.meshes[0] != mesh_bytes ||
        upload.resources.textures.size() != 1 || upload.resources.textures[0] != j2c) return 10;

    // Refusals: a mesh that is not type-49, and a texture that is not J2C.
    const std::vector<std::byte> garbage{std::byte{'g'}, std::byte{'l'}, std::byte{'T'},
                                         std::byte{'F'}};
    if (homeworldz::mesh_model::parse_upload(
            "<llsd><map>" + resources_xml(base64(garbage), "") + "</map></llsd>").ok) return 11;
    if (homeworldz::mesh_model::parse_upload(
            "<llsd><map>" + resources_xml(mesh_base64, base64(garbage)) + "</map></llsd>").ok)
        return 12;

    // The TextureEntry an instance gets: face 0 draws the uploaded texture,
    // the default stays the fallback — read back through the same walker the
    // asset closure uses.
    const auto fallback = homeworldz::viewer::parse_uuid(
        "89556747-24cb-43ed-920b-47caed15465f").value();
    const auto uploaded = homeworldz::viewer::parse_uuid(
        "abababab-abab-abab-abab-abababababab").value();
    const std::vector<std::optional<homeworldz::viewer::Uuid>> textures{uploaded};
    const std::vector<homeworldz::mesh_model::Face> faces{
        {0, {1.0f, 1.0f, 1.0f, 1.0f}}, {-1, {1.0f, 0.0f, 0.0f, 1.0f}}};
    const auto entry = homeworldz::mesh_model::instance_texture_entry(
        fallback, faces, textures);
    const auto ids = homeworldz::asset::texture_entry_texture_ids(entry);
    if (ids.size() != 2 || ids[0] != "89556747-24cb-43ed-920b-47caed15465f" ||
        ids[1] != "abababab-abab-abab-abab-abababababab") return 13;

    // Quaternion helpers: a quarter turn about z carries x onto y; a negative
    // w packs to the same rotation with the sign folded away.
    const std::array<float, 4> quarter{0.0f, 0.0f, 0.7071068f, 0.7071068f};
    const auto rotated = homeworldz::mesh_model::quaternion_rotate(quarter, {1.0f, 0.0f, 0.0f});
    if (std::fabs(rotated[0]) > 0.001f || std::fabs(rotated[1] - 1.0f) > 0.001f) return 14;
    const auto packed = homeworldz::mesh_model::packed_rotation(
        {0.0f, 0.0f, -0.7071068f, -0.7071068f});
    if (std::fabs(packed.z - 0.7071068) > 0.001) return 15;
    return 0;
}
