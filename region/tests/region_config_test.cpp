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
    if (settings.size() != 15 || settings.at("HOMEWORLDZ_REGION_NAME") != "Demo Region" ||
        settings.at("HOMEWORLDZ_REGION_GRID_X") != "1001" ||
        settings.at("HOMEWORLDZ_REGION_GRID_Y") != "1002" ||
        settings.at("HOMEWORLDZ_REGION_PUBLIC_ENDPOINT") != "http://region.example:42001" ||
        settings.at("HOMEWORLDZ_REGION_PORT") != "42001" ||
        settings.at("HOMEWORLDZ_VIEWER_PORT") != "42002" ||
        settings.at("HOMEWORLDZ_REGION_BIND_ADDRESS") != "0.0.0.0" ||
        settings.at("HOMEWORLDZ_VIEWER_BIND_ADDRESS") != "0.0.0.0" ||
        settings.at("HOMEWORLDZ_REGION_DATA_PATH") != "D:\\HomeWorldz\\region" ||
        settings.at("HOMEWORLDZ_REGION_ASSET_PATH") != "assets/region" ||
        settings.at("HOMEWORLDZ_REGION_TERRAIN_PATH") != "assets/region/terrain/plateau-square.raw" ||
        settings.at("HOMEWORLDZ_REGION_LEASE_SECONDS") != "60" ||
        settings.at("HOMEWORLDZ_GRID_URL") != "https://grid.example" ||
        settings.at("HOMEWORLDZ_GRID_PUBLIC_URL") != "https://grid.example" ||
        settings.at("HOMEWORLDZ_GRID_SERVICE_TOKEN") != "secret")
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
