#include "homeworldz/physics_adapters.h"
#include "homeworldz/physics_scenarios.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <string_view>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#else
#include <sys/resource.h>
#endif

namespace {
using namespace homeworldz;

struct TickMetrics {
    double total_ms{};
    double worst_ms{};
    std::size_t count{};
};

class TimedWorld final : public physics::World {
public:
    TimedWorld(std::unique_ptr<physics::World> inner, TickMetrics& metrics)
        : inner_(std::move(inner)), metrics_(metrics) {}
    physics::BodyId create_body(const physics::BodyDefinition& value) override { return inner_->create_body(value); }
    bool remove_body(physics::BodyId id) override { return inner_->remove_body(id); }
    std::optional<physics::BodyState> body_state(physics::BodyId id) const override { return inner_->body_state(id); }
    void set_body_state(const physics::BodyState& state) override { inner_->set_body_state(state); }
    void apply_impulse(physics::BodyId id, scene::Vector3 impulse) override { inner_->apply_impulse(id, impulse); }
    physics::CharacterId create_character(const physics::CharacterDefinition& value) override { return inner_->create_character(value); }
    bool remove_character(physics::CharacterId id) override { return inner_->remove_character(id); }
    std::optional<physics::BodyState> character_state(physics::CharacterId id) const override { return inner_->character_state(id); }
    void set_character_state(physics::CharacterId id, const physics::BodyState& state) override { inner_->set_character_state(id, state); }
    void set_character_velocity(physics::CharacterId id, scene::Vector3 value) override { inner_->set_character_velocity(id, value); }
    void step(double seconds) override {
        const auto start = std::chrono::steady_clock::now();
        inner_->step(seconds);
        const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        metrics_.total_ms += elapsed;
        metrics_.worst_ms = std::max(metrics_.worst_ms, elapsed);
        ++metrics_.count;
    }
    std::span<const physics::Contact> contacts() const override { return inner_->contacts(); }
    std::optional<physics::RayHit> ray_cast(scene::Vector3 origin, scene::Vector3 direction,
        double distance) const override { return inner_->ray_cast(origin, direction, distance); }
    std::optional<physics::RayHit> ray_cast_body(physics::BodyId id, scene::Vector3 origin,
        scene::Vector3 direction, double distance) const override {
        return inner_->ray_cast_body(id, origin, direction, distance);
    }
    physics::TransferState capture(std::span<const physics::BodyId> bodies) const override { return inner_->capture(bodies); }
    void restore(const physics::TransferState& state) override { inner_->restore(state); }
private:
    std::unique_ptr<physics::World> inner_;
    TickMetrics& metrics_;
};

std::size_t peak_rss_kib() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS counters{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)))
        return counters.PeakWorkingSetSize / 1024;
    return 0;
#else
    rusage usage{};
    return getrusage(RUSAGE_SELF, &usage) == 0 ? static_cast<std::size_t>(usage.ru_maxrss) : 0;
#endif
}

physics::BodyState replay(const physics::WorldFactory& factory) {
    auto world = factory();
    physics::BodyDefinition floor;
    floor.shape.half_extents = {10, 10, 0.5};
    floor.position = {0, 0, -0.5};
    world->create_body(floor);
    physics::BodyDefinition body;
    body.entity_id = 2;
    body.motion = physics::MotionType::Dynamic;
    body.shape.type = physics::ShapeType::Sphere;
    body.position = {0, 0, 5};
    const auto id = world->create_body(body);
    world->apply_impulse(id, {1, 0.5, 2});
    for (int step = 0; step < 120; ++step) world->step(1.0 / 60.0);
    return *world->body_state(id);
}

double distance(scene::Vector3 first, scene::Vector3 second) {
    const auto x = first.x - second.x;
    const auto y = first.y - second.y;
    const auto z = first.z - second.z;
    return std::sqrt(x * x + y * y + z * z);
}

bool run_backend(std::string_view name, const physics::WorldFactory& backend) {
    std::vector<TickMetrics> metrics;
    physics::WorldFactory timed = [&] {
        metrics.emplace_back();
        return std::make_unique<TimedWorld>(backend(), metrics.back());
    };
    const auto cpu_start = std::clock();
    const auto results = physics::run_common_scenarios(timed);
    const auto cpu_ms = 1000.0 * (std::clock() - cpu_start) / CLOCKS_PER_SEC;
    bool passed = true;
    for (std::size_t index = 0; index < results.size(); ++index) {
        const auto& result = results[index];
        const auto& timing = metrics[index];
        const auto average = timing.count ? timing.total_ms / timing.count : 0.0;
        std::cout << name << ',' << result.name << ',' << (result.passed ? "pass" : "fail") << ','
                  << result.simulated_bodies << ',' << timing.count << ',' << average << ',' << timing.worst_ms << '\n';
        passed = passed && result.passed;
    }
    const auto first = replay(backend);
    const auto second = replay(backend);
    std::cout << "summary," << name << ",cpu_ms," << cpu_ms << ",peak_rss_kib," << peak_rss_kib()
              << ",replay_position_drift," << distance(first.position, second.position)
              << ",transfer_body_bytes," << sizeof(physics::BodyState) << '\n';
    return passed;
}
}

int main(int argc, char** argv) {
    std::cout << std::fixed << std::setprecision(6);
    std::cout << "backend,scenario,status,bodies,steps,average_tick_ms,worst_tick_ms\n";
    const bool run_jolt = argc == 1 || std::string_view(argv[1]) == "jolt";
    const bool run_physx = argc == 1 || std::string_view(argv[1]) == "physx";
    const bool jolt = !run_jolt || run_backend("jolt", physics::make_jolt_world);
    const bool physx = !run_physx || run_backend("physx", physics::make_physx_world);
    if (!run_jolt && !run_physx) {
        std::cerr << "usage: homeworldz-physics-lab [jolt|physx]\n";
        return 2;
    }
    return jolt && physx ? 0 : 1;
}
