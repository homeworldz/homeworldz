#pragma once

#include "homeworldz/scene.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace homeworldz::asset {

struct ObjectAsset {
    scene::Vector3 scale;
    scene::Vector3 rotation;
    std::uint8_t material{};
    std::string description;
    std::uint8_t physics_shape_type{};
    double physics_density{1000.0};
    double physics_friction{0.6};
    double physics_restitution{0.5};
    double physics_gravity_multiplier{1.0};
    std::vector<std::byte> texture_entry;
};

std::optional<ObjectAsset> parse_object_asset(std::span<const std::byte> content);

} // namespace homeworldz::asset
