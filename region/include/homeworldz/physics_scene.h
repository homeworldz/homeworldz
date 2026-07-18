#pragma once

#include "homeworldz/physics.h"
#include "homeworldz/scene.h"

#include <cstddef>
#include <unordered_map>

namespace homeworldz::physics {

struct MaterialProperties {
    double density;
    double friction;
    double restitution;
};

// Second Life-compatible defaults for PRIM_MATERIAL_* values 0x00-0x07.
MaterialProperties material_properties(std::uint8_t material);
double box_mass(scene::Vector3 scale, double density);
double ellipsoid_mass(scene::Vector3 scale, double density);
double cylinder_mass(scene::Vector3 scale, double density);
double prism_mass(scene::Vector3 scale, double density);
double pyramid_mass(scene::Vector3 scale, double density);
double entity_mass(const scene::Entity& entity);
scene::Vector3 rotate_vector(scene::Vector3 value, const std::array<double, 4>& rotation);
scene::Vector3 rotated_box_half_extents(
    scene::Vector3 scale, const std::array<double, 4>& rotation);
bool contain_body_without_neighbors(BodyState& state, double region_extent = 256.0);
bool within_viewer_interest(scene::Vector3 observer, scene::Vector3 subject,
                            double draw_distance, double subject_radius = 0.0);
bool body_transform_changed(const BodyState& previous, const BodyState& current,
                            double position_epsilon = 0.01,
                            double velocity_epsilon = 0.01,
                            double rotation_epsilon = 0.001);

class StaticSceneMirror {
public:
    explicit StaticSceneMirror(World& world) : world_(world) {}

    bool synchronize(
        const scene::Entity& entity,
        std::optional<MotionType> linked_motion = std::nullopt);
    void synchronize(const scene::Scene& scene);
    bool remove(scene::EntityId entity_id);
    BodyId body_id(scene::EntityId entity_id) const;
    std::size_t size() const { return bodies_.size(); }

private:
    World& world_;
    std::unordered_map<scene::EntityId, BodyId> bodies_;
};

} // namespace homeworldz::physics
