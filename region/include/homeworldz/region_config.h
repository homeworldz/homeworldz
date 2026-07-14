#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>

namespace homeworldz::config {

using RegionSettings = std::unordered_map<std::string, std::string>;

RegionSettings parse_region_ini(std::string_view content);
RegionSettings load_region_ini(const std::filesystem::path& path);

} // namespace homeworldz::config
