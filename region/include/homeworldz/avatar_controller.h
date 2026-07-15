#pragma once

#include "homeworldz/scene.h"
#include "homeworldz/viewer_protocol.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace homeworldz::viewer {

inline constexpr std::uint32_t control_forward = 0x00000001;
inline constexpr std::uint32_t control_back = 0x00000002;
inline constexpr std::uint32_t control_left = 0x00000004;
inline constexpr std::uint32_t control_right = 0x00000008;
inline constexpr std::uint32_t control_up = 0x00000010;
inline constexpr std::uint32_t control_down = 0x00000020;
inline constexpr std::uint32_t control_fast_forward = 0x00000400;
inline constexpr std::uint32_t control_fast_left = 0x00000800;
inline constexpr std::uint32_t control_fast_up = 0x00001000;
inline constexpr std::uint32_t control_fly = 0x00002000;

struct AvatarGeometry {
    double height{1.56};
    double hip_offset{};
};

std::optional<AvatarGeometry> avatar_geometry(const AgentSetAppearance& appearance);

struct AvatarState {
    scene::Vector3 position{128.0, 128.0, 25.0};
    scene::Vector3 velocity{};
    std::array<float, 3> rotation{};
    double height{1.56};
    double hip_offset{};
    bool flying{};
    bool grounded{true};
    std::array<float, 3> camera_center{};
    std::array<float, 3> camera_at{};
    std::array<float, 3> camera_left{};
    std::array<float, 3> camera_up{};
    float draw_distance{};
};

enum class MovementAnimation {
    stand,
    walk,
    run,
    jump,
    fall,
    fly,
    hover,
    hover_up,
    hover_down,
    land,
};

std::string_view movement_animation_id(MovementAnimation animation);

class AvatarController {
public:
    explicit AvatarController(scene::Vector3 spawn = {128.0, 128.0, 25.0},
                              double ground_height = 25.0, double avatar_height = 1.56,
                              double hip_offset = 0.0, double region_width = 256.0,
                              double region_height = 256.0);
    void apply(const AgentUpdate& update);
    void set_avatar_geometry(double height, double hip_offset);
    void set_ground_height(double height);
    void step(double seconds);
    const AvatarState& state() const { return state_; }
    scene::Vector3 viewer_position() const;
    MovementAnimation movement_animation() const;

private:
    AvatarState state_;
    double ground_height_;
    double region_width_;
    double region_height_;
    double flight_lift_velocity_{};
    std::uint32_t controls_{};
    std::array<float, 3> body_rotation_{};
    double landing_animation_remaining_{};
};

} // namespace homeworldz::viewer
