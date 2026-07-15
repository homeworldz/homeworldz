#include "homeworldz/physics_scenarios.h"

#include <array>
#include <cmath>
#include <exception>

namespace homeworldz::physics {
namespace {

constexpr double tick = 1.0 / 60.0;

BodyId add_floor(World& world) {
    BodyDefinition floor;
    floor.entity_id = 1;
    floor.shape.half_extents = {32, 32, 0.5};
    floor.position = {0, 0, -0.5};
    return world.create_body(floor);
}

BodyId add_dynamic(World& world, scene::EntityId entity, scene::Vector3 position,
                   Shape shape = {}) {
    BodyDefinition body;
    body.entity_id = entity;
    body.motion = MotionType::Dynamic;
    body.shape = shape;
    body.position = position;
    return world.create_body(body);
}

void advance(World& world, std::size_t steps) {
    for (std::size_t step = 0; step < steps; ++step) world.step(tick);
}

ScenarioResult terrain(const WorldFactory& factory) {
    auto world = factory();
    add_floor(*world);
    Shape sphere; sphere.type = ShapeType::Sphere;
    const auto body = add_dynamic(*world, 2, {0, 0, 5}, sphere);
    advance(*world, 120);
    const auto state = world->body_state(body);
    const bool passed = state && state->position.z > -0.1 && state->position.z < 1.0;
    return {"terrain-collision", passed, passed ? "body settled on terrain" : "body did not settle on terrain", 2, 120};
}

ScenarioResult avatar(const WorldFactory& factory) {
    auto world = factory();
    add_floor(*world);
    const auto character = world->create_character({2, {0, 0, 1}});
    world->set_character_velocity(character, {2, 0, 0});
    advance(*world, 30);
    const auto state = world->character_state(character);
    const bool passed = state && state->position.x > 0.25 && world->remove_character(character);
    return {"avatar-controller", passed, passed ? "character moved and was removed" : "character lifecycle failed", 2, 30};
}

ScenarioResult stacking(const WorldFactory& factory) {
    auto world = factory();
    add_floor(*world);
    std::array<BodyId, 5> bodies{};
    for (std::size_t index = 0; index < bodies.size(); ++index)
        bodies[index] = add_dynamic(*world, 10 + index, {0, 0, 0.6 + index * 1.1});
    advance(*world, 180);
    bool passed = true;
    double previous = -1.0;
    for (const auto body : bodies) {
        const auto state = world->body_state(body);
        passed = passed && state && state->position.z > previous && state->position.z > -0.1;
        if (state) previous = state->position.z;
    }
    return {"object-stacking", passed, passed ? "stack remained ordered" : "stack collapsed or tunneled", 6, 180};
}

ScenarioResult impulse(const WorldFactory& factory) {
    auto world = factory();
    add_floor(*world);
    const auto body = add_dynamic(*world, 2, {0, 0, 1});
    world->apply_impulse(body, {4, 0, 2});
    advance(*world, 30);
    const auto state = world->body_state(body);
    const bool passed = state && state->position.x > 0.25;
    return {"scripted-impulse", passed, passed ? "impulse changed trajectory" : "impulse had no visible effect", 2, 30};
}

ScenarioResult vehicle(const WorldFactory& factory) {
    auto world = factory();
    add_floor(*world);
    Shape chassis; chassis.half_extents = {1.2, 0.3, 0.7};
    const auto body = add_dynamic(*world, 2, {0, 0, 0.6}, chassis);
    for (int step = 0; step < 60; ++step) {
        world->apply_impulse(body, {0.4, 0, 0});
        world->step(tick);
    }
    const auto state = world->body_state(body);
    const bool passed = state && state->position.x > 1.0 && state->position.z > -0.1;
    return {"vehicle-style-motion", passed, passed ? "chassis accelerated across terrain" : "chassis motion failed", 2, 60};
}

ScenarioResult handoff(const WorldFactory& factory) {
    auto world = factory();
    const auto body = add_dynamic(*world, 2, {255.5, 0, 4});
    world->apply_impulse(body, {2, 0, 0});
    advance(*world, 15);
    const std::array ids{body};
    const auto transfer = world->capture(ids);
    if (transfer.bodies.size() != 1)
        return {"region-handoff", false, "capture omitted the crossing body", 1, 15};
    auto state = transfer.bodies.front();
    state.position.x -= 256.0;
    world->restore({{state}});
    const auto restored = world->body_state(body);
    const bool passed = restored && std::abs(restored->position.x - state.position.x) < 0.01
        && std::abs(restored->linear_velocity.x - state.linear_velocity.x) < 0.01;
    return {"region-handoff", passed, passed ? "position and velocity survived handoff" : "handoff state drifted", 1, 15};
}

ScenarioResult restore(const WorldFactory& factory) {
    auto world = factory();
    const auto body = add_dynamic(*world, 2, {3, 2, 7});
    const std::array ids{body};
    auto transfer = world->capture(ids);
    world->apply_impulse(body, {5, 0, 0});
    advance(*world, 20);
    world->restore(transfer);
    const auto state = world->body_state(body);
    const auto& expected = transfer.bodies.front();
    const bool passed = state && std::abs(state->position.x - expected.position.x) < 0.01
        && std::abs(state->position.z - expected.position.z) < 0.01;
    return {"state-restore", passed, passed ? "captured transform was restored" : "restored transform differed", 1, 20};
}

ScenarioResult load(const WorldFactory& factory) {
    auto world = factory();
    add_floor(*world);
    constexpr std::size_t count = 256;
    for (std::size_t index = 0; index < count; ++index) {
        Shape sphere; sphere.type = ShapeType::Sphere; sphere.radius = 0.2;
        add_dynamic(*world, 1000 + index,
            {static_cast<double>(index % 16) - 8, static_cast<double>((index / 4) % 16) - 8,
             2.0 + static_cast<double>(index / 16) * 0.5}, sphere);
    }
    advance(*world, 30);
    const bool passed = world->body_state(count).has_value();
    return {"region-load", passed, passed ? "loaded bodies advanced" : "loaded body was lost", count + 1, 30};
}

template <typename Function>
ScenarioResult guarded(std::string name, Function function) {
    try { return function(); }
    catch (const std::exception& error) { return {std::move(name), false, error.what(), 0, 0}; }
}

} // namespace

std::vector<ScenarioResult> run_common_scenarios(const WorldFactory& factory) {
    std::vector<ScenarioResult> results;
    results.push_back(guarded("avatar-controller", [&] { return avatar(factory); }));
    results.push_back(guarded("terrain-collision", [&] { return terrain(factory); }));
    results.push_back(guarded("object-stacking", [&] { return stacking(factory); }));
    results.push_back(guarded("scripted-impulse", [&] { return impulse(factory); }));
    results.push_back(guarded("vehicle-style-motion", [&] { return vehicle(factory); }));
    results.push_back(guarded("region-handoff", [&] { return handoff(factory); }));
    results.push_back(guarded("state-restore", [&] { return restore(factory); }));
    results.push_back(guarded("region-load", [&] { return load(factory); }));
    return results;
}

} // namespace homeworldz::physics
