#include "homeworldz/physics_scene.h"

#include <cmath>
#include <unordered_map>

namespace {

class RecordingWorld final : public homeworldz::physics::World {
public:
    homeworldz::physics::BodyId create_body(
        const homeworldz::physics::BodyDefinition& definition) override {
        last_definition = definition;
        const auto id = next_id++;
        definitions.emplace(id, definition);
        return id;
    }
    bool remove_body(homeworldz::physics::BodyId id) override {
        return definitions.erase(id) != 0;
    }
    std::optional<homeworldz::physics::BodyState> body_state(
        homeworldz::physics::BodyId) const override { return std::nullopt; }
    void set_body_state(const homeworldz::physics::BodyState&) override {}
    void apply_impulse(homeworldz::physics::BodyId, homeworldz::scene::Vector3) override {}
    homeworldz::physics::CharacterId create_character(
        const homeworldz::physics::CharacterDefinition&) override { return 1; }
    bool remove_character(homeworldz::physics::CharacterId) override { return true; }
    std::optional<homeworldz::physics::BodyState> character_state(
        homeworldz::physics::CharacterId) const override { return std::nullopt; }
    void set_character_state(homeworldz::physics::CharacterId,
                             const homeworldz::physics::BodyState&) override {}
    void set_character_velocity(homeworldz::physics::CharacterId,
                                homeworldz::scene::Vector3) override {}
    void step(double) override {}
    std::span<const homeworldz::physics::Contact> contacts() const override { return {}; }
    std::optional<homeworldz::physics::RayHit> ray_cast(
        homeworldz::scene::Vector3, homeworldz::scene::Vector3, double) const override {
        return std::nullopt;
    }
    homeworldz::physics::TransferState capture(
        std::span<const homeworldz::physics::BodyId>) const override { return {}; }
    void restore(const homeworldz::physics::TransferState&) override {}

    homeworldz::physics::BodyDefinition last_definition;
    std::unordered_map<homeworldz::physics::BodyId,
                       homeworldz::physics::BodyDefinition> definitions;
    homeworldz::physics::BodyId next_id{1};
};

bool close(double first, double second) {
    return std::abs(first - second) < 0.000001;
}

} // namespace

int main() {
    using namespace homeworldz;
    const auto glass = physics::material_properties(0x02);
    const auto wood = physics::material_properties(0x03);
    const auto rubber = physics::material_properties(0x06);
    if (!close(glass.density, 1000.0) || !close(wood.density, 1000.0) ||
        !close(glass.friction, 0.2) || !close(glass.restitution, 0.7) ||
        !close(wood.friction, 0.6) || !close(wood.restitution, 0.5) ||
        !close(rubber.friction, 0.9) || !close(rubber.restitution, 0.9))
        return 1;
    if (!close(physics::box_mass({2.0, 3.0, 4.0}, 1000.0), 24000.0) ||
        !close(physics::box_mass({0.0, 1.0, 1.0}, 1000.0), 0.001) ||
        !close(physics::box_mass({100.0, 100.0, 100.0}, 1000.0), 100000.0) ||
        !close(physics::ellipsoid_mass({2.0, 3.0, 4.0}, 1000.0), 12566.3706143592) ||
        !close(physics::cylinder_mass({2.0, 3.0, 4.0}, 1000.0), 18849.5559215388) ||
        !close(physics::prism_mass({2.0, 3.0, 4.0}, 1000.0), 12000.0))
        return 2;

    RecordingWorld world;
    physics::StaticSceneMirror mirror(world);
    scene::Entity entity;
    entity.id = 42;
    entity.object_id = "object";
    entity.physical = true;
    entity.material = 0x02;
    entity.scale = {2.0, 3.0, 4.0};
    entity.physics_density = 125.0;
    entity.physics_friction = 0.7;
    entity.physics_restitution = 0.25;
    entity.physics_gravity_multiplier = 1.5;
    if (!mirror.synchronize(entity) || mirror.size() != 1 ||
        world.last_definition.motion != physics::MotionType::Dynamic ||
        !close(world.last_definition.mass, 3000.0) ||
        !close(world.last_definition.friction, 0.7) ||
        !close(world.last_definition.restitution, 0.25) ||
        !close(world.last_definition.gravity_multiplier, 1.5))
        return 3;

    const auto original_body = mirror.body_id(entity.id);
    entity.material = 0x06;
    entity.physics_friction = 0.9;
    entity.physics_restitution = 0.9;
    if (!mirror.synchronize(entity) || mirror.body_id(entity.id) == original_body ||
        world.definitions.contains(original_body) || !close(world.last_definition.friction, 0.9))
        return 4;
    entity.path_curve = 0x20;
    entity.profile_curve = 0x05;
    entity.scale = {2.0, 2.0, 2.0};
    if (!mirror.synchronize(entity) || world.last_definition.shape.type != physics::ShapeType::Sphere ||
        !close(world.last_definition.shape.radius, 1.0) ||
        !close(world.last_definition.mass, 523.598775598299))
        return 6;
    entity.path_curve = 0x10;
    entity.profile_curve = 0x00;
    entity.scale = {2.0, 2.0, 3.0};
    if (!mirror.synchronize(entity) || world.last_definition.shape.type != physics::ShapeType::Cylinder ||
        !close(world.last_definition.shape.radius, 1.0) ||
        !close(world.last_definition.shape.height, 3.0) ||
        !close(world.last_definition.mass, 1178.09724509617))
        return 7;
    entity.profile_curve = 0x01;
    entity.path_scale_x = 0;
    entity.path_scale_y = 100;
    entity.path_shear_x = 0xce;
    entity.path_shear_y = 0;
    if (!mirror.synchronize(entity) ||
        world.last_definition.shape.type != physics::ShapeType::ConvexHull ||
        world.last_definition.shape.hull_points.size() != 6 ||
        !close(world.last_definition.shape.hull_points[0].x, -1.0) ||
        !close(world.last_definition.shape.hull_points[1].y, 1.0) ||
        !close(world.last_definition.shape.hull_points[2].x, 1.0) ||
        !close(world.last_definition.shape.hull_points[4].z, 1.5) ||
        !close(world.last_definition.mass, 750.0))
        return 8;
    const auto rubber_body = mirror.body_id(entity.id);
    entity.physics_shape_type = 0x01;
    if (!mirror.synchronize(entity) || mirror.size() != 0 || world.definitions.contains(rubber_body))
        return 5;
    return 0;
}
