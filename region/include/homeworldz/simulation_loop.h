#pragma once

#include "homeworldz/scene.h"

#include <cstddef>

namespace homeworldz::simulation {

class FixedStepLoop {
public:
    explicit FixedStepLoop(scene::Scene& scene, double step_seconds = 1.0 / 45.0,
                           std::size_t max_catch_up_steps = 8);

    std::size_t advance(double elapsed_seconds);
    double interpolation_alpha() const;
    double step_seconds() const { return step_seconds_; }

private:
    scene::Scene& scene_;
    double step_seconds_;
    double accumulator_{};
    std::size_t max_catch_up_steps_;
};

} // namespace homeworldz::simulation
