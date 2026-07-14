#pragma once

#include <cstdint>
#include <optional>
#include <string>
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
};

struct RayIntersection {
    Vector3 position;
    Vector3 normal;
};

std::optional<RayIntersection> intersect_box(
    Vector3 ray_start, Vector3 ray_end, Vector3 center, Vector3 scale);

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
