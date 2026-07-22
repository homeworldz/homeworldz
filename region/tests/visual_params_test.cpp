#include "homeworldz/visual_params.h"

#include "homeworldz/wearable.h"

#include <iostream>

using homeworldz::viewer::build_visual_params;
using homeworldz::viewer::visual_param_count;
using homeworldz::viewer::Wearable;

namespace {
bool expect(bool ok, const char* what) {
    if (!ok) std::cerr << "FAIL: " << what << '\n';
    return ok;
}
}  // namespace

int main() {
    bool ok = true;

    ok &= expect(visual_param_count() == 253, "253 transmitted params");

    // Default avatar: no wearables -> full-length blob of quantized defaults.
    const auto defaults = build_visual_params({});
    ok &= expect(defaults.size() == 253, "default blob is 253 bytes");
    // Param 1 (first): default 0.0 in [-0.3, 2.0] -> floor(0.3/2.3*255) = 33.
    ok &= expect(defaults[0] == 33, "param 1 default quantizes to 33");

    // Wearable override: set param 1 to its max -> 255; below min -> clamped 0.
    Wearable at_max;
    at_max.type = homeworldz::viewer::WearableType::Shape;
    at_max.parameters[1] = 2.0;
    ok &= expect(build_visual_params({at_max})[0] == 255, "param 1 at max quantizes to 255");

    Wearable below_min;
    below_min.type = homeworldz::viewer::WearableType::Shape;
    below_min.parameters[1] = -5.0;
    ok &= expect(build_visual_params({below_min})[0] == 0, "param 1 below min clamps to 0");

    if (!ok) return 1;
    std::cerr << "visual params OK\n";
    return 0;
}
