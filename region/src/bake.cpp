#include "homeworldz/bake.h"

#include <array>

namespace homeworldz::viewer {
namespace {

// Hair color is an SL "global color" (LLTexGlobalColor "hair_color"): several
// color params, each mapping its 0..1 value across a color ramp, summed (the
// Add operation) into one tint. Ramps are from avatar_lad.xml. The default hair
// is Blonde (114) = .5, which lands mid-ramp at a medium brown (90,53,16).
struct HairColorParam {
    std::uint32_t id;
    std::vector<std::array<int, 3>> ramp;  // ordered stops, value 0..1 across them
};

const std::array<HairColorParam, 4>& hair_color_params() {
    static const std::array<HairColorParam, 4> params = {{
        {114,  // Blonde
         {{0, 0, 0}, {22, 6, 6}, {29, 9, 6}, {45, 21, 11}, {78, 39, 11}, {90, 53, 16},
          {136, 92, 21}, {150, 106, 33}, {198, 156, 74}, {233, 192, 103}, {238, 205, 136}}},
        {113, {{0, 0, 0}, {118, 47, 19}}},                          // Red
        {115, {{0, 0, 0}, {255, 255, 255}}},                        // White
        {112,                                                        // Rainbow
         {{0, 0, 0}, {255, 0, 255}, {255, 0, 0}, {255, 255, 0}, {0, 255, 0}, {0, 255, 255},
          {0, 0, 255}, {255, 0, 255}}},
    }};
    return params;
}

// Interpolate a color ramp at value in [0,1], matching LMV's GetColorFromParams.
std::array<double, 3> ramp_color(const std::vector<std::array<int, 3>>& ramp, double value) {
    const int n = static_cast<int>(ramp.size());
    if (n == 0) return {0, 0, 0};
    if (n == 1) return {double(ramp[0][0]), double(ramp[0][1]), double(ramp[0][2])};
    const double step = 1.0 / (n - 1);
    int a = 0;
    for (int i = 0; i < n; ++i) {
        if (i * step <= value) a = i; else break;
    }
    const int b = (a == n - 1) ? a : a + 1;
    const double distance = value - a * step;
    const auto& c1 = ramp[a];
    const auto& c2 = ramp[b];
    if (distance < 1e-5 || a == b) return {double(c1[0]), double(c1[1]), double(c1[2])};
    const double t = distance / step;
    return {c1[0] + (c2[0] - c1[0]) * t, c1[1] + (c2[1] - c1[1]) * t, c1[2] + (c2[2] - c1[2]) * t};
}

std::array<std::uint8_t, 3> hair_tint(const Wearable& w) {
    std::array<double, 3> res = {0, 0, 0};
    for (const auto& p : hair_color_params()) {
        const auto it = w.parameters.find(p.id);
        if (it == w.parameters.end()) continue;
        double v = it->second;
        v = v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v);
        const auto c = ramp_color(p.ramp, v);
        res[0] += c[0];  // Add operation (global hair color)
        res[1] += c[1];
        res[2] += c[2];
    }
    const auto clamp8 = [](double x) -> std::uint8_t {
        x = x < 0.0 ? 0.0 : (x > 255.0 ? 255.0 : x);
        return static_cast<std::uint8_t>(x + 0.5);
    };
    return {clamp8(res[0]), clamp8(res[1]), clamp8(res[2])};
}

// SL clothing color is carried as three visual params (red, green, blue), each
// 0..1, that tint the layer's texture. The default outfit relies on this: its
// shirt and pants both use the opaque white "Blank" texture and are colored
// entirely by these params (shirt grey 803/804/805 = .5/.5/.6, pants reddish
// 806/807/808 = .8/.2/.2). Hair uses the global-color ramp above; other body
// parts (skin/shape/eyes) carry no such params and render untinted.
std::array<std::uint8_t, 3> wearable_tint(const Wearable& w) {
    if (w.type == WearableType::Hair) return hair_tint(w);
    struct ColorParams {
        WearableType type;
        std::uint32_t r, g, b;
    };
    static const std::array<ColorParams, 9> table = {{
        {WearableType::Shirt, 803, 804, 805},
        {WearableType::Pants, 806, 807, 808},
        {WearableType::Shoes, 812, 813, 817},
        {WearableType::Socks, 818, 819, 820},
        {WearableType::Jacket, 834, 835, 836},
        {WearableType::Gloves, 827, 829, 830},
        {WearableType::Undershirt, 821, 822, 823},
        {WearableType::Underpants, 824, 825, 826},
        {WearableType::Skirt, 921, 922, 923},
    }};
    const auto channel = [&](std::uint32_t id) -> std::uint8_t {
        const auto it = w.parameters.find(id);
        double v = (it == w.parameters.end()) ? 1.0 : it->second;
        v = v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v);
        return static_cast<std::uint8_t>(v * 255.0 + 0.5);
    };
    for (const auto& c : table) {
        if (c.type == w.type) return {channel(c.r), channel(c.g), channel(c.b)};
    }
    return {255, 255, 255};
}

}  // namespace

const std::vector<BakeSlotLayout>& bake_slot_layouts() {
    namespace tx = tex_index;
    // Composite order is bottom -> top: skin, then tattoo, then under layers,
    // the main garment, and finally the jacket on top. Only layers a user
    // actually wears contribute; the default six-wearable outfit exercises skin
    // plus one garment per body region.
    static const std::vector<BakeSlotLayout> layouts = {
        {BakeSlot::Head, 512, {tx::kHeadBodypaint, tx::kHeadTattoo}},
        {BakeSlot::Upper,
         512,
         {tx::kUpperBodypaint, tx::kUpperTattoo, tx::kUpperUndershirt, tx::kUpperGloves,
          tx::kUpperShirt, tx::kUpperJacket}},
        {BakeSlot::Lower,
         512,
         {tx::kLowerBodypaint, tx::kLowerTattoo, tx::kLowerUnderpants, tx::kLowerSocks,
          tx::kLowerShoes, tx::kLowerPants, tx::kLowerJacket}},
        {BakeSlot::Eyes, 128, {tx::kEyesIris}},
        {BakeSlot::Skirt, 512, {tx::kSkirt}},
        {BakeSlot::Hair, 512, {tx::kHair}},
    };
    return layouts;
}

std::uint32_t baked_texture_index(BakeSlot slot) {
    switch (slot) {
        case BakeSlot::Head:
            return tex_index::kHeadBaked;
        case BakeSlot::Upper:
            return tex_index::kUpperBaked;
        case BakeSlot::Lower:
            return tex_index::kLowerBaked;
        case BakeSlot::Eyes:
            return tex_index::kEyesBaked;
        case BakeSlot::Skirt:
            return tex_index::kSkirtBaked;
        case BakeSlot::Hair:
            return tex_index::kHairBaked;
    }
    return tex_index::kHeadBaked;
}

std::map<BakeSlot, image::Image> bake_outfit(const std::vector<Wearable>& worn,
                                             const TextureFetch& fetch) {
    // Merge worn textures by texture-entry index (later wearable wins), carrying
    // each source wearable's color tint so the layer is colored on composite.
    struct WornTexture {
        Uuid id;
        std::array<std::uint8_t, 3> tint;
    };
    std::map<std::uint32_t, WornTexture> worn_textures;
    for (const Wearable& w : worn) {
        const auto tint = wearable_tint(w);
        for (const auto& [index, id] : w.textures) worn_textures[index] = {id, tint};
    }

    std::map<BakeSlot, image::Image> baked;
    for (const BakeSlotLayout& layout : bake_slot_layouts()) {
        std::vector<image::Layer> layers;
        for (std::uint32_t source : layout.source_texture_indices) {
            const auto it = worn_textures.find(source);
            if (it == worn_textures.end()) continue;
            std::optional<image::Image> texture = fetch(it->second.id);
            if (!texture || texture->empty()) continue;
            layers.push_back(image::Layer{std::move(*texture), it->second.tint});
        }
        if (layers.empty()) continue;
        image::Image slot_image =
            image::composite_rgba(layout.resolution, layout.resolution, layers);
        if (!slot_image.empty()) baked.emplace(layout.slot, std::move(slot_image));
    }
    return baked;
}

}  // namespace homeworldz::viewer
