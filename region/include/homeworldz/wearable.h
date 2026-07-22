#ifndef HOMEWORLDZ_WEARABLE_H
#define HOMEWORLDZ_WEARABLE_H

#include "homeworldz/viewer_protocol.h"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace homeworldz::viewer {

// Second Life wearable types (LLWearableType). Body parts (shape/skin/hair/
// eyes) define the base avatar; clothing layers composite on top of them.
enum class WearableType : int {
    Shape = 0,
    Skin = 1,
    Hair = 2,
    Eyes = 3,
    Shirt = 4,
    Pants = 5,
    Shoes = 6,
    Socks = 7,
    Jacket = 8,
    Gloves = 9,
    Undershirt = 10,
    Underpants = 11,
    Skirt = 12,
    Alpha = 13,
    Tattoo = 14,
    Physics = 15,
    Universal = 16,
    Invalid = -1,
};

// A parsed LLWearable (.bodypart / .clothing) asset: the worn layer's type, its
// visual parameters (id -> value, which include color/tint and alpha params),
// and its textures keyed by avatar texture-entry index (ETextureIndex).
struct Wearable {
    int version = 0;
    std::string name;
    WearableType type = WearableType::Invalid;
    std::map<std::uint32_t, double> parameters;
    std::map<std::uint32_t, Uuid> textures;  // texture-entry index -> texture UUID
};

// Parse the LLWearable text asset format. Returns nullopt when the header is not
// a recognizable LLWearable or the type is absent/malformed. Permissions and
// sale-info blocks are skipped; unknown top-level tokens are ignored.
std::optional<Wearable> parse_wearable(std::string_view text);

}  // namespace homeworldz::viewer

#endif  // HOMEWORLDZ_WEARABLE_H
