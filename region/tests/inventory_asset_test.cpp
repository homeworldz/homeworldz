#include "homeworldz/inventory_asset.h"

#include <cassert>
#include <string>

int main() {
    using homeworldz::inventory::default_asset_content;
    const homeworldz::scene::Vector3 position{12.5, 34.25, 56.0};
    const std::string region_id = "6720c401-788d-4d1a-b97c-e193c42e39b9";

    const auto landmark = default_asset_content(3, 3, region_id, position);
    assert(landmark);
    assert(landmark->find("Landmark version 2\n") == 0);
    assert(landmark->find("region_id " + region_id) != std::string::npos);
    assert(landmark->find("local_pos 12.500000 34.250000 56.000000") != std::string::npos);

    const auto notecard = default_asset_content(7, 7, region_id, position);
    assert(notecard && notecard->find("Linden text version 2\n") == 0);
    assert(notecard->find("Text length 0\n") != std::string::npos);

    const auto script = default_asset_content(10, 10, region_id, position);
    assert(script && script->find("default\n{") == 0);
    assert(script->find("llSay(0, \"Hello, Avatar!\");") != std::string::npos);

    const auto gesture = default_asset_content(21, 20, region_id, position);
    assert(gesture && *gesture == "2\n0\n0\n\n\n0\n");
    assert(!default_asset_content(0, 0, region_id, position));
}
