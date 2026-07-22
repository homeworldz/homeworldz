#ifndef HOMEWORLDZ_BAKE_H
#define HOMEWORLDZ_BAKE_H

#include "homeworldz/image.h"
#include "homeworldz/viewer_protocol.h"
#include "homeworldz/wearable.h"

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <vector>

namespace homeworldz::viewer {

// Avatar texture-entry indices (LLAvatarAppearanceDefines::ETextureIndex) used
// as bake inputs and outputs. Only the indices relevant to the classic six
// bakes are named here.
namespace tex_index {
inline constexpr std::uint32_t kHeadBodypaint = 0;   // skin, head
inline constexpr std::uint32_t kUpperShirt = 1;
inline constexpr std::uint32_t kLowerPants = 2;
inline constexpr std::uint32_t kEyesIris = 3;
inline constexpr std::uint32_t kHair = 4;
inline constexpr std::uint32_t kUpperBodypaint = 5;  // skin, upper body
inline constexpr std::uint32_t kLowerBodypaint = 6;  // skin, lower body
inline constexpr std::uint32_t kLowerShoes = 7;
inline constexpr std::uint32_t kHeadBaked = 8;
inline constexpr std::uint32_t kUpperBaked = 9;
inline constexpr std::uint32_t kLowerBaked = 10;
inline constexpr std::uint32_t kEyesBaked = 11;
inline constexpr std::uint32_t kLowerSocks = 12;
inline constexpr std::uint32_t kUpperJacket = 13;
inline constexpr std::uint32_t kLowerJacket = 14;
inline constexpr std::uint32_t kUpperGloves = 15;
inline constexpr std::uint32_t kUpperUndershirt = 16;
inline constexpr std::uint32_t kLowerUnderpants = 17;
inline constexpr std::uint32_t kSkirt = 18;
inline constexpr std::uint32_t kSkirtBaked = 19;
inline constexpr std::uint32_t kHairBaked = 20;
inline constexpr std::uint32_t kHeadTattoo = 26;
inline constexpr std::uint32_t kUpperTattoo = 27;
inline constexpr std::uint32_t kLowerTattoo = 28;
}  // namespace tex_index

// The classic six bake slots (LLAvatarAppearanceDefines::EBakedTextureIndex
// 0..5). Universal/aux bakes are out of Phase 1 scope.
enum class BakeSlot : int {
    Head = 0,
    Upper = 1,
    Lower = 2,
    Eyes = 3,
    Skirt = 4,
    Hair = 5,
};

// Definition of one bake slot: its output resolution and the ordered
// (bottom -> top) source texture-entry indices that composite into it.
struct BakeSlotLayout {
    BakeSlot slot;
    std::uint32_t resolution;
    std::vector<std::uint32_t> source_texture_indices;
};

// The Phase-1 bake layout table (head, upper, lower, eyes, skirt, hair).
const std::vector<BakeSlotLayout>& bake_slot_layouts();

// The avatar texture-entry index (TEX_*_BAKED) that receives a slot's result.
std::uint32_t baked_texture_index(BakeSlot slot);

// Fetches the RGBA image for a texture UUID, or nullopt if unavailable.
using TextureFetch = std::function<std::optional<image::Image>(const Uuid&)>;

// Composite the worn wearables into baked slot images. Worn textures are merged
// by texture-entry index (a later wearable overrides an earlier one for the
// same index); each bake slot composites its source indices in order via the
// image pixel engine. Slots with no available source texture are omitted.
std::map<BakeSlot, image::Image> bake_outfit(const std::vector<Wearable>& worn,
                                             const TextureFetch& fetch);

}  // namespace homeworldz::viewer

#endif  // HOMEWORLDZ_BAKE_H
