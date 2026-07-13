#pragma once

#include "homeworldz/physics.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace homeworldz::physics {

using WorldFactory = std::function<std::unique_ptr<World>()>;

struct ScenarioResult {
    std::string name;
    bool passed{};
    std::string detail;
    std::size_t simulated_bodies{};
    std::size_t simulated_steps{};
};

std::vector<ScenarioResult> run_common_scenarios(const WorldFactory& factory);

} // namespace homeworldz::physics
