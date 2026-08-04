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
// **Region-wide constants, not per-region state.** Making these operator
// settings is a named prerequisite of the terrain-surface work in
// docs/ROADMAP.md and is not done. Published anyway, and published with that
// fact attached, because "identical on every region" is something a client
// should be told rather than left to hardcode — the moment it stops being true,
// a client that was told will notice and one that assumed will not.
//
// **No blend rule is published, deliberately.** The region implements none:
// viewers each blend in their own code, and the original grid's version was
// never reproduced outside it, so any rule stated here would be authoritative
// for the first-party client and approximate against a viewer standing on the
// same hill. Publishing an approximate rule is worse than publishing none
// (client core's own preference, and the reason the contact model stayed
// unpublished).

#include <array>
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

// True while the values above are compile-time constants shared by every
// region. Published so a client reads the fact rather than assuming it.
inline constexpr bool layers_are_grid_wide = true;

} // namespace homeworldz::terrain
