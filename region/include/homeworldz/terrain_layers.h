#pragma once

// The four ground textures and the elevation band they are selected by.
//
// One definition, read by both the viewer's RegionHandshake and the session
// hello. It is a header rather than two literals because the same fact written
// in two places is how a capability came to be advertised at one path and
// handled at another, and how a texture rule came to be enforced on one of two
// capabilities that had to obey it (both found live 2026-07-31). A client and a
// viewer standing on the same ground must at least be told the same ids.
//
// **The values below are defaults, not constants.** Each region starts at them
// and an operator may change its own through the viewer's Region/Estate →
// Terrain tab; `Settings` at the foot of this file is one region's live copy,
// persisted and reloaded. Whether a region still holds the defaults is itself
// published, so a client reads the fact rather than assuming it.
//
// **Which layer applies at a height is published; how two layers blend into
// each other is only partly.** The selection rule is exact and stated. The
// transition width is a region setting (`region.terrain_blend_tenths`) that only
// a client shading its own terrain can honour — no legacy message carries a
// blend width, so a viewer computes its own, including a noise term never
// reproduced outside Linden. Two families will therefore differ subtly on the
// same ground however much is published, which is worth knowing in advance
// rather than discovering as a discrepancy.

#include <array>
#include <string>
#include <string_view>

namespace homeworldz::terrain {

// Lowest to highest: dirt, grass, mountain, rock. 1024x1024 PNG canonicals from
// ambientCG, CC0 — so the grid's default ground carries no attribution or
// share-alike obligation at all, which the previous set did (it was a
// generative-AI upscale of Second Life viewer artwork, and CC BY-SA followed the
// derivative). See assets/region/library/terrain/LICENSE.md.
inline constexpr std::array<std::string_view, 4> layer_assets{
    "e0a13caa-d4dc-4efe-8605-bfbf8411177b",  // Ground079L
    "150cbea9-6ecf-4d31-aeae-0fddbda311b5",  // Grass005
    "97a0e146-6773-4d69-8cf9-263758a430ca",  // Rock060
    "1437e5fc-e6f4-4f00-8247-2395ad489b0e"}; // Rock002, alpha removed

// The two elevations that select between the four layers, per corner, in the
// order the handshake writes them: south-west, north-west, south-east,
// north-east. Corner values are interpolated across the region.
//
// **Both are absolute heights in metres**, not a start and a span. The viewer's
// Region/Estate → Terrain tab states the semantics outright — "the LOW value is
// the MAXIMUM height of Texture #1, and the HIGH value is the MINIMUM height of
// Texture #4" — and it displays exactly what the region sends: with 10 and 60 on
// the wire it shows Low 10, High 60, where a start-plus-range reading would have
// displayed 70. The protocol's historical field names say start height and
// height range; the meaning is low and high.
//
//     layer 1   below low
//     layer 2   low .. midpoint
//     layer 3   midpoint .. high
//     layer 4   above high
//
// So the defaults below put layer 1 entirely below 10 m. With water at 20 m that
// is wholly submerged, and no dry ground can show it — which is why the operator
// could see three layers and not the first (2026-08-04). Raising low above the
// waterline is what makes layer 1 visible, and that is an operator decision
// rather than a default worth changing blind.
inline constexpr std::array<float, 4> layer_low_height{10.0F, 10.0F, 10.0F, 10.0F};
inline constexpr std::array<float, 4> layer_high_height{60.0F, 60.0F, 60.0F, 60.0F};

// Where layer 2 gives way to layer 3: halfway between low and high, which is the
// only division the two bounds determine on their own.
inline constexpr float layer_midpoint(float low, float high) { return (low + high) * 0.5F; }

// One region's live layer settings: what the constants above start it at, and
// what an operator may have changed since through the viewer's Region/Estate →
// Terrain tab. The values are per region from here on, so `gridWide` in the
// hello is no longer a constant either — it reports whether this region still
// holds the defaults.
struct Settings {
    std::array<std::string, 4> assets{
        std::string(layer_assets[0]), std::string(layer_assets[1]),
        std::string(layer_assets[2]), std::string(layer_assets[3])};
    std::array<float, 4> low{layer_low_height};
    std::array<float, 4> high{layer_high_height};

    // True while nothing has been changed from the shipped defaults. Published
    // rather than assumed: the day a region differs, a client that was told
    // notices and one that hardcoded does not.
    bool matches_defaults() const {
        for (std::size_t index = 0; index < 4; ++index)
            if (assets[index] != layer_assets[index] ||
                low[index] != layer_low_height[index] ||
                high[index] != layer_high_height[index])
                return false;
        return true;
    }
};

} // namespace homeworldz::terrain
