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

double pyramid_mass(scene::Vector3 scale, double density) {
    constexpr double minimum_mass = 0.001;
    constexpr double maximum_mass = 100000.0;
    constexpr double volume_fraction = 1.0 / 3.0;
    const auto volume = volume_fraction * std::max(0.0, scale.x) *
                        std::max(0.0, scale.y) * std::max(0.0, scale.z);
    return std::clamp(volume * density, minimum_mass, maximum_mass);
}

double entity_mass(const scene::Entity& entity) {
    const auto density = std::isfinite(entity.physics_density)
        ? std::clamp(entity.physics_density, 1.0, 22587.0)
        : material_properties(entity.material).density;
    const bool sphere = entity.path_curve == 0x20 && (entity.profile_curve & 0x0f) == 0x05;
    const bool cylinder = entity.path_curve == 0x10 && (entity.profile_curve & 0x0f) == 0x00;
    const bool prism = entity.path_curve == 0x10 && (entity.profile_curve & 0x0f) == 0x01 &&
        entity.path_scale_x == 200 && entity.path_scale_y == 100 &&
        entity.path_shear_x == 0xce && entity.path_shear_y == 0;
    const bool pyramid = entity.path_curve == 0x10 && (entity.profile_curve & 0x0f) == 0x01 &&
        entity.path_scale_x == 200 && entity.path_scale_y == 200 &&
        entity.path_shear_x == 0 && entity.path_shear_y == 0;
    return sphere ? ellipsoid_mass(entity.scale, density) :
        (cylinder ? cylinder_mass(entity.scale, density) :
        (prism ? prism_mass(entity.scale, density) :
        (pyramid ? pyramid_mass(entity.scale, density) : box_mass(entity.scale, density))));
}

scene::Vector3 rotate_vector(scene::Vector3 value, const std::array<double, 4>& rotation) {
    const scene::Vector3 quaternion_vector{rotation[0], rotation[1], rotation[2]};
    const auto cross = [](scene::Vector3 first, scene::Vector3 second) {
        return scene::Vector3{
            first.y * second.z - first.z * second.y,
            first.z * second.x - first.x * second.z,
            first.x * second.y - first.y * second.x};
    };
    const auto first_cross = cross(quaternion_vector, value);
    const scene::Vector3 doubled{
        2.0 * first_cross.x, 2.0 * first_cross.y, 2.0 * first_cross.z};
    const auto second_cross = cross(quaternion_vector, doubled);
    return {
        value.x + rotation[3] * doubled.x + second_cross.x,
        value.y + rotation[3] * doubled.y + second_cross.y,
        value.z + rotation[3] * doubled.z + second_cross.z};
}

scene::Vector3 rotated_box_half_extents(
    scene::Vector3 scale, const std::array<double, 4>& rotation) {
    const scene::Vector3 x = rotate_vector({scale.x * 0.5, 0.0, 0.0}, rotation);
    const scene::Vector3 y = rotate_vector({0.0, scale.y * 0.5, 0.0}, rotation);
    const scene::Vector3 z = rotate_vector({0.0, 0.0, scale.z * 0.5}, rotation);
    return {std::abs(x.x) + std::abs(y.x) + std::abs(z.x),
            std::abs(x.y) + std::abs(y.y) + std::abs(z.y),
            std::abs(x.z) + std::abs(y.z) + std::abs(z.z)};
}

bool contain_body_without_neighbors(BodyState& state, double region_extent) {
    bool constrained{};
    if (state.position.x < 0.0) {
        state.position.x = 0.0;
        state.linear_velocity.x = std::max(0.0, state.linear_velocity.x);
        constrained = true;
    } else if (state.position.x > region_extent) {
        state.position.x = region_extent;
        state.linear_velocity.x = std::min(0.0, state.linear_velocity.x);
        constrained = true;
    }
    if (state.position.y < 0.0) {
        state.position.y = 0.0;
        state.linear_velocity.y = std::max(0.0, state.linear_velocity.y);
        constrained = true;
    } else if (state.position.y > region_extent) {
        state.position.y = region_extent;
        state.linear_velocity.y = std::min(0.0, state.linear_velocity.y);
        constrained = true;
    }
    return constrained;
}

bool StaticSceneMirror::synchronize(
    const scene::Entity& entity, std::optional<MotionType> linked_motion) {
    if (entity.object_id.empty() || (entity.parent_id != 0 && !linked_motion) || entity.phantom ||
        entity.physics_shape_type == 0x01) {
        static_cast<void>(remove(entity.id));
        return true;
    }
    BodyDefinition definition;
    definition.entity_id = entity.id;
    definition.motion = linked_motion.value_or(
        entity.physical ? MotionType::Dynamic : MotionType::Static);
    const bool sphere = entity.path_curve == 0x20 && (entity.profile_curve & 0x0f) == 0x05;
    const bool cylinder = entity.path_curve == 0x10 && (entity.profile_curve & 0x0f) == 0x00;
    const bool prism = entity.path_curve == 0x10 && (entity.profile_curve & 0x0f) == 0x01 &&
        entity.path_scale_x == 200 && entity.path_scale_y == 100 &&
        entity.path_shear_x == 0xce && entity.path_shear_y == 0;
    const bool pyramid = entity.path_curve == 0x10 && (entity.profile_curve & 0x0f) == 0x01 &&
        entity.path_scale_x == 200 && entity.path_scale_y == 200 &&
        entity.path_shear_x == 0 && entity.path_shear_y == 0;
    definition.shape.type = sphere ? ShapeType::Sphere :
        (cylinder ? ShapeType::Cylinder :
        ((prism || pyramid) ? ShapeType::ConvexHull : ShapeType::Box));
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
    if (pyramid) {
        const auto x = entity.scale.x * 0.5;
        const auto y = entity.scale.y * 0.5;
        const auto z = entity.scale.z * 0.5;
        definition.shape.hull_points = {
            {-x, -y, -z}, {-x, y, -z}, {x, -y, -z}, {x, y, -z}, {0.0, 0.0, z}};
    }
    definition.position = entity.position;
    definition.velocity = entity.velocity;
    const auto properties = material_properties(entity.material);
    definition.mass = entity_mass(entity);
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
        std::optional<MotionType> linked_motion;
        if (entity.parent_id != 0) {
            const auto* root = scene.find(entity.parent_id);
            if (!root || root->physical) continue;
            linked_motion = MotionType::Static;
        }
        if (entity.object_id.empty() || entity.phantom ||
            entity.physics_shape_type == 0x01)
            continue;
        present.insert(entity_id);
        synchronize(entity, linked_motion);
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
