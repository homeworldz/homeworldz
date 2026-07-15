#pragma once

#include "homeworldz/viewer_protocol.h"

#include <array>
#include <filesystem>
#include <memory>
#include <vector>

namespace homeworldz::terrain {

using Heightmap = std::array<float, 256 * 256>;

std::unique_ptr<Heightmap> load_state(const std::filesystem::path& path);
bool save_state(const std::filesystem::path& path, const Heightmap& heightmap);
std::vector<viewer::TerrainPatch> apply(Heightmap& heightmap, const Heightmap& revert,
                                        const viewer::ModifyLand& edit);

} // namespace homeworldz::terrain
