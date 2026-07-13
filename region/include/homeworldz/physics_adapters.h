#pragma once

#include "homeworldz/physics.h"

#include <memory>

namespace homeworldz::physics {

std::unique_ptr<World> make_jolt_world();
std::unique_ptr<World> make_physx_world();

} // namespace homeworldz::physics
