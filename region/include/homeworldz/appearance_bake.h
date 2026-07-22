#ifndef HOMEWORLDZ_APPEARANCE_BAKE_H
#define HOMEWORLDZ_APPEARANCE_BAKE_H

#include "homeworldz/viewer_protocol.h"
#include "homeworldz/wearable.h"

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace homeworldz::viewer {

// A completed server-side outfit bake, ready to publish: the per-face texture
// UUIDs, the encoded TextureEntry, and the baked slot textures to persist.
struct OutfitBake {
    std::array<Uuid, 32> faces;           // baked UUIDs at bake slots, default_id elsewhere
    std::vector<std::byte> texture_entry;  // encode_avatar_texture_entry(faces, default_id)

    struct BakedAsset {
        Uuid id;                       // content-derived viewer UUID for this bake
        std::uint8_t texture_index;    // TEX_*_BAKED index the bake occupies
        std::vector<std::byte> j2c;    // encoded JPEG2000 bytes to store
    };
    std::vector<BakedAsset> assets;
    std::vector<Wearable> worn;  // the parsed wearables (for visual-param assembly)
};

// Fetches the raw bytes of an asset (wearable or texture) by UUID, or nullopt.
using AssetBytesFetch = std::function<std::optional<std::vector<std::byte>>(const Uuid&)>;

// Bake a worn outfit into the classic bake slots and assemble a TextureEntry.
// Each wearable asset is fetched + parsed; its layer textures are fetched and
// JPEG2000-decoded; the slots are composited and re-encoded to JPEG2000. Baked
// texture UUIDs are content-derived (sha256) so identical outfits dedupe.
// default_id fills faces with no bake. Returns nullopt if nothing baked.
std::optional<OutfitBake> bake_worn_outfit(const std::vector<Uuid>& wearable_asset_ids,
                                           const Uuid& default_id, const AssetBytesFetch& fetch);

}  // namespace homeworldz::viewer

#endif  // HOMEWORLDZ_APPEARANCE_BAKE_H
