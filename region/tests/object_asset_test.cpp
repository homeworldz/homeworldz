#include "homeworldz/object_asset.h"

#include <array>
#include <string>

int main() {
    const std::string json = R"({"format":"homeworldz-object-v1","creatorId":"10000000-0000-4000-8000-000000000001","name":"Prism","scale":[0.500000,0.500000,0.500000],"rotation":[0.000000,0.000000,0.382683],"description":"Round \"prim\"","material":3,"physicsShapeType":2,"physicsDensity":125.000000,"physicsFriction":0.700000,"physicsRestitution":0.250000,"physicsGravityMultiplier":1.500000,"textureEntry":"aabbcc","pathCurve":16,"profileCurve":1,"pathBegin":0,"pathEnd":0,"pathScaleX":200,"pathScaleY":100,"pathShearX":206,"pathShearY":0,"pathTwist":0,"pathTwistBegin":0,"pathRadiusOffset":0,"pathTaperX":0,"pathTaperY":0,"pathRevolutions":0,"pathSkew":0,"profileBegin":0,"profileEnd":0,"profileHollow":0,"physical":true,"phantom":false,"basePermissions":647168})";
    const auto bytes = std::span(reinterpret_cast<const std::byte*>(json.data()), json.size());
    const auto asset = homeworldz::asset::parse_object_asset(bytes);
    if (!asset || asset->scale.x != 0.5 || asset->rotation.z != 0.382683 ||
        asset->material != 3 || asset->physics_shape_type != 2 ||
        asset->physics_density != 125.0 || asset->physics_friction != 0.7 ||
        asset->physics_restitution != 0.25 || asset->physics_gravity_multiplier != 1.5 ||
        asset->texture_entry != std::vector<std::byte>{std::byte{0xaa}, std::byte{0xbb}, std::byte{0xcc}} ||
        asset->path_curve != 0x10 || asset->profile_curve != 0x01 || asset->path_scale_x != 200 ||
        asset->path_scale_y != 100 || asset->path_shear_x != 0xce || !asset->physical || asset->phantom ||
        asset->description != "Round \"prim\"")
        return 1;
    auto no_texture_json = json;
    const std::string texture_text = "\"textureEntry\":\"aabbcc\",";
    const auto texture_field = no_texture_json.find(texture_text);
    if (texture_field == std::string::npos) return 1;
    no_texture_json.erase(texture_field, texture_text.size());
    const auto no_texture = homeworldz::asset::parse_object_asset(std::span(
        reinterpret_cast<const std::byte*>(no_texture_json.data()), no_texture_json.size()));
    if (!no_texture || !no_texture->texture_entry.empty()) return 1;
    const std::string invalid = R"({"format":"homeworldz-object-v1","scale":[0,1,1],"rotation":[0,0,0],"description":"","material":3})";
    if (homeworldz::asset::parse_object_asset(
            std::span(reinterpret_cast<const std::byte*>(invalid.data()), invalid.size())))
        return 1;
    homeworldz::scene::Entity root;
    root.id = 10;
    root.name = "Root Prim";
    root.creator_id = "10000000-0000-4000-8000-000000000001";
    root.scale = {1.0, 2.0, 3.0};
    root.rotation = {0.0, 0.0, 0.25};
    root.owner_permissions = 0x0008e000;
    homeworldz::scene::Entity child;
    child.id = 11;
    child.parent_id = root.id;
    child.name = "Child Prim";
    child.creator_id = "20000000-0000-4000-8000-000000000002";
    child.scale = {0.5, 0.75, 1.0};
    child.local_position = {2.0, -3.0, 4.0};
    child.local_rotation = {0.125, 0.0, -0.25};
    child.next_owner_permissions = 0x00082000;
    const std::array<const homeworldz::scene::Entity*, 1> children{&child};
    const auto linkset_text = homeworldz::asset::serialize_linkset_asset(root, children);
    const auto linkset = homeworldz::asset::parse_linkset_asset(std::span(
        reinterpret_cast<const std::byte*>(linkset_text.data()), linkset_text.size()));
    if (!linkset || linkset->root.name != "Root Prim" || linkset->root.scale.y != 2.0 ||
        linkset->root.rotation.z != 0.25 || linkset->root.owner_permissions != 0x0008e000 ||
        linkset->children.size() != 1 || linkset->children[0].name != "Child Prim" ||
        linkset->children[0].creator_id != child.creator_id ||
        linkset->children[0].local_position.x != 2.0 ||
        linkset->children[0].local_position.y != -3.0 ||
        linkset->children[0].local_rotation.z != -0.25 ||
        linkset->children[0].next_owner_permissions != 0x00082000)
        return 1;
    const auto single = homeworldz::asset::parse_linkset_asset(bytes);
    if (!single || !single->children.empty() || single->root.description != "Round \"prim\"")
        return 1;
    return 0;
}
