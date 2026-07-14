#pragma once

#include "homeworldz/scene.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace homeworldz::asset {

struct ObjectAsset {
    scene::Vector3 scale;
    scene::Vector3 rotation;
    std::uint8_t material{};
    std::string description;
};

std::optional<ObjectAsset> parse_object_asset(std::span<const std::byte> content);

} // namespace homeworldz::asset
