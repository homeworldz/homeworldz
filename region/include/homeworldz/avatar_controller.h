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

// The legacy animation asset a viewer plays for this state. Linden-authored
// content that ships inside the viewer, so it identifies an animation only to
// something with that content already: useless to a client built from nothing,
// which is why the name below exists beside it rather than instead of it.
std::string_view movement_animation_id(MovementAnimation animation);

// The same state as a portable name, published to session clients. A name
// rather than the UUID for the reason water is a height rather than a surface:
// the id names Linden viewer content a client with no legacy code cannot fetch
// or play, while the name says what the avatar is doing and leaves how to draw
// it to the client (client core's third avatar blocker, 2026-08-04). One switch
// per representation, over the same enum, so a new state cannot be given an id
// and no name.
std::string_view movement_animation_name(MovementAnimation animation);

// The movement model's authoritative constants. Published to session clients
// in the hello payload so client-side prediction can simulate what this
// controller will do with the same input, rather than hard-coding an
// observation that rubber-bands the day the region changes it (client core
// request, 2026-07-28). The region owns these numbers; this header is their
// single definition.
inline constexpr double avatar_walk_speed = 4.0;   // m/s; also flight cruise
inline constexpr double avatar_fast_speed = 8.0;   // m/s, run and fast flight
inline constexpr double avatar_jump_velocity = 5.0;  // m/s straight up
inline constexpr double avatar_gravity = 9.81;     // m/s², airborne non-flying
// The avatar capsule and its ground contract, published like the movement
// constants (client core request, 2026-07-29). The avatar's position is the
// capsule center, so standing support is ground height plus half the
// avatar's own height — the height the region computes from shape and
// reports in the session spawned reply. The radius is both the horizontal
// containment margin and the Jolt character capsule; grounded means within
// the tolerance above support.
inline constexpr double avatar_capsule_radius = 0.3;      // m
inline constexpr double avatar_grounded_tolerance = 0.05; // m above support

class AvatarController {
public:
    explicit AvatarController(scene::Vector3 spawn = {128.0, 128.0, 25.0},
                              double ground_height = 25.0, double avatar_height = 1.56,
                              double hip_offset = 0.0, double region_width = 256.0,
                              double region_height = 256.0);
    // MovementInput is the transport-neutral movement command: exactly the
    // fields the controller consumes from a viewer's AgentUpdate, and what a
    // region-session move message maps onto (docs/CLIENT2-EMBODIMENT.md).
    struct MovementInput {
        std::uint32_t control_flags{};
        std::array<float, 3> body_rotation{};
        std::array<float, 3> camera_center{};
        std::array<float, 3> camera_at{};
        std::array<float, 3> camera_left{};
        std::array<float, 3> camera_up{};
        float draw_distance{};
    };
    void apply(const MovementInput& input);
    void apply(const AgentUpdate& update);
    void expire_transient_controls();
    void set_avatar_geometry(double height, double hip_offset);
    void set_ground_height(double height);
    void set_border_crossing_enabled(bool enabled) { border_crossing_enabled_ = enabled; }
    void contain_horizontal();
    void restore_motion(scene::Vector3 velocity, std::array<float, 3> rotation, bool flying);
    void teleport(scene::Vector3 position, bool flying);
    void synchronize_physics(scene::Vector3 position, scene::Vector3 velocity, bool grounded);
    void step(double seconds);
    const AvatarState& state() const { return state_; }
    std::array<float, 3> look_direction() const;
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
    bool physics_grounding_{};
    bool border_crossing_enabled_{};
};

} // namespace homeworldz::viewer
