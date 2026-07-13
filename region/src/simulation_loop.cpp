#include "homeworldz/simulation_loop.h"

#include <algorithm>
#include <stdexcept>

namespace homeworldz::simulation {

FixedStepLoop::FixedStepLoop(scene::Scene& scene, double step_seconds,
                             std::size_t max_catch_up_steps)
    : scene_(scene), step_seconds_(step_seconds), max_catch_up_steps_(max_catch_up_steps) {
    if (step_seconds_ <= 0.0 || max_catch_up_steps_ == 0) {
        throw std::invalid_argument("fixed-step settings must be positive");
    }
}

std::size_t FixedStepLoop::advance(double elapsed_seconds) {
    if (elapsed_seconds <= 0.0) return 0;
    const auto maximum = step_seconds_ * static_cast<double>(max_catch_up_steps_);
    accumulator_ += std::min(elapsed_seconds, maximum);
    std::size_t steps = 0;
    const auto epsilon = step_seconds_ * 1e-12;
    while (accumulator_ + epsilon >= step_seconds_ && steps < max_catch_up_steps_) {
        scene_.step(step_seconds_);
        accumulator_ = std::max(0.0, accumulator_ - step_seconds_);
        ++steps;
    }
    return steps;
}

double FixedStepLoop::interpolation_alpha() const {
    return accumulator_ / step_seconds_;
}

} // namespace homeworldz::simulation
