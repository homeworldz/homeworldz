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

class StaticSceneMirror {
public:
    explicit StaticSceneMirror(World& world) : world_(world) {}

    bool synchronize(const scene::Entity& entity);
    void synchronize(const scene::Scene& scene);
    bool remove(scene::EntityId entity_id);
    BodyId body_id(scene::EntityId entity_id) const;
    std::size_t size() const { return bodies_.size(); }

private:
    World& world_;
    std::unordered_map<scene::EntityId, BodyId> bodies_;
};

} // namespace homeworldz::physics
