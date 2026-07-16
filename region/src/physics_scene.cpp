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

double ellipsoid_mass(scene::Vector3 scale, double density) {
    constexpr double minimum_mass = 0.001;
    constexpr double maximum_mass = 100000.0;
    constexpr double pi_over_six = 0.52359877559829887308;
    const auto volume = pi_over_six * std::max(0.0, scale.x) *
                        std::max(0.0, scale.y) * std::max(0.0, scale.z);
    return std::clamp(volume * density, minimum_mass, maximum_mass);
}

double cylinder_mass(scene::Vector3 scale, double density) {
    constexpr double minimum_mass = 0.001;
    constexpr double maximum_mass = 100000.0;
    constexpr double pi_over_four = 0.78539816339744830962;
    const auto volume = pi_over_four * std::max(0.0, scale.x) *
                        std::max(0.0, scale.y) * std::max(0.0, scale.z);
    return std::clamp(volume * density, minimum_mass, maximum_mass);
}

double prism_mass(scene::Vector3 scale, double density) {
    constexpr double minimum_mass = 0.001;
    constexpr double maximum_mass = 100000.0;
    // Firestorm's canonical prism is a square extrusion whose top X ratio is
    // collapsed to zero and sheared to one side: exactly half a box.
    constexpr double volume_fraction = 0.5;
    const auto volume = volume_fraction * std::max(0.0, scale.x) *
                        std::max(0.0, scale.y) * std::max(0.0, scale.z);
    return std::clamp(volume * density, minimum_mass, maximum_mass);
}

bool StaticSceneMirror::synchronize(const scene::Entity& entity) {
    if (entity.object_id.empty() || entity.phantom || entity.physics_shape_type == 0x01)
        return remove(entity.id);
    BodyDefinition definition;
    definition.entity_id = entity.id;
    definition.motion = entity.physical ? MotionType::Dynamic : MotionType::Static;
    const bool sphere = entity.path_curve == 0x20 && (entity.profile_curve & 0x0f) == 0x05;
    const bool cylinder = entity.path_curve == 0x10 && (entity.profile_curve & 0x0f) == 0x00;
    const bool prism = entity.path_curve == 0x10 && (entity.profile_curve & 0x0f) == 0x01 &&
        entity.path_scale_x == 200 && entity.path_scale_y == 100 &&
        entity.path_shear_x == 0xce && entity.path_shear_y == 0;
    definition.shape.type = sphere ? ShapeType::Sphere :
        (cylinder ? ShapeType::Cylinder :
        (prism ? ShapeType::ConvexHull : ShapeType::Box));
    definition.shape.half_extents = {
        entity.scale.x * 0.5, entity.scale.y * 0.5, entity.scale.z * 0.5};
    if (sphere)
        definition.shape.radius = std::min({entity.scale.x, entity.scale.y, entity.scale.z}) * 0.5;
    if (cylinder) {
        definition.shape.radius = std::min(entity.scale.x, entity.scale.y) * 0.5;
        definition.shape.height = entity.scale.z;
    }
    if (prism) {
        const auto x = entity.scale.x * 0.5;
        const auto y = entity.scale.y * 0.5;
        const auto z = entity.scale.z * 0.5;
        definition.shape.hull_points = {
            {-x, -y, -z}, {-x, y, -z}, {x, -y, -z}, {x, y, -z},
            {-x, -y, z}, {-x, y, z}};
    }
    definition.position = entity.position;
    definition.velocity = entity.velocity;
    const auto properties = material_properties(entity.material);
    const auto density = std::isfinite(entity.physics_density)
        ? std::clamp(entity.physics_density, 1.0, 22587.0)
        : properties.density;
    definition.mass = sphere ? ellipsoid_mass(entity.scale, density) :
        (cylinder ? cylinder_mass(entity.scale, density) :
        (prism ? prism_mass(entity.scale, density) : box_mass(entity.scale, density)));
    definition.friction = std::isfinite(entity.physics_friction)
        ? std::clamp(entity.physics_friction, 0.0, 255.0)
        : properties.friction;
    definition.restitution = std::isfinite(entity.physics_restitution)
        ? std::clamp(entity.physics_restitution, 0.0, 1.0)
        : properties.restitution;
    definition.gravity_multiplier = std::isfinite(entity.physics_gravity_multiplier)
        ? std::clamp(entity.physics_gravity_multiplier, -1.0, 28.0)
        : 1.0;
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
        if (entity.object_id.empty() || entity.phantom || entity.physics_shape_type == 0x01)
            continue;
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
