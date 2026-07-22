#include "homeworldz/bake.h"

namespace homeworldz::viewer {

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
    // Merge worn textures by texture-entry index (later wearable wins).
    std::map<std::uint32_t, Uuid> worn_textures;
    for (const Wearable& w : worn) {
        for (const auto& [index, id] : w.textures) worn_textures[index] = id;
    }

    std::map<BakeSlot, image::Image> baked;
    for (const BakeSlotLayout& layout : bake_slot_layouts()) {
        std::vector<image::Layer> layers;
        for (std::uint32_t source : layout.source_texture_indices) {
            const auto it = worn_textures.find(source);
            if (it == worn_textures.end()) continue;
            std::optional<image::Image> texture = fetch(it->second);
            if (!texture || texture->empty()) continue;
            layers.push_back(image::Layer{std::move(*texture), {255, 255, 255}});
        }
        if (layers.empty()) continue;
        image::Image slot_image =
            image::composite_rgba(layout.resolution, layout.resolution, layers);
        if (!slot_image.empty()) baked.emplace(layout.slot, std::move(slot_image));
    }
    return baked;
}

}  // namespace homeworldz::viewer
