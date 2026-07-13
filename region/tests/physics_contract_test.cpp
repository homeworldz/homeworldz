#include "homeworldz/physics.h"

#include <array>
#include <unordered_map>

namespace {

class ContractWorld final : public homeworldz::physics::World {
public:
    homeworldz::physics::BodyId create_body(const homeworldz::physics::BodyDefinition& definition) override {
        const auto id = next_body_++;
        bodies_[id] = {id, definition.entity_id, definition.position, definition.velocity, {}, false};
        return id;
    }
    bool remove_body(homeworldz::physics::BodyId id) override { return bodies_.erase(id) != 0; }
    std::optional<homeworldz::physics::BodyState> body_state(homeworldz::physics::BodyId id) const override {
        const auto found = bodies_.find(id);
        return found == bodies_.end() ? std::nullopt : std::optional(found->second);
    }
    void set_body_state(const homeworldz::physics::BodyState& state) override { bodies_[state.body_id] = state; }
    void apply_impulse(homeworldz::physics::BodyId id, homeworldz::scene::Vector3 impulse) override {
        auto& velocity = bodies_.at(id).linear_velocity;
        velocity.x += impulse.x; velocity.y += impulse.y; velocity.z += impulse.z;
    }
    homeworldz::physics::CharacterId create_character(const homeworldz::physics::CharacterDefinition&) override {
        return next_character_++;
    }
    bool remove_character(homeworldz::physics::CharacterId) override { return true; }
    void set_character_velocity(homeworldz::physics::CharacterId, homeworldz::scene::Vector3) override {}
    void step(double seconds) override {
        for (auto& [id, state] : bodies_) {
            static_cast<void>(id);
            state.position.x += state.linear_velocity.x * seconds;
            state.position.y += state.linear_velocity.y * seconds;
            state.position.z += state.linear_velocity.z * seconds;
        }
    }
    std::span<const homeworldz::physics::Contact> contacts() const override { return contacts_; }
    std::optional<homeworldz::physics::RayHit> ray_cast(homeworldz::scene::Vector3,
        homeworldz::scene::Vector3, double) const override { return std::nullopt; }
    homeworldz::physics::TransferState capture(std::span<const homeworldz::physics::BodyId> ids) const override {
        homeworldz::physics::TransferState result;
        for (const auto id : ids) if (const auto state = body_state(id)) result.bodies.push_back(*state);
        return result;
    }
    void restore(const homeworldz::physics::TransferState& state) override {
        for (const auto& body : state.bodies) set_body_state(body);
    }

private:
    homeworldz::physics::BodyId next_body_{1};
    homeworldz::physics::CharacterId next_character_{1};
    std::unordered_map<homeworldz::physics::BodyId, homeworldz::physics::BodyState> bodies_;
    std::vector<homeworldz::physics::Contact> contacts_;
};

} // namespace

int main() {
    ContractWorld world;
    const auto body = world.create_body({42, homeworldz::physics::MotionType::Dynamic, {}, {}, {1, 0, 0}});
    world.apply_impulse(body, {1, 2, 3});
    world.step(0.5);
    const auto state = world.body_state(body);
    if (!state || state->position.x != 1.0 || state->position.y != 1.0 || state->position.z != 1.5) return 1;
    const std::array ids{body};
    const auto transfer = world.capture(ids);
    if (!world.remove_body(body) || world.body_state(body)) return 1;
    world.restore(transfer);
    if (!world.body_state(body) || !world.contacts().empty() || world.ray_cast({}, {1,0,0}, 10)) return 1;
    return 0;
}
