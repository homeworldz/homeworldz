#include "homeworldz/inventory_asset.h"

#include <iomanip>
#include <sstream>

namespace homeworldz::inventory {

std::optional<std::string> default_asset_content(
    const std::int32_t asset_type, const std::int32_t inventory_type,
    const std::string_view region_id, const scene::Vector3& position) {
    if (asset_type == 3 && inventory_type == 3) {
        std::ostringstream content;
        content << "Landmark version 2\nregion_id " << region_id
                << "\nlocal_pos " << std::fixed << std::setprecision(6)
                << position.x << ' ' << position.y << ' ' << position.z << "\n";
        return content.str();
    }
    if (asset_type == 7 && inventory_type == 7) {
        return "Linden text version 2\n"
               "{\n"
               "LLEmbeddedItems version 1\n"
               "{\n"
               "count 0\n"
               "}\n"
               "Text length 0\n"
               "\n"
               "}\n";
    }
    if (asset_type == 10 && inventory_type == 10) {
        return "default\n"
               "{\n"
               "    state_entry()\n"
               "    {\n"
               "        llSay(0, \"Hello, Avatar!\");\n"
               "    }\n"
               "}\n";
    }
    if (asset_type == 21 && inventory_type == 20) {
        // Gesture asset version, trigger key, modifier mask, trigger text,
        // replacement text, and an empty sequence.
        return "2\n0\n0\n\n\n0\n";
    }
    return std::nullopt;
}

}  // namespace homeworldz::inventory
