#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace homeworldz::scene {

using EntityId = std::uint64_t;

constexpr std::uint32_t permission_transfer = 0x00002000;
constexpr std::uint32_t permission_modify = 0x00004000;
constexpr std::uint32_t permission_copy = 0x00008000;
constexpr std::uint32_t permission_export = 0x00010000;
constexpr std::uint32_t permission_move = 0x00080000;
constexpr std::uint32_t permission_all = permission_transfer | permission_modify |
    permission_copy | permission_move;
constexpr std::uint32_t permission_creator = permission_all | permission_export;

constexpr std::uint8_t permission_field_base = 0x01;
constexpr std::uint8_t permission_field_owner = 0x02;
constexpr std::uint8_t permission_field_group = 0x04;
constexpr std::uint8_t permission_field_everyone = 0x08;
constexpr std::uint8_t permission_field_next_owner = 0x10;

struct Vector3 {
    double x{};
    double y{};
    double z{};
};

struct Entity {
    EntityId id{};
    std::string name;
    Vector3 position;
    Vector3 velocity;
    std::string object_id;
    std::string owner_id;
    Vector3 scale{1.0, 1.0, 1.0};
    std::uint8_t material{3};
    std::string creator_id;
    std::uint32_t base_permissions{permission_creator};
    std::uint32_t owner_permissions{permission_creator};
    std::uint32_t group_permissions{};
    std::uint32_t everyone_permissions{};
    std::uint32_t next_owner_permissions{permission_all};
    std::uint64_t creation_date{};
    Vector3 rotation;
    std::string description;
    std::vector<std::byte> texture_entry;
    bool avatar_flying{};
    bool physical{};
    bool phantom{};
    std::uint8_t physics_shape_type{};
    double physics_density{1000.0};
    double physics_friction{0.6};
    double physics_restitution{0.5};
    double physics_gravity_multiplier{1.0};
    std::uint8_t path_curve{0x10};
    std::uint8_t profile_curve{0x01};
    std::uint16_t path_begin{};
    std::uint16_t path_end{};
    std::uint8_t path_scale_x{100};
    std::uint8_t path_scale_y{100};
    std::uint8_t path_shear_x{};
    std::uint8_t path_shear_y{};
    std::uint8_t path_twist{};
    std::uint8_t path_twist_begin{};
    std::uint8_t path_radius_offset{};
    std::uint8_t path_taper_x{};
    std::uint8_t path_taper_y{};
    std::uint8_t path_revolutions{};
    std::uint8_t path_skew{};
    std::uint16_t profile_begin{};
    std::uint16_t profile_end{};
    std::uint16_t profile_hollow{};
    EntityId parent_id{};
    Vector3 local_position;
    Vector3 local_rotation;
};

struct RayIntersection {
    Vector3 position;
    Vector3 normal;
};

bool apply_permission_update(
    Entity& entity, std::string_view agent_id, std::uint8_t field, bool set,
    std::uint32_t mask);

std::optional<RayIntersection> intersect_box(
    Vector3 ray_start, Vector3 ray_end, Vector3 center, Vector3 scale);

void establish_link(Entity& child, const Entity& root);
void update_linked_world_transform(Entity& child, const Entity& root);
void scale_linked_child(Entity& child, Vector3 factors);

class Scene {
public:
    EntityId create(std::string name, Vector3 position = {}, Vector3 velocity = {});
    bool remove(EntityId id);
    Entity* find(EntityId id);
    const Entity* find(EntityId id) const;
    void step(double seconds);
    void restore(std::uint64_t revision, std::vector<Entity> entities);

    std::size_t size() const { return entities_.size(); }
    std::uint64_t revision() const { return revision_; }
    std::uint64_t simulation_steps() const { return simulation_steps_; }
    const std::unordered_map<EntityId, Entity>& entities() const { return entities_; }

private:
    EntityId next_id_{1};
    std::uint64_t revision_{};
    std::uint64_t simulation_steps_{};
    std::unordered_map<EntityId, Entity> entities_;
};

} // namespace homeworldz::scene
