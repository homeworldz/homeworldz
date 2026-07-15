#include "homeworldz/physics_adapters.h"

#include <Jolt/Jolt.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/HeightFieldShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include <algorithm>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <unordered_map>

namespace homeworldz::physics {
namespace {

namespace Layers {
constexpr JPH::ObjectLayer static_body = 0;
constexpr JPH::ObjectLayer moving_body = 1;
constexpr JPH::uint count = 2;
}
namespace BroadLayers {
const JPH::BroadPhaseLayer static_body{0};
const JPH::BroadPhaseLayer moving_body{1};
constexpr JPH::uint count = 2;
}

class BroadPhaseLayers final : public JPH::BroadPhaseLayerInterface {
public:
    JPH::uint GetNumBroadPhaseLayers() const override { return BroadLayers::count; }
    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override {
        return layer == Layers::static_body ? BroadLayers::static_body : BroadLayers::moving_body;
    }
};

class ObjectBroadPhaseFilter final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer first, JPH::BroadPhaseLayer second) const override {
        return first == Layers::moving_body || second == BroadLayers::moving_body;
    }
};

class ObjectPairFilter final : public JPH::ObjectLayerPairFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer first, JPH::ObjectLayer second) const override {
        return first == Layers::moving_body || second == Layers::moving_body;
    }
};

void initialize_jolt() {
    static std::once_flag once;
    std::call_once(once, [] {
        JPH::RegisterDefaultAllocator();
        JPH::Factory::sInstance = new JPH::Factory();
        JPH::RegisterTypes();
    });
}

JPH::Vec3 vec(scene::Vector3 value) {
    return {static_cast<float>(value.x), static_cast<float>(value.y), static_cast<float>(value.z)};
}
scene::Vector3 vec(JPH::Vec3Arg value) { return {value.GetX(), value.GetY(), value.GetZ()}; }

JPH::ShapeRefC make_shape(const Shape& shape) {
    switch (shape.type) {
    case ShapeType::Sphere:
        return new JPH::SphereShape(static_cast<float>(shape.radius));
    case ShapeType::Capsule:
        return new JPH::CapsuleShape(static_cast<float>(std::max(0.0, shape.height * 0.5 - shape.radius)),
                                     static_cast<float>(shape.radius));
    case ShapeType::Box:
    default:
        return new JPH::BoxShape(vec(shape.half_extents));
    }
}

JPH::EMotionType motion(MotionType value) {
    if (value == MotionType::Dynamic) return JPH::EMotionType::Dynamic;
    if (value == MotionType::Kinematic) return JPH::EMotionType::Kinematic;
    return JPH::EMotionType::Static;
}

struct JoltBody {
    JPH::BodyID native;
    scene::EntityId entity{};
};

class JoltWorld final : public World {
public:
    JoltWorld()
        : allocator_(16 * 1024 * 1024),
          jobs_(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers,
                std::max(1u, std::thread::hardware_concurrency()) - 1) {
        system_.Init(65536, 0, 65536, 10240, broad_layers_, broad_filter_, pair_filter_);
        system_.SetGravity({0.0F, 0.0F, -9.81F});
    }

    BodyId create_body(const BodyDefinition& definition) override {
        JPH::BodyCreationSettings settings(make_shape(definition.shape), vec(definition.position),
            JPH::Quat::sIdentity(), motion(definition.motion),
            definition.motion == MotionType::Static ? Layers::static_body : Layers::moving_body);
        settings.mFriction = static_cast<float>(definition.friction);
        settings.mRestitution = static_cast<float>(definition.restitution);
        if (definition.motion == MotionType::Dynamic && definition.mass > 0) {
            settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
            settings.mMassPropertiesOverride.mMass = static_cast<float>(definition.mass);
        }
        const auto native = system_.GetBodyInterface().CreateAndAddBody(settings,
            definition.motion == MotionType::Static ? JPH::EActivation::DontActivate : JPH::EActivation::Activate);
        if (native.IsInvalid()) throw std::runtime_error("Jolt could not create a body");
        system_.GetBodyInterface().SetLinearVelocity(native, vec(definition.velocity));
        const auto id = next_body_++;
        bodies_.emplace(id, JoltBody{native, definition.entity_id});
        native_to_body_.emplace(native.GetIndexAndSequenceNumber(), id);
        return id;
    }

    bool remove_body(BodyId id) override {
        const auto found = bodies_.find(id);
        if (found == bodies_.end()) return false;
        auto& interface = system_.GetBodyInterface();
        interface.RemoveBody(found->second.native);
        interface.DestroyBody(found->second.native);
        native_to_body_.erase(found->second.native.GetIndexAndSequenceNumber());
        bodies_.erase(found);
        return true;
    }

    std::optional<BodyState> body_state(BodyId id) const override {
        const auto found = bodies_.find(id);
        if (found == bodies_.end()) return std::nullopt;
        const auto& interface = system_.GetBodyInterface();
        return BodyState{id, found->second.entity, vec(interface.GetPosition(found->second.native)),
            vec(interface.GetLinearVelocity(found->second.native)),
            vec(interface.GetAngularVelocity(found->second.native)), !interface.IsActive(found->second.native)};
    }

    void set_body_state(const BodyState& state) override {
        const auto found = bodies_.find(state.body_id);
        if (found == bodies_.end()) return;
        auto& interface = system_.GetBodyInterface();
        interface.SetPosition(found->second.native, vec(state.position), JPH::EActivation::Activate);
        interface.SetLinearAndAngularVelocity(found->second.native, vec(state.linear_velocity), vec(state.angular_velocity));
        if (state.sleeping) interface.DeactivateBody(found->second.native);
    }

    void apply_impulse(BodyId id, scene::Vector3 impulse) override {
        if (const auto found = bodies_.find(id); found != bodies_.end())
            system_.GetBodyInterface().AddImpulse(found->second.native, vec(impulse));
    }

    BodyId create_heightfield(const HeightFieldDefinition& definition) override {
        const auto count = definition.sample_count;
        if (count < 4 || definition.samples.size() != static_cast<std::size_t>(count) * count ||
            !std::isfinite(definition.spacing) || definition.spacing <= 0.0)
            throw std::invalid_argument("invalid Jolt heightfield definition");
        std::vector<float> reversed(definition.samples.size());
        for (std::uint32_t y = 0; y < count; ++y)
            std::copy_n(definition.samples.begin() + static_cast<std::size_t>(count - 1 - y) * count,
                        count, reversed.begin() + static_cast<std::size_t>(y) * count);
        JPH::HeightFieldShapeSettings shape_settings(
            reversed.data(), JPH::Vec3::sZero(),
            {static_cast<float>(definition.spacing), 1.0F, static_cast<float>(definition.spacing)}, count);
        const auto shape = shape_settings.Create();
        if (shape.HasError()) throw std::runtime_error(shape.GetError().c_str());
        const auto rotation = JPH::Quat::sRotation(JPH::Vec3::sAxisX(), JPH::DegreesToRadians(90.0F));
        const JPH::RVec3 position{
            0.0F, static_cast<float>(static_cast<double>(count - 1) * definition.spacing), 0.0F};
        JPH::BodyCreationSettings settings(
            shape.Get(), position, rotation, JPH::EMotionType::Static, Layers::static_body);
        const auto native = system_.GetBodyInterface().CreateAndAddBody(settings, JPH::EActivation::DontActivate);
        if (native.IsInvalid()) throw std::runtime_error("Jolt could not create a heightfield");
        const auto id = next_body_++;
        bodies_.emplace(id, JoltBody{native, definition.entity_id});
        native_to_body_.emplace(native.GetIndexAndSequenceNumber(), id);
        return id;
    }

    CharacterId create_character(const CharacterDefinition& definition) override {
        BodyDefinition body{definition.entity_id, MotionType::Kinematic,
            {ShapeType::Capsule, {}, definition.radius, definition.height}, definition.position};
        const auto body_id = create_body(body);
        const auto id = next_character_++;
        characters_.emplace(id, body_id);
        return id;
    }
    bool remove_character(CharacterId id) override {
        const auto found = characters_.find(id);
        if (found == characters_.end()) return false;
        remove_body(found->second);
        characters_.erase(found);
        return true;
    }
    std::optional<BodyState> character_state(CharacterId id) const override {
        const auto found = characters_.find(id);
        return found == characters_.end() ? std::nullopt : body_state(found->second);
    }
    void set_character_state(CharacterId id, const BodyState& state) override {
        const auto found = characters_.find(id);
        if (found == characters_.end()) return;
        auto character = state;
        character.body_id = found->second;
        set_body_state(character);
    }
    void set_character_velocity(CharacterId id, scene::Vector3 velocity) override {
        if (const auto found = characters_.find(id); found != characters_.end()) {
            auto state = body_state(found->second);
            if (state) { state->linear_velocity = velocity; set_body_state(*state); }
        }
    }

    void step(double seconds) override {
        contacts_.clear();
        system_.Update(static_cast<float>(seconds), 1, &allocator_, &jobs_);
    }
    std::span<const Contact> contacts() const override { return contacts_; }

    std::optional<RayHit> ray_cast(scene::Vector3 origin, scene::Vector3 direction,
                                   double maximum_distance) const override {
        JPH::RRayCast ray{JPH::RVec3(vec(origin)), vec(direction).Normalized() * static_cast<float>(maximum_distance)};
        JPH::RayCastResult hit;
        if (!system_.GetNarrowPhaseQuery().CastRay(ray, hit)) return std::nullopt;
        const auto found = native_to_body_.find(hit.mBodyID.GetIndexAndSequenceNumber());
        if (found == native_to_body_.end()) return std::nullopt;
        const auto point = ray.GetPointOnRay(hit.mFraction);
        return RayHit{found->second, vec(point), {}, hit.mFraction};
    }

    std::optional<RayHit> ray_cast_body(BodyId id, scene::Vector3 origin,
                                        scene::Vector3 direction,
                                        double maximum_distance) const override {
        const auto target = bodies_.find(id);
        if (target == bodies_.end()) return std::nullopt;
        class TargetBodyFilter final : public JPH::BodyFilter {
        public:
            explicit TargetBodyFilter(JPH::BodyID target) : target_(target) {}
            bool ShouldCollide(const JPH::BodyID& candidate) const override {
                return candidate == target_;
            }
        private:
            JPH::BodyID target_;
        } filter(target->second.native);
        JPH::RRayCast ray{
            JPH::RVec3(vec(origin)), vec(direction).Normalized() * static_cast<float>(maximum_distance)};
        JPH::RayCastResult hit;
        if (!system_.GetNarrowPhaseQuery().CastRay(ray, hit, {}, {}, filter)) return std::nullopt;
        const auto point = ray.GetPointOnRay(hit.mFraction);
        return RayHit{id, vec(point), {}, hit.mFraction};
    }

    TransferState capture(std::span<const BodyId> ids) const override {
        TransferState result;
        for (const auto id : ids) if (const auto state = body_state(id)) result.bodies.push_back(*state);
        return result;
    }
    void restore(const TransferState& state) override {
        for (const auto& body : state.bodies) set_body_state(body);
    }

private:
    BroadPhaseLayers broad_layers_;
    ObjectBroadPhaseFilter broad_filter_;
    ObjectPairFilter pair_filter_;
    JPH::TempAllocatorImpl allocator_;
    JPH::JobSystemThreadPool jobs_;
    JPH::PhysicsSystem system_;
    BodyId next_body_{1};
    CharacterId next_character_{1};
    std::unordered_map<BodyId, JoltBody> bodies_;
    std::unordered_map<JPH::uint32, BodyId> native_to_body_;
    std::unordered_map<CharacterId, BodyId> characters_;
    std::vector<Contact> contacts_;
};

} // namespace

std::unique_ptr<World> make_jolt_world() {
    initialize_jolt();
    return std::make_unique<JoltWorld>();
}

} // namespace homeworldz::physics
