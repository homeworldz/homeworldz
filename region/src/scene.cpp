#include "homeworldz/scene.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace homeworldz::scene {

EntityId Scene::create(std::string name, Vector3 position, Vector3 velocity) {
    const auto id = next_id_++;
    entities_.emplace(id, Entity{id, std::move(name), position, velocity});
    ++revision_;
    return id;
}

bool Scene::remove(EntityId id) {
    if (entities_.erase(id) == 0) return false;
    ++revision_;
    return true;
}

Entity* Scene::find(EntityId id) {
    const auto found = entities_.find(id);
    return found == entities_.end() ? nullptr : &found->second;
}

const Entity* Scene::find(EntityId id) const {
    const auto found = entities_.find(id);
    return found == entities_.end() ? nullptr : &found->second;
}

void Scene::step(double seconds) {
    for (auto& [id, entity] : entities_) {
        static_cast<void>(id);
        entity.position.x += entity.velocity.x * seconds;
        entity.position.y += entity.velocity.y * seconds;
        entity.position.z += entity.velocity.z * seconds;
    }
    ++simulation_steps_;
    ++revision_;
}

void Scene::restore(std::uint64_t revision, std::vector<Entity> entities) {
    std::unordered_map<EntityId, Entity> restored;
    restored.reserve(entities.size());
    EntityId next_id = 1;
    for (auto& entity : entities) {
        if (entity.id == 0 || entity.id == std::numeric_limits<EntityId>::max()) {
            throw std::invalid_argument("restored entity ID is outside the supported range");
        }
        next_id = std::max(next_id, entity.id + 1);
        const auto [position, inserted] = restored.emplace(entity.id, std::move(entity));
        static_cast<void>(position);
        if (!inserted) throw std::invalid_argument("restored scene contains duplicate entity IDs");
    }
    entities_ = std::move(restored);
    next_id_ = next_id;
    revision_ = revision;
    simulation_steps_ = 0;
}

} // namespace homeworldz::scene
