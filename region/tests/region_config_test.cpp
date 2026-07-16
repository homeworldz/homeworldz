#include "homeworldz/region_config.h"

#include <stdexcept>
#include <string_view>

int main() {
    constexpr std::string_view input = R"ini(
# packaged region configuration
[region]
name = Demo Region
grid_x = 1001
grid_y = 1002
public_endpoint = http://region.example:42001
http_port = 42001
viewer_port = 42002
bind_address = 0.0.0.0
viewer_bind_address = 0.0.0.0
data_path = D:\HomeWorldz\region
asset_path = assets/region
terrain_path = assets/region/terrain/plateau-square.raw
lease_seconds = 60

[grid]
url = https://grid.example
public_url = https://grid.example
service_token = secret
)ini";
    const auto settings = homeworldz::config::parse_region_ini(input);
    if (settings.size() != 15 || settings.at("region.name") != "Demo Region" ||
        settings.at("region.grid_x") != "1001" || settings.at("region.grid_y") != "1002" ||
        settings.at("region.public_endpoint") != "http://region.example:42001" ||
        settings.at("region.http_port") != "42001" || settings.at("region.viewer_port") != "42002" ||
        settings.at("region.bind_address") != "0.0.0.0" ||
        settings.at("region.viewer_bind_address") != "0.0.0.0" ||
        settings.at("region.data_path") != "D:\\HomeWorldz\\region" ||
        settings.at("region.asset_path") != "assets/region" ||
        settings.at("region.terrain_path") != "assets/region/terrain/plateau-square.raw" ||
        settings.at("region.lease_seconds") != "60" ||
        settings.at("grid.url") != "https://grid.example" ||
        settings.at("grid.public_url") != "https://grid.example" ||
        settings.at("grid.service_token") != "secret")
        return 1;
    try {
        static_cast<void>(homeworldz::config::parse_region_ini("[region]\nunknown = value\n"));
        return 1;
    } catch (const std::runtime_error&) {
    }
    try {
        static_cast<void>(homeworldz::config::parse_region_ini("name = missing-section\n"));
        return 1;
    } catch (const std::runtime_error&) {
    }
    return 0;
}
