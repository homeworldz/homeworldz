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

// The two numbers that select between the four layers, per corner, in the order
// the handshake writes them: south-west, north-west, south-east, north-east.
// Corner values are interpolated bilinearly across the region.
//
// **A start height and a height range, not two absolute bounds.** The viewer
// computes a composition value from them and blends the four layers by it
// (`llvlcomposition.cpp`):
//
//     t = clamp((height + noise - start) * 4 / range, 0, 3)
//
// Layer 1 is pure at t = 0 and layer 4 at t = 3, with linear blending between
// neighbours, so the boundaries sit where t is a half integer:
//
//     1 | 2   at start + 0.125 * range
//     2 | 3   at start + 0.375 * range
//     3 | 4   at start + 0.625 * range
//     layer 4 pure from start + 0.75 * range upward
//
// **This reading was retracted on 2026-08-04 and then restored the same day, and
// the reason is worth keeping.** The viewer's Region/Estate dialog labels the two
// spinners Low and High and states outright that "the LOW value is the MAXIMUM
// height of Texture #1, and the HIGH value is the MINIMUM height of Texture #4".
// That text describes a model its own renderer does not implement. The widgets are
// named `height_start_spin_N` and `height_range_spin_N`, are filled from
// `getStartHeight()` and `getHeightRange()`, and the arithmetic above is what
// decides what an operator sees. The help text was believed over the source, which
// was vendored in this repository the whole time; an operator's screenshot of sand
// at 22 m with start 20 and range 60 is what caught it. t there is 0.13 - layer 1
// almost pure - and the numbers were doing exactly what they say.
//
// So: to put boundaries at 20, 40 and 60 an operator wants start 10, range 80, not
// 20 and 60. The lesson recorded rather than the number.
inline constexpr std::array<float, 4> layer_start_height{10.0F, 10.0F, 10.0F, 10.0F};
inline constexpr std::array<float, 4> layer_height_range{60.0F, 60.0F, 60.0F, 60.0F};

// The viewer's composition value for a height, without its noise term. Published
// as a rule rather than shipped as a shading function: the noise is a turbulence
// sum no one has reproduced outside Linden, so a client matching this exactly
// still differs from a viewer by a few metres of patchiness.
inline constexpr float layer_composition_value(float height, float start, float range) {
    if (range <= 0.0F) return 0.0F;  // a zero range would divide by zero in a viewer
    const auto t = (height - start) * 4.0F / range;
    return t < 0.0F ? 0.0F : (t > 3.0F ? 3.0F : t);
}

// One region's live layer settings: what the constants above start it at, and
// what an operator may have changed since through the viewer's Region/Estate →
// Terrain tab. The values are per region from here on, so `gridWide` in the
// hello is no longer a constant either — it reports whether this region still
// holds the defaults.
struct Settings {
    std::array<std::string, 4> assets{
        std::string(layer_assets[0]), std::string(layer_assets[1]),
        std::string(layer_assets[2]), std::string(layer_assets[3])};
    std::array<float, 4> start{layer_start_height};
    std::array<float, 4> range{layer_height_range};

    // True while nothing has been changed from the shipped defaults. Published
    // rather than assumed: the day a region differs, a client that was told
    // notices and one that hardcoded does not.
    bool matches_defaults() const {
        for (std::size_t index = 0; index < 4; ++index)
            if (assets[index] != layer_assets[index] ||
                start[index] != layer_start_height[index] ||
                range[index] != layer_height_range[index])
                return false;
        return true;
    }
};

} // namespace homeworldz::terrain
