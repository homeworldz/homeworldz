#include "homeworldz/bake.h"

#include <array>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

using homeworldz::image::Image;
using homeworldz::viewer::bake_outfit;
using homeworldz::viewer::baked_texture_index;
using homeworldz::viewer::BakeSlot;
using homeworldz::viewer::format_uuid;
using homeworldz::viewer::parse_uuid;
using homeworldz::viewer::Wearable;
using homeworldz::viewer::WearableType;
namespace tx = homeworldz::viewer::tex_index;

namespace {

const char* const kSkinHead = "aaaaaaaa-0000-0000-0000-000000000001";
const char* const kSkinUpper = "aaaaaaaa-0000-0000-0000-000000000002";
const char* const kSkinLower = "aaaaaaaa-0000-0000-0000-000000000003";
const char* const kPantsTex = "bbbbbbbb-0000-0000-0000-000000000001";

Image solid(std::uint32_t size, std::array<std::uint8_t, 4> rgba) {
    Image img;
    img.width = size;
    img.height = size;
    img.channels = 4;
    img.pixels.resize(img.expected_size());
    for (std::size_t i = 0; i < img.pixel_count(); ++i) {
        img.pixels[i * 4 + 0] = rgba[0];
        img.pixels[i * 4 + 1] = rgba[1];
        img.pixels[i * 4 + 2] = rgba[2];
        img.pixels[i * 4 + 3] = rgba[3];
    }
    return img;
}

Wearable make_wearable(WearableType type,
                       std::vector<std::pair<std::uint32_t, const char*>> textures) {
    Wearable w;
    w.type = type;
    for (auto& [index, uuid] : textures) w.textures[index] = *parse_uuid(uuid);
    return w;
}

bool expect(bool ok, const char* what) {
    if (!ok) std::cerr << "FAIL: " << what << '\n';
    return ok;
}

}  // namespace

int main() {
    bool ok = true;

    // A skin body part supplies the three body-region skin textures; opaque
    // blue pants clothe the lower body on top of red lower skin.
    std::vector<Wearable> worn = {
        make_wearable(WearableType::Skin,
                      {{tx::kHeadBodypaint, kSkinHead},
                       {tx::kUpperBodypaint, kSkinUpper},
                       {tx::kLowerBodypaint, kSkinLower}}),
        make_wearable(WearableType::Pants, {{tx::kLowerPants, kPantsTex}}),
    };

    auto fetch = [&](const homeworldz::viewer::Uuid& id) -> std::optional<Image> {
        const std::string s = format_uuid(id);
        if (s == kSkinHead) return solid(64, {200, 180, 160, 255});
        if (s == kSkinUpper) return solid(64, {200, 180, 160, 255});
        if (s == kSkinLower) return solid(64, {255, 0, 0, 255});
        if (s == kPantsTex) return solid(64, {0, 0, 255, 255});
        return std::nullopt;
    };

    auto baked = bake_outfit(worn, fetch);

    // Head, upper, and lower slots should bake; eyes/skirt/hair have no worn
    // texture and must be absent.
    ok &= expect(baked.count(BakeSlot::Head) == 1, "head baked");
    ok &= expect(baked.count(BakeSlot::Upper) == 1, "upper baked");
    ok &= expect(baked.count(BakeSlot::Lower) == 1, "lower baked");
    ok &= expect(baked.count(BakeSlot::Eyes) == 0, "no eyes bake without iris texture");
    ok &= expect(baked.count(BakeSlot::Skirt) == 0, "no skirt bake");
    ok &= expect(baked.count(BakeSlot::Hair) == 0, "no hair bake");

    if (baked.count(BakeSlot::Lower)) {
        const Image& lower = baked.at(BakeSlot::Lower);
        ok &= expect(lower.width == 512 && lower.height == 512, "lower bake is 512x512");
        // Opaque pants sit on top of skin, so the lower bake is blue.
        ok &= expect(lower.pixels[2] > 200 && lower.pixels[0] < 50,
                     "lower bake shows opaque pants over skin");
    }
    if (baked.count(BakeSlot::Head)) {
        const Image& head = baked.at(BakeSlot::Head);
        ok &= expect(head.pixels[0] > 150 && head.pixels[2] < 200, "head bake is skin");
    }

    ok &= expect(baked_texture_index(BakeSlot::Lower) == tx::kLowerBaked,
                 "lower -> TEX_LOWER_BAKED");
    ok &= expect(baked_texture_index(BakeSlot::Eyes) == tx::kEyesBaked,
                 "eyes -> TEX_EYES_BAKED");

    if (!ok) return 1;
    std::cerr << "bake outfit OK\n";
    return 0;
}
