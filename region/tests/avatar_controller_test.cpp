#include "homeworldz/avatar_controller.h"

#include <cmath>

int main() {
    homeworldz::viewer::AgentSetAppearance appearance;
    appearance.size = {0.45F, 0.60F, 2.0F};
    const auto fallback_geometry = homeworldz::viewer::avatar_geometry(appearance);
    if (!fallback_geometry || fallback_geometry->height != 2.0 || fallback_geometry->hip_offset != 0.0)
        return 1;
    appearance.visual_params.assign(149, 42);
    const auto calculated_geometry = homeworldz::viewer::avatar_geometry(appearance);
    if (!calculated_geometry || calculated_geometry->height <= 1.0 || calculated_geometry->height >= 3.0 ||
        calculated_geometry->hip_offset >= 0.0)
        return 2;

    homeworldz::viewer::AvatarController avatar;
    homeworldz::viewer::AgentUpdate update;
    update.control_flags = homeworldz::viewer::control_forward;
    update.camera_center = {1.F, 2.F, 3.F};
    update.body_rotation = {0.F, 0.F, 0.5F};
    update.draw_distance = 128.F;
    avatar.apply(update);
    avatar.step(0.25);
    if (std::abs(avatar.state().position.x - 128.5) > 1e-9 ||
        std::abs(avatar.state().position.y - (128.0 + std::sqrt(3.0) / 2.0)) > 1e-9 ||
        std::abs(avatar.state().position.z - 25.78) > 1e-9 ||
        avatar.state().camera_center[1] != 2.F || avatar.state().rotation != update.body_rotation)
        return 3;

    update.control_flags = homeworldz::viewer::control_up;
    avatar.apply(update);
    avatar.step(0.1);
    if (avatar.state().grounded || avatar.state().position.z <= 25.78) return 4;
    update.control_flags = 0;
    avatar.apply(update);
    for (int index = 0; index < 20; ++index) avatar.step(0.1);
    if (!avatar.state().grounded || std::abs(avatar.state().position.z - 25.78) > 1e-9) return 5;

    avatar.set_avatar_geometry(2.0, -0.075);
    avatar.set_ground_height(26.0);
    avatar.step(0.1);
    if (!avatar.state().grounded || avatar.state().height != 2.0 || avatar.state().position.z != 27.0 ||
        std::abs(avatar.viewer_position().z - 27.075) > 1e-9)
        return 6;

    update.control_flags = homeworldz::viewer::control_fly | homeworldz::viewer::control_up |
                           homeworldz::viewer::control_fast_up;
    avatar.apply(update);
    avatar.step(0.25);
    if (!avatar.state().flying || avatar.state().velocity.z != 10.0 || avatar.state().position.z != 29.5)
        return 7;

    homeworldz::viewer::AvatarController launch_avatar;
    update.control_flags = homeworldz::viewer::control_fly;
    launch_avatar.apply(update);
    const auto launch_start = launch_avatar.state().position.z;
    for (int index = 0; index < 150; ++index) launch_avatar.step(0.01);
    const auto launch_rise = launch_avatar.state().position.z - launch_start;
    if (!launch_avatar.state().flying || launch_avatar.state().grounded ||
        launch_rise < 0.49 || launch_rise > 0.53 || std::abs(launch_avatar.state().velocity.z) > 0.01)
        return 8;
    return 0;
}
