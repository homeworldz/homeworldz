#include "homeworldz/avatar_controller.h"

#include <cmath>

int main() {
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
        return 1;

    update.control_flags = homeworldz::viewer::control_up;
    avatar.apply(update);
    avatar.step(0.1);
    if (avatar.state().grounded || avatar.state().position.z <= 25.78) return 2;
    update.control_flags = 0;
    avatar.apply(update);
    for (int index = 0; index < 20; ++index) avatar.step(0.1);
    if (!avatar.state().grounded || std::abs(avatar.state().position.z - 25.78) > 1e-9) return 3;

    avatar.set_avatar_height(2.0);
    avatar.set_ground_height(26.0);
    avatar.step(0.1);
    if (!avatar.state().grounded || avatar.state().height != 2.0 || avatar.state().position.z != 27.0)
        return 4;

    update.control_flags = homeworldz::viewer::control_fly | homeworldz::viewer::control_up |
                           homeworldz::viewer::control_fast_up;
    avatar.apply(update);
    avatar.step(0.25);
    if (!avatar.state().flying || avatar.state().velocity.z != 8.0 || avatar.state().position.z != 29.0)
        return 5;
    return 0;
}
