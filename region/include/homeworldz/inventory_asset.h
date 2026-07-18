#pragma once

#include "homeworldz/scene.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace homeworldz::inventory {

// Creates the initial asset body for viewer-created inventory types that do not
// begin with a separate upload transaction.
std::optional<std::string> default_asset_content(
    std::int32_t asset_type, std::int32_t inventory_type,
    std::string_view region_id, const scene::Vector3& position);

}  // namespace homeworldz::inventory
