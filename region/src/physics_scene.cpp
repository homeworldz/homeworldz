#include "homeworldz/physics_scene.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace homeworldz::physics {

MaterialProperties material_properties(std::uint8_t material) {
    // SL gives all legacy materials the same default density. Material selects
    // the contact behavior; Extra Physics / scripting may override density.
    constexpr double default_density = 1000.0;
    switch (material) {
    case 0x00: return {default_density, 0.8, 0.4}; // stone
    case 0x01: return {default_density, 0.3, 0.4}; // metal
    case 0x02: return {default_density, 0.2, 0.7}; // glass
    case 0x04: return {default_density, 0.9, 0.3}; // flesh
    case 0x05: return {default_density, 0.4, 0.7}; // plastic
    case 0x06: return {default_density, 0.9, 0.9}; // rubber
    case 0x07: return {default_density, 0.6, 0.5}; // deprecated light
    case 0x03:
    default:   return {default_density, 0.6, 0.5}; // wood
    }
}

double box_mass(scene::Vector3 scale, double density) {
    // Keep malformed or extreme viewer input from destabilizing the solver.
    // These are adapter limits, not an alternate region-side force model.
    constexpr double minimum_mass = 0.001;
    constexpr double maximum_mass = 100000.0;
    const auto volume = std::max(0.0, scale.x) * std::max(0.0, scale.y) *
                        std::max(0.0, scale.z);
    return std::clamp(volume * density, minimum_mass, maximum_mass);
}

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
    const auto properties = material_properties(entity.material);
    definition.mass = box_mass(entity.scale, properties.density);
    definition.friction = properties.friction;
    definition.restitution = properties.restitution;
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
