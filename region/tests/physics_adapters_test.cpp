#include "homeworldz/physics_adapters.h"

#include <array>
#include <cmath>
#include <memory>

namespace {
bool smoke_test(std::unique_ptr<homeworldz::physics::World> world) {
    using namespace homeworldz;
    physics::BodyDefinition floor;
    floor.entity_id = 1;
    floor.shape.half_extents = {10, 0.5, 10};
    floor.position = {0, -0.5, 0};
    world->create_body(floor);
    physics::BodyDefinition ball;
    ball.entity_id = 2;
    ball.motion = physics::MotionType::Dynamic;
    ball.shape.type = physics::ShapeType::Sphere;
    ball.position = {0, 4, 0};
    const auto id = world->create_body(ball);
    world->apply_impulse(id, {1, 0, 0});
    for (int step = 0; step < 30; ++step) world->step(1.0 / 60.0);
    const auto state = world->body_state(id);
    if (!state || state->position.x <= 0 || state->position.y >= 4) return false;
    const std::array ids{id};
    const auto transfer = world->capture(ids);
    if (transfer.bodies.size() != 1) return false;
    auto moved = transfer.bodies.front();
    moved.position = {2, 8, 0};
    moved.sleeping = false;
    world->restore({{moved}});
    const auto restored = world->body_state(id);
    if (!restored || std::abs(restored->position.y - 8) > 0.01) return false;
    return world->ray_cast({0, 10, 0}, {0, -1, 0}, 20).has_value();
}
}

int main() {
    if (!smoke_test(homeworldz::physics::make_jolt_world())) return 1;
    if (!smoke_test(homeworldz::physics::make_physx_world())) return 1;
    return 0;
}
