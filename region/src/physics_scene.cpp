#include "homeworldz/physics_scene.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace homeworldz::physics {

bool StaticSceneMirror::synchronize(const scene::Entity& entity) {
    if (entity.object_id.empty() || entity.phantom) return remove(entity.id);
    BodyDefinition definition;
    definition.entity_id = entity.id;
    definition.motion = entity.physical ? MotionType::Dynamic : MotionType::Static;
    definition.shape.type = ShapeType::Box;
    definition.shape.half_extents = {
        entity.scale.x * 0.5, entity.scale.y * 0.5, entity.scale.z * 0.5};
    definition.position = entity.position;
    definition.velocity = entity.velocity;
    const auto squared = entity.rotation.x * entity.rotation.x +
                         entity.rotation.y * entity.rotation.y +
                         entity.rotation.z * entity.rotation.z;
    definition.rotation = {entity.rotation.x, entity.rotation.y, entity.rotation.z,
                           std::sqrt(std::max(0.0, 1.0 - squared))};
    const auto replacement = world_.create_body(definition);
    if (replacement == 0) return false;
    if (const auto found = bodies_.find(entity.id); found != bodies_.end())
        world_.remove_body(found->second);
    bodies_[entity.id] = replacement;
    return true;
}

void StaticSceneMirror::synchronize(const scene::Scene& scene) {
    std::unordered_set<scene::EntityId> present;
    for (const auto& [entity_id, entity] : scene.entities()) {
        if (entity.object_id.empty() || entity.phantom) continue;
        present.insert(entity_id);
        synchronize(entity);
    }
    for (auto iterator = bodies_.begin(); iterator != bodies_.end();) {
        if (present.contains(iterator->first)) {
            ++iterator;
            continue;
        }
        world_.remove_body(iterator->second);
        iterator = bodies_.erase(iterator);
    }
}

bool StaticSceneMirror::remove(scene::EntityId entity_id) {
    const auto found = bodies_.find(entity_id);
    if (found == bodies_.end()) return false;
    const auto removed = world_.remove_body(found->second);
    bodies_.erase(found);
    return removed;
}

BodyId StaticSceneMirror::body_id(scene::EntityId entity_id) const {
    const auto found = bodies_.find(entity_id);
    return found == bodies_.end() ? 0 : found->second;
}

} // namespace homeworldz::physics
