#ifndef HOMEWORLDZ_VISUAL_PARAMS_H
#define HOMEWORLDZ_VISUAL_PARAMS_H

#include "homeworldz/wearable.h"

#include <cstdint>
#include <vector>

namespace homeworldz::viewer {

// Assemble the AvatarAppearance visual_params byte array for an avatar wearing
// the given wearables. The transmitted set is the 253 group-0 (tweakable) plus
// group-3 (transmit-not-tweakable) visual params, in ascending param-ID order —
// the ordering the Second Life viewer / LibreMetaverse decoder expect. Each
// param takes its value from the first worn wearable that specifies it,
// otherwise its canonical default, quantized as
// floor((clamp(value, min, max) - min) / (max - min) * 255).
//
// With no wearables (or wearables that set no params) this yields the canonical
// default avatar's shape.
std::vector<std::uint8_t> build_visual_params(const std::vector<Wearable>& worn);

// The number of visual params transmitted (253).
std::size_t visual_param_count();

}  // namespace homeworldz::viewer

#endif  // HOMEWORLDZ_VISUAL_PARAMS_H
