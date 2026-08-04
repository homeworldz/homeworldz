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

// Per corner, in the order the handshake writes them: south-west, north-west,
// south-east, north-east. A layer's band is [start, start + range] with the
// corner values interpolated across the region; uniform here, so the band is
// 10 m to 70 m everywhere.
inline constexpr std::array<float, 4> layer_start_height{10.0F, 10.0F, 10.0F, 10.0F};
inline constexpr std::array<float, 4> layer_height_range{60.0F, 60.0F, 60.0F, 60.0F};

// True while the values above are compile-time constants shared by every
// region. Published so a client reads the fact rather than assuming it.
inline constexpr bool layers_are_grid_wide = true;

} // namespace homeworldz::terrain
