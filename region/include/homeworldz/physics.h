#pragma once

#include "homeworldz/scene.h"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace homeworldz::physics {

using BodyId = std::uint64_t;
using CharacterId = std::uint64_t;

enum class MotionType { Static, Kinematic, Dynamic };
enum class ShapeType { Box, Sphere, Capsule };

struct Shape {
    ShapeType type{ShapeType::Box};
    scene::Vector3 half_extents{0.5, 0.5, 0.5};
    double radius{0.5};
    double height{1.0};
};

struct BodyDefinition {
    scene::EntityId entity_id{};
    MotionType motion{MotionType::Static};
    Shape shape;
    scene::Vector3 position;
    scene::Vector3 velocity;
    double mass{1.0};
    double friction{0.5};
    double restitution{};
    std::array<double, 4> rotation{0.0, 0.0, 0.0, 1.0};
};

struct BodyState {
    BodyId body_id{};
    scene::EntityId entity_id{};
    scene::Vector3 position;
    scene::Vector3 linear_velocity;
    scene::Vector3 angular_velocity;
    bool sleeping{};
    bool grounded{};
    std::array<double, 4> rotation{0.0, 0.0, 0.0, 1.0};
};

struct CharacterDefinition {
    scene::EntityId entity_id{};
    scene::Vector3 position;
    double radius{0.35};
    double height{1.8};
    double step_height{0.4};
    double mass{70.0};
    double maximum_horizontal_acceleration{30.0};
};

struct HeightFieldDefinition {
    scene::EntityId entity_id{};
    std::vector<float> samples;
    std::uint32_t sample_count{};
    double spacing{1.0};
};

struct Contact {
    BodyId first{};
    BodyId second{};
    scene::Vector3 point;
    scene::Vector3 normal;
    double penetration{};
};

struct RayHit {
    BodyId body{};
    scene::Vector3 point;
    scene::Vector3 normal;
    double fraction{};
};

struct TransferState {
    std::vector<BodyState> bodies;
};

class World {
public:
    virtual ~World() = default;

    virtual BodyId create_body(const BodyDefinition& definition) = 0;
    virtual bool remove_body(BodyId id) = 0;
    virtual std::optional<BodyState> body_state(BodyId id) const = 0;
    virtual void set_body_state(const BodyState& state) = 0;
    virtual void apply_impulse(BodyId id, scene::Vector3 impulse) = 0;
    virtual BodyId create_heightfield(const HeightFieldDefinition&) { return 0; }

    virtual CharacterId create_character(const CharacterDefinition& definition) = 0;
    virtual bool remove_character(CharacterId id) = 0;
    virtual std::optional<BodyState> character_state(CharacterId id) const = 0;
    virtual void set_character_state(CharacterId id, const BodyState& state) = 0;
    virtual void set_character_velocity(CharacterId id, scene::Vector3 velocity) = 0;
    virtual void set_character_flying(CharacterId, bool) {}

    virtual void step(double seconds) = 0;
    virtual std::span<const Contact> contacts() const = 0;
    virtual std::optional<RayHit> ray_cast(scene::Vector3 origin, scene::Vector3 direction,
                                           double maximum_distance) const = 0;
    virtual std::optional<RayHit> ray_cast_body(BodyId, scene::Vector3, scene::Vector3,
                                                double) const { return std::nullopt; }

    virtual TransferState capture(std::span<const BodyId> bodies) const = 0;
    virtual void restore(const TransferState& state) = 0;
};

} // namespace homeworldz::physics
