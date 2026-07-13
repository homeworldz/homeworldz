#include "homeworldz/scene.h"

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

} // namespace homeworldz::scene
