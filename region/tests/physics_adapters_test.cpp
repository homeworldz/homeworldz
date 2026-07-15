#include "homeworldz/physics_adapters.h"

#include <array>
#include <cmath>
#include <memory>

namespace {
bool smoke_test(std::unique_ptr<homeworldz::physics::World> world) {
    using namespace homeworldz;
    physics::BodyDefinition floor;
    floor.entity_id = 1;
    floor.shape.half_extents = {10, 10, 0.5};
    floor.position = {0, 0, -0.5};
    world->create_body(floor);
    physics::BodyDefinition ball;
    ball.entity_id = 2;
    ball.motion = physics::MotionType::Dynamic;
    ball.shape.type = physics::ShapeType::Sphere;
    ball.position = {0, 0, 4};
    const auto id = world->create_body(ball);
    world->apply_impulse(id, {1, 0, 0});
    for (int step = 0; step < 30; ++step) world->step(1.0 / 60.0);
    const auto state = world->body_state(id);
    if (!state || state->position.x <= 0 || state->position.z >= 4) return false;
    const std::array ids{id};
    const auto transfer = world->capture(ids);
    if (transfer.bodies.size() != 1) return false;
    auto moved = transfer.bodies.front();
    moved.position = {2, 0, 8};
    moved.sleeping = false;
    world->restore({{moved}});
    const auto restored = world->body_state(id);
    if (!restored || std::abs(restored->position.z - 8) > 0.01) return false;
    return world->ray_cast({0, 0, 10}, {0, 0, -1}, 20).has_value();
}

bool jolt_heightfield_test() {
    auto world = homeworldz::physics::make_jolt_world();
    homeworldz::physics::HeightFieldDefinition terrain;
    terrain.entity_id = 99;
    terrain.sample_count = 8;
    terrain.samples.resize(64);
    for (std::uint32_t y = 0; y < terrain.sample_count; ++y)
        for (std::uint32_t x = 0; x < terrain.sample_count; ++x)
            terrain.samples[y * terrain.sample_count + x] = static_cast<float>(y);
    const auto body = world->create_heightfield(terrain);
    homeworldz::physics::BodyDefinition obstacle;
    obstacle.entity_id = 100;
    obstacle.position = {2, 6, 10};
    const auto obstacle_body = world->create_body(obstacle);
    const auto nearest = world->ray_cast({2, 6, 20}, {0, 0, -1}, 30);
    const auto hit = world->ray_cast_body(body, {2, 6, 20}, {0, 0, -1}, 30);
    return body != 0 && obstacle_body != 0 && nearest && nearest->body == obstacle_body && hit &&
           hit->body == body && std::abs(hit->point.z - 6.0) < 0.1;
}
}

int main() {
    if (!jolt_heightfield_test()) return 1;
    if (!smoke_test(homeworldz::physics::make_jolt_world())) return 1;
    if (!smoke_test(homeworldz::physics::make_physx_world())) return 1;
    return 0;
}
