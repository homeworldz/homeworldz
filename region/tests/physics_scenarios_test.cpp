#include "homeworldz/physics_adapters.h"
#include "homeworldz/physics_scenarios.h"

#include <iostream>

namespace {
bool run(const char* backend, const homeworldz::physics::WorldFactory& factory) {
    bool passed = true;
    for (const auto& result : homeworldz::physics::run_common_scenarios(factory)) {
        if (!result.passed) {
            std::cerr << backend << ' ' << result.name << ": " << result.detail << '\n';
            passed = false;
        }
    }
    return passed;
}
}

int main() {
    const bool jolt = run("jolt", homeworldz::physics::make_jolt_world);
    const bool physx = run("physx", homeworldz::physics::make_physx_world);
    return jolt && physx ? 0 : 1;
}
