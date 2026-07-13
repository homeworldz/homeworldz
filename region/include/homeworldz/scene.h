#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

namespace homeworldz::scene {

using EntityId = std::uint64_t;

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
};

class Scene {
public:
    EntityId create(std::string name, Vector3 position = {}, Vector3 velocity = {});
    bool remove(EntityId id);
    Entity* find(EntityId id);
    const Entity* find(EntityId id) const;
    void step(double seconds);

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
