#include "homeworldz/physics_adapters.h"

#include <PxPhysicsAPI.h>

#include <algorithm>
#include <stdexcept>
#include <thread>
#include <unordered_map>

namespace homeworldz::physics {
namespace {

physx::PxVec3 vec(scene::Vector3 value) {
    return {static_cast<float>(value.x), static_cast<float>(value.y), static_cast<float>(value.z)};
}
scene::Vector3 vec(const physx::PxVec3& value) { return {value.x, value.y, value.z}; }

struct PhysxBody { physx::PxRigidActor* actor{}; scene::EntityId entity{}; };

class PhysxWorld final : public World {
public:
    PhysxWorld() {
        foundation_ = PxCreateFoundation(PX_PHYSICS_VERSION, allocator_, errors_);
        if (!foundation_) throw std::runtime_error("PhysX foundation initialization failed");
        physics_ = PxCreatePhysics(PX_PHYSICS_VERSION, *foundation_, physx::PxTolerancesScale());
        if (!physics_) throw std::runtime_error("PhysX initialization failed");
        const auto hardware_threads = std::thread::hardware_concurrency();
        dispatcher_ = physx::PxDefaultCpuDispatcherCreate(std::max(1u, hardware_threads > 1 ? hardware_threads - 1 : 1));
        physx::PxSceneDesc description(physics_->getTolerancesScale());
        description.gravity = {0.0f, 0.0f, -9.81f};
        description.cpuDispatcher = dispatcher_;
        description.filterShader = physx::PxDefaultSimulationFilterShader;
        scene_ = physics_->createScene(description);
        if (!scene_) throw std::runtime_error("PhysX scene initialization failed");
    }

    ~PhysxWorld() override {
        for (auto& [id, body] : bodies_) { static_cast<void>(id); body.actor->release(); }
        if (scene_) scene_->release();
        if (dispatcher_) dispatcher_->release();
        if (physics_) physics_->release();
        if (foundation_) foundation_->release();
    }

    BodyId create_body(const BodyDefinition& definition) override {
        const physx::PxTransform transform(
            vec(definition.position), physx::PxQuat(
                static_cast<float>(definition.rotation[0]), static_cast<float>(definition.rotation[1]),
                static_cast<float>(definition.rotation[2]), static_cast<float>(definition.rotation[3])).getNormalized());
        physx::PxRigidActor* actor = nullptr;
        if (definition.motion == MotionType::Static) actor = physics_->createRigidStatic(transform);
        else {
            auto* dynamic = physics_->createRigidDynamic(transform);
            if (definition.motion == MotionType::Kinematic)
                dynamic->setRigidBodyFlag(physx::PxRigidBodyFlag::eKINEMATIC, true);
            actor = dynamic;
        }
        if (!actor) throw std::runtime_error("PhysX could not create an actor");
        auto* material = physics_->createMaterial(static_cast<float>(definition.friction),
            static_cast<float>(definition.friction), static_cast<float>(definition.restitution));
        if (!material) { actor->release(); throw std::runtime_error("PhysX could not create a material"); }
        physx::PxShape* shape = nullptr;
        switch (definition.shape.type) {
        case ShapeType::Sphere:
            shape = physics_->createShape(physx::PxSphereGeometry(static_cast<float>(definition.shape.radius)), *material);
            break;
        case ShapeType::Capsule:
            shape = physics_->createShape(physx::PxCapsuleGeometry(static_cast<float>(definition.shape.radius),
                static_cast<float>(std::max(0.0, definition.shape.height * 0.5 - definition.shape.radius))), *material);
            if (shape)
                shape->setLocalPose(physx::PxTransform(
                    physx::PxQuat(physx::PxHalfPi, physx::PxVec3(0.0F, 1.0F, 0.0F))));
            break;
        case ShapeType::Cylinder:
            // A box preserves the bounds but destroys the round circumference
            // needed by wheels and rollers. Require the portable convex-cylinder
            // cooking path instead of silently changing physical behavior.
            break;
        case ShapeType::Box:
        default:
            shape = physics_->createShape(physx::PxBoxGeometry(vec(definition.shape.half_extents)), *material);
            break;
        }
        if (!shape || !actor->attachShape(*shape)) {
            if (shape) shape->release();
            material->release();
            actor->release();
            throw std::runtime_error("PhysX could not attach a body shape");
        }
        shape->release();
        material->release();
        if (auto* dynamic = actor->is<physx::PxRigidDynamic>()) {
            if (definition.motion == MotionType::Dynamic) {
                dynamic->setLinearVelocity(vec(definition.velocity));
                physx::PxRigidBodyExt::setMassAndUpdateInertia(*dynamic, static_cast<float>(std::max(0.001, definition.mass)));
            }
        }
        scene_->addActor(*actor);
        const auto id = next_body_++;
        bodies_.emplace(id, PhysxBody{actor, definition.entity_id});
        actor_to_body_.emplace(actor, id);
        return id;
    }

    bool remove_body(BodyId id) override {
        const auto found = bodies_.find(id);
        if (found == bodies_.end()) return false;
        actor_to_body_.erase(found->second.actor);
        found->second.actor->release();
        bodies_.erase(found);
        return true;
    }

    std::optional<BodyState> body_state(BodyId id) const override {
        const auto found = bodies_.find(id);
        if (found == bodies_.end()) return std::nullopt;
        scene::Vector3 linear, angular;
        bool sleeping = true;
        if (const auto* dynamic = found->second.actor->is<physx::PxRigidDynamic>()) {
            linear = vec(dynamic->getLinearVelocity());
            angular = vec(dynamic->getAngularVelocity());
            sleeping = dynamic->isSleeping();
        }
        const auto pose = found->second.actor->getGlobalPose();
        return BodyState{id, found->second.entity, vec(pose.p), linear, angular, sleeping, false,
                         {pose.q.x, pose.q.y, pose.q.z, pose.q.w}};
    }

    void set_body_state(const BodyState& state) override {
        const auto found = bodies_.find(state.body_id);
        if (found == bodies_.end()) return;
        const auto pose = physx::PxTransform(vec(state.position), physx::PxQuat(
            static_cast<float>(state.rotation[0]), static_cast<float>(state.rotation[1]),
            static_cast<float>(state.rotation[2]), static_cast<float>(state.rotation[3])));
        found->second.actor->setGlobalPose(pose);
        if (auto* dynamic = found->second.actor->is<physx::PxRigidDynamic>()) {
            if (dynamic->getRigidBodyFlags().isSet(physx::PxRigidBodyFlag::eKINEMATIC))
                dynamic->setKinematicTarget(pose);
            else {
                dynamic->setLinearVelocity(vec(state.linear_velocity));
                dynamic->setAngularVelocity(vec(state.angular_velocity));
                if (state.sleeping) dynamic->putToSleep(); else dynamic->wakeUp();
            }
        }
    }

    void apply_impulse(BodyId id, scene::Vector3 impulse) override {
        const auto found = bodies_.find(id);
        if (found != bodies_.end())
            if (auto* dynamic = found->second.actor->is<physx::PxRigidDynamic>())
                dynamic->addForce(vec(impulse), physx::PxForceMode::eIMPULSE);
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
        character_velocities_.erase(id);
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
        if (characters_.contains(id)) character_velocities_[id] = velocity;
    }

    void step(double seconds) override {
        contacts_.clear();
        for (const auto& [character_id, velocity] : character_velocities_) {
            const auto character = characters_.find(character_id);
            if (character == characters_.end()) continue;
            const auto body = bodies_.find(character->second);
            if (body == bodies_.end()) continue;
            if (auto* dynamic = body->second.actor->is<physx::PxRigidDynamic>())
                dynamic->setKinematicTarget(physx::PxTransform(
                    dynamic->getGlobalPose().p + vec(velocity) * static_cast<float>(seconds)));
        }
        scene_->simulate(static_cast<float>(seconds));
        scene_->fetchResults(true);
    }
    std::span<const Contact> contacts() const override { return contacts_; }
    std::optional<RayHit> ray_cast(scene::Vector3 origin, scene::Vector3 direction,
                                   double maximum_distance) const override {
        physx::PxRaycastBuffer hit;
        if (!scene_->raycast(vec(origin), vec(direction).getNormalized(), static_cast<float>(maximum_distance), hit)
            || !hit.hasBlock) return std::nullopt;
        const auto found = actor_to_body_.find(hit.block.actor);
        if (found == actor_to_body_.end()) return std::nullopt;
        return RayHit{found->second, vec(hit.block.position), vec(hit.block.normal), hit.block.distance / maximum_distance};
    }
    TransferState capture(std::span<const BodyId> ids) const override {
        TransferState result;
        for (const auto id : ids) if (const auto state = body_state(id)) result.bodies.push_back(*state);
        return result;
    }
    void restore(const TransferState& state) override { for (const auto& body : state.bodies) set_body_state(body); }

private:
    physx::PxDefaultAllocator allocator_;
    physx::PxDefaultErrorCallback errors_;
    physx::PxFoundation* foundation_{};
    physx::PxPhysics* physics_{};
    physx::PxDefaultCpuDispatcher* dispatcher_{};
    physx::PxScene* scene_{};
    BodyId next_body_{1};
    CharacterId next_character_{1};
    std::unordered_map<BodyId, PhysxBody> bodies_;
    std::unordered_map<const physx::PxRigidActor*, BodyId> actor_to_body_;
    std::unordered_map<CharacterId, BodyId> characters_;
    std::unordered_map<CharacterId, scene::Vector3> character_velocities_;
    std::vector<Contact> contacts_;
};

} // namespace

std::unique_ptr<World> make_physx_world() { return std::make_unique<PhysxWorld>(); }

} // namespace homeworldz::physics
