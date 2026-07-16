#include "homeworldz/region_config.h"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace homeworldz::config {
namespace {

std::string trim(std::string_view value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return std::string(value.substr(first, last - first + 1));
}

const std::unordered_set<std::string> setting_names{
    "region.name", "region.grid_x", "region.grid_y", "region.public_endpoint",
    "region.http_port", "region.viewer_port", "region.bind_address",
    "region.viewer_bind_address", "region.data_path", "region.asset_path",
    "region.terrain_path", "region.lease_seconds", "grid.url",
    "grid.public_url", "grid.service_token"};

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
        const auto setting = section + '.' + key;
        const auto known = setting_names.find(setting);
        if (known == setting_names.end()) {
            throw std::runtime_error("unknown region setting at line " + std::to_string(line_number));
        }
        result[setting] = value;
    }
    return result;
}

RegionSettings load_region_ini(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("could not open " + path.string());
    std::ostringstream content;
    content << input.rdbuf();
    if (!input.eof() && input.fail()) throw std::runtime_error("could not read " + path.string());
    return parse_region_ini(content.str());
}

} // namespace homeworldz::config
