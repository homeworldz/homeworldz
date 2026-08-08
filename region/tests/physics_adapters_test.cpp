#include "homeworldz/physics_adapters.h"
#include "homeworldz/physics_scene.h"

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
    physics::BodyDefinition rotated;
    rotated.entity_id = 3;
    rotated.shape.half_extents = {2, 0.25, 0.25};
    rotated.position = {5, 0, 1};
    constexpr double half_sqrt_two = 0.7071067811865476;
    rotated.rotation = {0, 0, half_sqrt_two, half_sqrt_two};
    const auto rotated_id = world->create_body(rotated);
    const auto rotated_hit = world->ray_cast({5, 1, 10}, {0, 0, -1}, 20);
    if (!rotated_hit || rotated_hit->body != rotated_id) return false;
    physics::BodyDefinition capsule;
    capsule.entity_id = 4;
    capsule.shape.type = physics::ShapeType::Capsule;
    capsule.shape.radius = 0.3;
    capsule.shape.height = 4.0;
    capsule.position = {10, 0, 2};
    const auto capsule_id = world->create_body(capsule);
    const auto capsule_hit = world->ray_cast({8, 0, 3.5}, {1, 0, 0}, 4);
    if (!capsule_hit || capsule_hit->body != capsule_id) return false;
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

// What the walkable slope limit governs, on heightfield ground.
//
// Nothing asserted this until now, which is how three documents here and a
// header in the client repository came to claim the opposite of the code —
// "steeper ground holds but never grounds, so avatars slide" — with nothing to
// contradict them. The client core measured it in-world on purpose-built faces
// at 57.5, 65 and 70 degrees: an avatar rests on 70 and cannot walk up 65. So
// the limit is Jolt's mMaxSlopeAngle and governs *traversal*, while grounding is
// IsSupported() and is a contact test at any angle.
//
// Jolt-only on purpose. PhysX has no create_heightfield (the base returns 0), and
// engine parity is a goal rather than a guarantee (ADR 0015), so this belongs
// beside the other jolt_* tests rather than in run_common_scenarios.
//
// Two earlier attempts were withdrawn. One used a tilted box, whose contacts on a
// near-vertical face fall outside the character's supporting volume. The other put
// the character on flat ground and walked it *to* the ramp, so the approach
// dominated the distance and a walkable slope scored the same as an unwalkable
// one. This one places the character on the face.
//
// **It asserts resting on both sides of the limit, which is today's behaviour and
// not the desired one**: sliding on steep ground is pending. When that lands this
// test should fail and be updated deliberately, as the record of a chosen change.
bool jolt_walkable_slope_test() {
    using namespace homeworldz;
    // A ramp climbing along +x at a chosen angle, uniform in y. spacing is 1 m,
    // so a per-column rise of tan(angle) is that angle exactly.
    const auto ramp = [](double degrees) {
        physics::HeightFieldDefinition terrain;
        terrain.entity_id = 700;
        terrain.sample_count = 32;
        terrain.samples.resize(static_cast<std::size_t>(32) * 32);
        const auto rise = std::tan(degrees * 3.14159265358979323846 / 180.0);
        for (std::uint32_t y = 0; y < 32; ++y)
            for (std::uint32_t x = 0; x < 32; ++x)
                terrain.samples[y * 32u + x] = static_cast<float>(x * rise);
        return terrain;
    };
    // Placed on the face itself, at the height the ramp has there, then given a
    // moment to settle before anything is measured.
    const auto on_face = [&](double degrees, double push, double limit) {
        auto world = physics::make_jolt_world();
        if (world->create_heightfield(ramp(degrees)) == 0) return std::pair{false, 0.0};
        const auto rise = std::tan(degrees * 3.14159265358979323846 / 180.0);
        const double x0 = 12.0;
        physics::CharacterDefinition avatar;
        avatar.entity_id = 701;
        avatar.position = {x0, 16.0, x0 * rise + 1.2};
        avatar.walkable_slope_degrees = limit;
        const auto character = world->create_character(avatar);
        for (int step = 0; step < 90; ++step) world->step(1.0 / 60.0);
        const auto settled = world->character_state(character);
        if (!settled) return std::pair{false, 0.0};
        const auto from = settled->position.x;
        // Push uphill for a second and a half.
        for (int step = 0; step < 90; ++step) {
            world->set_character_velocity(character, {push, 0.0, 0.0});
            world->step(1.0 / 60.0);
        }
        const auto after = world->character_state(character);
        if (!after) return std::pair{false, 0.0};
        return std::pair{after->grounded, after->position.x - from};
    };

    constexpr double limit = physics::character_walkable_slope_degrees;  // 65
    const auto gentle = on_face(30.0, 4.0, limit);
    const auto steep = on_face(70.0, 4.0, limit);
    // The same 70 degree ramp against a limit that permits it. If traversal does
    // not change, this test is measuring how hard steep ground is to climb rather
    // than what the limit governs, and would keep passing if the limit stopped
    // being consulted at all.
    const auto steep_permitted = on_face(70.0, 4.0, 89.0);
    // Grounded on the gentle face and *not* on the steep one. Changed
    // 2026-08-08: grounding now means Jolt's OnGround rather than
    // IsSupported(), which was also true for OnSteepGround and so reported an
    // avatar as standing on ground it could never walk. The 70 degree face is
    // past the limit, so it no longer supports; gravity applies and the avatar
    // descends under the existing fall path.
    //
    // This assertion previously read "supported on both: grounding is a contact
    // test, not a slope test", which is what the region did and what this test
    // was written to pin down after two withdrawn attempts. It was an honest
    // record of real behaviour; the behaviour is what changed.
    if (!gentle.first || steep.first) return false;
    // Traversable only below the limit, which is the limit's actual job. The
    // gentle climb is generous and the steep one close to nothing; the gap is
    // wide enough that a small change in Jolt's solver will not flip it.
    if (gentle.second < 1.5) return false;
    if (steep.second > 0.5) return false;
    // Sensitive to the limit, not just to the geometry.
    if (steep_permitted.second <= steep.second + 0.5) return false;
    return true;
}

bool jolt_terrain_bounce_test() {
    using namespace homeworldz;
    auto world = physics::make_jolt_world();
    // Flat terrain at z = 5 with the default stone-like material. Before the
    // terrain carried a material, it inherited Jolt's restitution 0 and the
    // averaging combine halved every ground bounce.
    physics::HeightFieldDefinition terrain;
    terrain.entity_id = 120;
    terrain.sample_count = 8;
    terrain.samples.assign(64, 5.0F);
    if (world->create_heightfield(terrain) == 0) return false;
    physics::BodyDefinition ball;
    ball.entity_id = 121;
    ball.motion = physics::MotionType::Dynamic;
    ball.shape.type = physics::ShapeType::Sphere;
    ball.shape.radius = 0.25;
    ball.position = {3.5, 3.5, 7};
    ball.restitution = 0.9;
    ball.mass = 10.0;
    const auto body = world->create_body(ball);
    double rebound_speed = 0.0;
    for (int tick = 0; tick < 600; ++tick) {
        world->step(1.0 / 180.0);
        if (const auto state = world->body_state(body))
            rebound_speed = std::max(rebound_speed, state->linear_velocity.z);
    }
    // A 1.75 m drop impacts at about 5.9 m/s. Averaging 0.9 against the terrain's
    // 0.4 gives 0.65, so the rebound should be near 3.8 m/s; the old bare terrain
    // averaged to 0.45 and could not exceed about 2.7 m/s.
    return rebound_speed > 3.0 && rebound_speed < 4.6;
}

bool jolt_fast_fall_penetration_test() {
    using namespace homeworldz;
    auto world = physics::make_jolt_world();
    physics::HeightFieldDefinition terrain;
    terrain.entity_id = 130;
    terrain.sample_count = 8;
    terrain.samples.assign(64, 5.0F);
    if (world->create_heightfield(terrain) == 0) return false;
    // A thin, heavy box falling fast enough to cross more than its own thickness
    // in one 45 Hz region tick. Discrete collision let it sink into the terrain
    // and get pushed back out — the "squashing" a viewer sees on impact;
    // LinearCast sweeps the motion so the surface is never meaningfully crossed.
    physics::BodyDefinition slab;
    slab.entity_id = 131;
    slab.motion = physics::MotionType::Dynamic;
    slab.shape.half_extents = {0.5, 0.5, 0.1};
    slab.position = {3.5, 3.5, 25};
    slab.restitution = 0.0;
    slab.mass = 500.0;
    const auto body = world->create_body(slab);
    double lowest_center = 25.0;
    for (int tick = 0; tick < 240; ++tick) {
        world->step(1.0 / 45.0);
        if (const auto state = world->body_state(body))
            lowest_center = std::min(lowest_center, state->position.z);
    }
    // Resting center is 5.1; anything below 5.0 means the slab's midplane crossed
    // the terrain surface entirely. Allow a small solver margin above that.
    return lowest_center > 5.0;
}

bool jolt_restitution_combine_test() {
    using namespace homeworldz;
    auto world = physics::make_jolt_world();
    physics::BodyDefinition floor;
    floor.entity_id = 110;
    floor.shape.half_extents = {5, 5, 0.5};
    floor.position = {0, 0, -0.5};
    floor.restitution = 0.0;
    world->create_body(floor);
    physics::BodyDefinition ball;
    ball.entity_id = 111;
    ball.motion = physics::MotionType::Dynamic;
    ball.shape.type = physics::ShapeType::Sphere;
    ball.shape.radius = 0.5;
    ball.position = {0, 0, 2};
    ball.restitution = 0.5;
    const auto body = world->create_body(ball);
    double maximum_rebound_speed = 0.0;
    for (int tick = 0; tick < 600; ++tick) {
        world->step(1.0 / 180.0);
        if (const auto state = world->body_state(body))
            maximum_rebound_speed = std::max(maximum_rebound_speed, state->linear_velocity.z);
    }
    // A 1.5 m drop reaches about 5.4 m/s. Average restitution gives roughly
    // 1.35 m/s; Jolt's former max rule produces roughly 2.7 m/s.
    return maximum_rebound_speed > 0.8 && maximum_rebound_speed < 1.8;
}

bool static_scene_mirror_test() {
    auto world = homeworldz::physics::make_jolt_world();
    homeworldz::physics::StaticSceneMirror mirror(*world);
    homeworldz::scene::Scene scene;
    const auto avatar = scene.create("avatar", {2, 2, 2});
    const auto object = scene.create("rotated", {5, 0, 1});
    auto* entity = scene.find(object);
    entity->object_id = "00000000-0000-0000-0000-000000000001";
    entity->scale = {4, 0.5, 0.5};
    constexpr double half_sqrt_two = 0.7071067811865476;
    entity->rotation = {0, 0, half_sqrt_two};
    mirror.synchronize(scene);
    const auto first_body = mirror.body_id(object);
    const auto hit = world->ray_cast({5, 1, 10}, {0, 0, -1}, 20);
    if (mirror.size() != 1 || mirror.body_id(avatar) != 0 || first_body == 0 ||
        !hit || hit->body != first_body) return false;
    entity->position = {8, 0, 1};
    if (!mirror.synchronize(*entity) || mirror.body_id(object) == first_body) return false;
    const auto moved_hit = world->ray_cast({8, 1, 10}, {0, 0, -1}, 20);
    if (!moved_hit || moved_hit->body != mirror.body_id(object)) return false;
    entity->physical = true;
    entity->position.z = 5;
    if (!mirror.synchronize(*entity)) return false;
    const auto dynamic_body = mirror.body_id(object);
    world->step(0.1);
    const auto falling = world->body_state(dynamic_body);
    if (!falling || falling->position.z >= 5 || falling->linear_velocity.z >= 0) return false;
    entity->phantom = true;
    if (!mirror.synchronize(*entity) || mirror.body_id(object) != 0) return false;
    entity->phantom = false;
    scene.remove(object);
    mirror.synchronize(scene);
    return mirror.size() == 0;
}

bool jolt_compound_linkset_test() {
    using namespace homeworldz;
    auto world = physics::make_jolt_world();
    physics::BodyDefinition floor;
    floor.entity_id = 300;
    floor.shape.half_extents = {10, 10, 0.5};
    floor.position = {0, 0, -0.5};
    world->create_body(floor);
    scene::Scene scene;
    const auto root_id = scene.create("root", {0, 0, 4});
    const auto child_id = scene.create("child", {1.5, 0, 4});
    auto* root = scene.find(root_id);
    auto* child = scene.find(child_id);
    root->object_id = "00000000-0000-4000-8000-000000000301";
    child->object_id = "00000000-0000-4000-8000-000000000302";
    root->physical = true;
    child->scale = {2.0, 0.5, 0.5};
    constexpr double half_sqrt_two = 0.7071067811865476;
    child->rotation = {0.0, 0.0, half_sqrt_two};
    scene::establish_link(*child, *root);
    physics::StaticSceneMirror mirror(*world);
    mirror.synchronize(scene);
    const auto body_id = mirror.body_id(root_id);
    if (mirror.size() != 1 || body_id == 0 || mirror.body_id(child_id) != 0) return false;
    const auto root_hit = world->ray_cast({0, 0, 10}, {0, 0, -1}, 10);
    const auto child_hit = world->ray_cast({1.5, 0.8, 10}, {0, 0, -1}, 10);
    if (!root_hit || !child_hit || root_hit->body != body_id || child_hit->body != body_id)
        return false;
    for (int tick = 0; tick < 30; ++tick) world->step(1.0 / 60.0);
    const auto falling = world->body_state(body_id);
    return falling && falling->position.z < 4.0 && falling->linear_velocity.z < 0.0;
}

bool jolt_character_collision_test() {
    auto world = homeworldz::physics::make_jolt_world();
    homeworldz::physics::BodyDefinition floor;
    floor.entity_id = 1;
    floor.shape.half_extents = {4, 4, 0.5};
    floor.position = {0, 0, -0.5};
    world->create_body(floor);
    homeworldz::physics::BodyDefinition wall;
    wall.entity_id = 2;
    wall.shape.half_extents = {0.1, 2, 2};
    wall.position = {1, 0, 1};
    world->create_body(wall);
    const auto character = world->create_character({3, {0, 0, 0.9}, 0.3, 1.8, 0.4});
    world->set_character_velocity(character, {4, 0, 0});
    for (int step = 0; step < 60; ++step) world->step(1.0 / 60.0);
    const auto state = world->character_state(character);
    return state && state->position.x > 0.1 && state->position.x < 0.65;
}

bool jolt_character_step_test() {
    auto world = homeworldz::physics::make_jolt_world();
    homeworldz::physics::BodyDefinition floor;
    floor.entity_id = 1;
    floor.shape.half_extents = {4, 4, 0.5};
    floor.position = {0, 0, -0.5};
    world->create_body(floor);
    homeworldz::physics::BodyDefinition step;
    step.entity_id = 2;
    step.shape.half_extents = {1, 1, 0.125};
    step.position = {1.5, 0, 0.125};
    world->create_body(step);
    const auto character = world->create_character({3, {0, 0, 0.9}, 0.3, 1.8, 0.4});
    world->set_character_velocity(character, {2, 0, 0});
    for (int tick = 0; tick < 45; ++tick) world->step(1.0 / 60.0);
    const auto moving = world->character_state(character);
    if (!moving || moving->position.x <= 1.0 || moving->position.z <= 1.05 || !moving->grounded)
        return false;
    world->set_character_velocity(character, {});
    for (int tick = 0; tick < 300; ++tick) world->step(1.0 / 60.0);
    const auto resting = world->character_state(character);
    if (!resting || !resting->grounded || std::abs(resting->position.z - 1.15) > 0.01 ||
        std::abs(resting->linear_velocity.z) > 0.001)
        return false;
    const auto falling = world->create_character({4, {-2, 0, 4}, 0.3, 1.8, 0.4});
    world->set_character_velocity(falling, {0, 0, -5});
    for (int tick = 0; tick < 300; ++tick) world->step(1.0 / 60.0);
    const auto landed = world->character_state(falling);
    return landed && landed->grounded && std::abs(landed->position.z - 0.9) < 0.01 &&
           std::abs(landed->linear_velocity.z) < 0.001;
}

struct CharacterPushResult {
    double body_displacement;
    double character_x;
};

CharacterPushResult jolt_character_push_result(double body_mass) {
    using namespace homeworldz;
    auto world = physics::make_jolt_world();
    physics::BodyDefinition floor;
    floor.entity_id = 200;
    floor.shape.half_extents = {10, 10, 0.5};
    floor.position = {0, 0, -0.5};
    floor.friction = 0.6;
    world->create_body(floor);
    physics::BodyDefinition cube;
    cube.entity_id = 201;
    cube.motion = physics::MotionType::Dynamic;
    cube.shape.half_extents = {0.25, 0.25, 0.25};
    cube.position = {1.0, 0, 0.25};
    cube.mass = body_mass;
    cube.friction = 0.6;
    const auto cube_id = world->create_body(cube);
    physics::CharacterDefinition avatar;
    avatar.entity_id = 202;
    avatar.position = {0, 0, 0.9};
    avatar.radius = 0.3;
    avatar.height = 1.8;
    avatar.step_height = 0.4;
    const auto character = world->create_character(avatar);
    world->set_character_velocity(character, {2, 0, 0});
    for (int tick = 0; tick < 120; ++tick) world->step(1.0 / 60.0);
    const auto body = world->body_state(cube_id);
    const auto avatar_state = world->character_state(character);
    return {body ? body->position.x - 1.0 : -1.0,
            avatar_state ? avatar_state->position.x : -1.0};
}

bool jolt_character_mass_response_test() {
    const auto light = jolt_character_push_result(125.0);
    const auto heavy = jolt_character_push_result(5000.0);
    return light.body_displacement > 0.04 &&
           light.character_x + 0.4 < 1.0 + light.body_displacement &&
           heavy.body_displacement >= 0.0 && heavy.body_displacement < 0.005 &&
           heavy.character_x + 0.4 < 1.0 + heavy.body_displacement;
}

bool jolt_flying_hover_test() {
    auto world = homeworldz::physics::make_jolt_world();
    homeworldz::physics::BodyDefinition platform;
    platform.entity_id = 1;
    platform.shape.half_extents = {2, 2, 0.125};
    platform.position = {0, 0, 1.125};
    world->create_body(platform);
    const auto character = world->create_character({2, {0, 0, 2.6}, 0.3, 1.8, 0.4});
    world->set_character_flying(character, true);
    world->set_character_velocity(character, {});
    for (int tick = 0; tick < 300; ++tick) world->step(1.0 / 60.0);
    const auto state = world->character_state(character);
    return state && std::abs(state->position.z - 2.6) < 0.001;
}

bool jolt_character_spawn_depenetration_test() {
    auto world = homeworldz::physics::make_jolt_world();
    homeworldz::physics::BodyDefinition platform;
    platform.entity_id = 500;
    platform.shape.half_extents = {2, 2, 0.25};
    platform.position = {10, 20, 4.75};
    world->create_body(platform);

    // Center Z 5.8 puts the 1.8 m capsule's feet at 4.9, slightly inside the
    // platform whose top is 5.0. Creation must preserve X/Y and lift it clear.
    const auto embedded = world->create_character({501, {10, 20, 5.8}, 0.3, 1.8, 0.4});
    const auto recovered = world->character_state(embedded);
    if (!recovered || recovered->position.x != 10 || recovered->position.y != 20 ||
        recovered->position.z <= 5.8)
        return false;

    const auto clear = world->create_character({502, {15, 20, 8}, 0.3, 1.8, 0.4});
    const auto unchanged = world->character_state(clear);
    return unchanged && std::abs(unchanged->position.z - 8.0) < 0.001;
}
}

int main() {
    if (!jolt_heightfield_test()) return 1;
    if (!jolt_walkable_slope_test()) return 1;
    if (!jolt_terrain_bounce_test()) return 1;
    if (!jolt_fast_fall_penetration_test()) return 1;
    if (!jolt_restitution_combine_test()) return 1;
    if (!static_scene_mirror_test()) return 1;
    if (!jolt_compound_linkset_test()) return 1;
    if (!jolt_character_collision_test()) return 1;
    if (!jolt_character_step_test()) return 1;
    if (!jolt_character_mass_response_test()) return 1;
    if (!jolt_flying_hover_test()) return 1;
    if (!jolt_character_spawn_depenetration_test()) return 1;
    if (!smoke_test(homeworldz::physics::make_jolt_world())) return 1;
    if (!smoke_test(homeworldz::physics::make_physx_world())) return 1;
    return 0;
}
