#include "homeworldz/avatar_controller.h"

#include <cmath>

int main() {
    homeworldz::viewer::AvatarController avatar;
    homeworldz::viewer::AgentUpdate update;
    update.control_flags = homeworldz::viewer::control_forward;
    update.camera_center = {1.F, 2.F, 3.F};
    update.draw_distance = 128.F;
    avatar.apply(update);
    avatar.step(0.25);
    if (std::abs(avatar.state().position.x - 129.0) > 1e-9 || avatar.state().camera_center[1] != 2.F)
        return 1;

    update.control_flags = homeworldz::viewer::control_up;
    avatar.apply(update);
    avatar.step(0.1);
    if (avatar.state().grounded || avatar.state().position.z <= 25.0) return 2;
    update.control_flags = 0;
    avatar.apply(update);
    for (int index = 0; index < 20; ++index) avatar.step(0.1);
    if (!avatar.state().grounded || avatar.state().position.z != 25.0) return 3;

    update.control_flags = homeworldz::viewer::control_fly | homeworldz::viewer::control_up |
                           homeworldz::viewer::control_fast_up;
    avatar.apply(update);
    avatar.step(0.25);
    if (!avatar.state().flying || avatar.state().velocity.z != 8.0 || avatar.state().position.z != 27.0)
        return 4;
    return 0;
}
