#include "homeworldz/scene.h"
#include "homeworldz/simulation_loop.h"

#include <cmath>
#include <iostream>
#include <vector>

namespace {

bool near(double value, double expected) {
    if (std::abs(value - expected) < 1e-9) return true;
    std::cerr << "value " << value << ", want " << expected << '\n';
    return false;
}

} // namespace

int main() {
    homeworldz::scene::Scene scene;
    const auto id = scene.create("moving object", {1.0, 2.0, 3.0}, {4.0, -2.0, 1.0});
    if (scene.size() != 1 || scene.revision() != 1) return 1;

    homeworldz::simulation::FixedStepLoop loop(scene, 0.02, 4);
    if (loop.advance(0.01) != 0 || !near(loop.interpolation_alpha(), 0.5)) return 1;
    if (loop.advance(0.03) != 2) return 1;
    const auto* entity = scene.find(id);
    if (entity == nullptr || !near(entity->position.x, 1.16) ||
        !near(entity->position.y, 1.92) || !near(entity->position.z, 3.04)) return 1;
    if (scene.simulation_steps() != 2 || scene.revision() != 3) return 1;

    if (loop.advance(10.0) != 4) return 1;
    if (scene.simulation_steps() != 6 || loop.interpolation_alpha() >= 1.0) return 1;
    if (!scene.remove(id) || scene.remove(id) || scene.size() != 0) return 1;

    scene.restore(42, std::vector<homeworldz::scene::Entity>{
        {7, "restored", {9.0, 8.0, 7.0}, {1.0, 2.0, 3.0}}});
    const auto* restored = scene.find(7);
    if (scene.size() != 1 || scene.revision() != 42 || scene.simulation_steps() != 0 ||
        restored == nullptr || restored->name != "restored" || !near(restored->position.y, 8.0)) return 1;
    if (scene.create("after restore") != 8) return 1;
    return 0;
}
