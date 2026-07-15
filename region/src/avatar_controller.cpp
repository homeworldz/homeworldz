#include "homeworldz/avatar_controller.h"

#include <algorithm>
#include <cmath>

namespace homeworldz::viewer {

std::optional<AvatarGeometry> avatar_geometry(const AgentSetAppearance& appearance) {
    // Match the Halcyon/InWorldz visual-parameter height and hip calculation.
    // The viewer-provided Size is a fallback for clients with a shorter block.
    const auto& values = appearance.visual_params;
    if (values.size() > 148) {
        const auto shoe_heel = 0.08 * values[77] / 255.0;
        const auto shoe_platform = 0.07 * values[78] / 255.0;
        const auto leg_length = 0.3836 * values[125] / 255.0;
        const auto height = 1.23077
                          + 0.516945 * values[25] / 255.0
                          + 0.072514 * values[120] / 255.0
                          + leg_length + shoe_heel + shoe_platform
                          + 0.076 * values[148] / 255.0;
        const auto hip_offset =
            (0.615385 + shoe_heel + shoe_platform + leg_length - height * 0.5) * 0.3 - 0.04;
        return AvatarGeometry{height, hip_offset};
    }
    const auto viewer_height = static_cast<double>(appearance.size[2]);
    if (std::isfinite(viewer_height) && viewer_height >= 1.0 && viewer_height <= 3.0)
        return AvatarGeometry{viewer_height, 0.0};
    return std::nullopt;
}

AvatarController::AvatarController(scene::Vector3 spawn, double ground_height, double avatar_height,
                                   double hip_offset)
    : state_{spawn}, ground_height_(ground_height) {
    set_avatar_geometry(avatar_height, hip_offset);
    const auto support_height = ground_height_ + state_.height * 0.5;
    if (state_.position.z <= support_height) state_.position.z = support_height;
    state_.grounded = state_.position.z <= support_height + 0.05;
}

void AvatarController::apply(const AgentUpdate& update) {
    controls_ = update.control_flags;
    body_rotation_ = update.body_rotation;
    state_.rotation = update.body_rotation;
    const auto flying = (controls_ & control_fly) != 0;
    if (flying && !state_.flying && state_.grounded) {
        // Halcyon launches a grounded avatar into flight with a 2 m/s vertical
        // impulse. Exponential damping approximates the flight controller's
        // settling behavior until Jolt owns the character capsule.
        flight_lift_velocity_ = 2.0;
        state_.grounded = false;
    } else if (!flying) {
        flight_lift_velocity_ = 0.0;
    }
    state_.flying = flying;
    state_.camera_center = update.camera_center;
    state_.camera_at = update.camera_at;
    state_.camera_left = update.camera_left;
    state_.camera_up = update.camera_up;
    state_.draw_distance = update.draw_distance;
}

void AvatarController::set_avatar_geometry(double height, double hip_offset) {
    if (!std::isfinite(height)) return;
    state_.height = std::clamp(height, 1.0, 3.0);
    if (std::isfinite(hip_offset)) state_.hip_offset = std::clamp(hip_offset, -0.5, 0.5);
    if (state_.grounded) state_.position.z = ground_height_ + state_.height * 0.5;
}

void AvatarController::set_ground_height(double height) {
    if (std::isfinite(height)) ground_height_ = height;
}

scene::Vector3 AvatarController::viewer_position() const {
    auto position = state_.position;
    position.z -= state_.hip_offset;
    return position;
}

void AvatarController::step(double seconds) {
    seconds = std::clamp(seconds, 0.0, 0.25);
    const auto support_height = ground_height_ + state_.height * 0.5;
    if (!state_.flying && state_.grounded) {
        if (state_.position.z > support_height + 0.25)
            state_.grounded = false;
        else
            state_.position.z = support_height;
    }
    const double x = body_rotation_[0], y = body_rotation_[1], z = body_rotation_[2];
    const double w = std::sqrt(std::max(0.0, 1.0 - x * x - y * y - z * z));
    const double forward_x = 1.0 - 2.0 * (y * y + z * z);
    const double forward_y = 2.0 * (x * y + w * z);
    const double left_x = -forward_y;
    const double left_y = forward_x;
    double forward = ((controls_ & control_forward) ? 1.0 : 0.0) - ((controls_ & control_back) ? 1.0 : 0.0);
    double left = ((controls_ & control_left) ? 1.0 : 0.0) - ((controls_ & control_right) ? 1.0 : 0.0);
    const double length = std::hypot(forward, left);
    if (length > 1.0) { forward /= length; left /= length; }
    const bool fast = controls_ & (control_fast_forward | control_fast_left | control_fast_up);
    const double speed = fast ? 8.0 : 4.0;
    state_.velocity.x = (forward_x * forward + left_x * left) * speed;
    state_.velocity.y = (forward_y * forward + left_y * left) * speed;
    if (state_.flying) {
        const double vertical = ((controls_ & control_up) ? 1.0 : 0.0) - ((controls_ & control_down) ? 1.0 : 0.0);
        state_.velocity.z = vertical * speed + flight_lift_velocity_;
        flight_lift_velocity_ *= std::exp(-4.0 * seconds);
        if (flight_lift_velocity_ < 0.01) flight_lift_velocity_ = 0.0;
        state_.grounded = false;
    } else {
        if ((controls_ & control_up) && state_.grounded) {
            state_.velocity.z = 5.0;
            state_.grounded = false;
        }
        if (!state_.grounded) state_.velocity.z -= 9.81 * seconds;
    }
    state_.position.x += state_.velocity.x * seconds;
    state_.position.y += state_.velocity.y * seconds;
    state_.position.z += state_.velocity.z * seconds;
    if (!state_.flying && state_.position.z <= support_height) {
        state_.position.z = support_height;
        state_.velocity.z = 0.0;
        state_.grounded = true;
    }
}

} // namespace homeworldz::viewer
