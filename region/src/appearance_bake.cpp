#include "homeworldz/appearance_bake.h"

#include "homeworldz/bake.h"
#include "homeworldz/image.h"
#include "homeworldz/sha256.h"
#include "homeworldz/wearable.h"

#include <cstring>
#include <string>

namespace homeworldz::viewer {
namespace {

std::vector<std::uint8_t> to_bytes_u8(const std::vector<std::byte>& in) {
    std::vector<std::uint8_t> out(in.size());
    if (!in.empty()) std::memcpy(out.data(), in.data(), in.size());
    return out;
}

std::vector<std::byte> to_bytes_std(const std::vector<std::uint8_t>& in) {
    std::vector<std::byte> out(in.size());
    if (!in.empty()) std::memcpy(out.data(), in.data(), in.size());
    return out;
}

// Derive a deterministic viewer UUID from content so identical bakes share an
// id (content-addressed at the handle level too).
Uuid content_uuid(const std::vector<std::byte>& content) {
    const std::string hex = crypto::sha256_hex(content);
    const std::string dashed = hex.substr(0, 8) + "-" + hex.substr(8, 4) + "-" +
                               hex.substr(12, 4) + "-" + hex.substr(16, 4) + "-" +
                               hex.substr(20, 12);
    return parse_uuid(dashed).value();
}

}  // namespace

std::optional<OutfitBake> bake_worn_outfit(const std::vector<Uuid>& wearable_asset_ids,
                                           const Uuid& default_id, const AssetBytesFetch& fetch) {
    std::vector<Wearable> worn;
    for (const Uuid& id : wearable_asset_ids) {
        auto bytes = fetch(id);
        if (!bytes || bytes->empty()) continue;
        const std::string text(reinterpret_cast<const char*>(bytes->data()), bytes->size());
        if (auto parsed = parse_wearable(text)) worn.push_back(std::move(*parsed));
    }
    if (worn.empty()) return std::nullopt;

    const TextureFetch texture_fetch = [&](const Uuid& id) -> std::optional<image::Image> {
        auto bytes = fetch(id);
        if (!bytes || bytes->empty()) return std::nullopt;
        return image::decode_j2c(to_bytes_u8(*bytes));
    };

    std::map<BakeSlot, image::Image> baked = bake_outfit(worn, texture_fetch);
    if (baked.empty()) return std::nullopt;

    OutfitBake result;
    result.faces.fill(default_id);
    for (auto& [slot, slot_image] : baked) {
        auto encoded = image::encode_j2c(slot_image);
        if (!encoded) continue;
        std::vector<std::byte> j2c = to_bytes_std(*encoded);
        const Uuid id = content_uuid(j2c);
        const auto index = static_cast<std::uint8_t>(baked_texture_index(slot));
        result.faces[index] = id;
        result.assets.push_back(OutfitBake::BakedAsset{id, index, std::move(j2c)});
    }
    if (result.assets.empty()) return std::nullopt;

    result.texture_entry = encode_avatar_texture_entry(result.faces, default_id);
    return result;
}

}  // namespace homeworldz::viewer
