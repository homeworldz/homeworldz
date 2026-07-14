#include "homeworldz/region_config.h"

#include <fstream>
#include <sstream>
#include <stdexcept>

namespace homeworldz::config {
namespace {

std::string trim(std::string_view value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return std::string(value.substr(first, last - first + 1));
}

const std::unordered_map<std::string, std::string> setting_names{
    {"region.name", "HOMEWORLDZ_REGION_NAME"},
    {"region.grid_x", "HOMEWORLDZ_REGION_GRID_X"},
    {"region.grid_y", "HOMEWORLDZ_REGION_GRID_Y"},
    {"region.public_endpoint", "HOMEWORLDZ_REGION_PUBLIC_ENDPOINT"},
    {"region.http_port", "HOMEWORLDZ_REGION_PORT"},
    {"region.viewer_port", "HOMEWORLDZ_VIEWER_PORT"},
    {"region.bind_address", "HOMEWORLDZ_REGION_BIND_ADDRESS"},
    {"region.viewer_bind_address", "HOMEWORLDZ_VIEWER_BIND_ADDRESS"},
    {"region.data_path", "HOMEWORLDZ_REGION_DATA_PATH"},
    {"region.asset_path", "HOMEWORLDZ_REGION_ASSET_PATH"},
    {"region.terrain_path", "HOMEWORLDZ_REGION_TERRAIN_PATH"},
    {"region.lease_seconds", "HOMEWORLDZ_REGION_LEASE_SECONDS"},
    {"grid.url", "HOMEWORLDZ_GRID_URL"},
    {"grid.public_url", "HOMEWORLDZ_GRID_PUBLIC_URL"},
    {"grid.service_token", "HOMEWORLDZ_GRID_SERVICE_TOKEN"},
};

} // namespace

RegionSettings parse_region_ini(std::string_view content) {
    RegionSettings result;
    std::string section;
    std::istringstream input{std::string(content)};
    std::string line;
    std::size_t line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        const auto cleaned = trim(line);
        if (cleaned.empty() || cleaned.front() == '#' || cleaned.front() == ';') continue;
        if (cleaned.front() == '[' && cleaned.back() == ']') {
            section = trim(std::string_view(cleaned).substr(1, cleaned.size() - 2));
            if (section.empty()) throw std::runtime_error("empty INI section at line " + std::to_string(line_number));
            continue;
        }
        const auto equals = cleaned.find('=');
        if (section.empty() || equals == std::string::npos) {
            throw std::runtime_error("invalid INI setting at line " + std::to_string(line_number));
        }
        const auto key = trim(std::string_view(cleaned).substr(0, equals));
        const auto value = trim(std::string_view(cleaned).substr(equals + 1));
        const auto known = setting_names.find(section + '.' + key);
        if (known == setting_names.end()) {
            throw std::runtime_error("unknown region setting at line " + std::to_string(line_number));
        }
        result[known->second] = value;
    }
    return result;
}

RegionSettings load_region_ini(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) {
        if (!std::filesystem::exists(path)) return {};
        throw std::runtime_error("could not open " + path.string());
    }
    std::ostringstream content;
    content << input.rdbuf();
    if (!input.eof() && input.fail()) throw std::runtime_error("could not read " + path.string());
    return parse_region_ini(content.str());
}

} // namespace homeworldz::config
