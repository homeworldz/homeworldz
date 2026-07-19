#pragma once

#include "homeworldz/grid_client.h"

#include <chrono>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace homeworldz::region {

struct AvatarBorderCrossing {
    grid::RegionNeighbor destination;
    std::array<float, 3> position{};
};

std::optional<AvatarBorderCrossing> plan_avatar_border_crossing(
    int source_grid_x, int source_grid_y, int source_size_x, int source_size_y,
    std::array<double, 3> source_position,
    std::span<const grid::RegionNeighbor> neighbors, double destination_inset = 0.3);

std::optional<std::array<float, 3>> resolve_region_teleport_position(
    int region_grid_x, int region_grid_y, int region_size_x, int region_size_y,
    std::uint64_t requested_handle, std::array<float, 3> requested_position);

class InboundTransitRegistry {
public:
    bool stage(const grid::AvatarTransit& transit, std::string_view local_region_id,
               std::chrono::steady_clock::time_point now,
               std::chrono::seconds lifetime = std::chrono::seconds(30));
    const grid::AvatarTransit* authorize(std::string_view agent_id, std::string_view session_id,
                                        std::chrono::steady_clock::time_point now);
    std::optional<grid::AvatarTransit> consume(std::string_view session_id,
                                               std::chrono::steady_clock::time_point now);
    void remove(std::string_view session_id);
    std::size_t size(std::chrono::steady_clock::time_point now);

private:
    struct Entry {
        grid::AvatarTransit transit;
        std::chrono::steady_clock::time_point expires_at;
    };

    void purge(std::chrono::steady_clock::time_point now);
    std::unordered_map<std::string, Entry> entries_;
};

class CapabilityArrivalGate {
public:
    bool mark_seed_served(std::string_view session_id, std::string_view visit_id);
    bool consume_seed(std::string_view session_id, std::string_view visit_id);
    void clear_session(std::string_view session_id);
    std::size_t size() const;

private:
    static std::string key(std::string_view session_id, std::string_view visit_id);
    std::unordered_set<std::string> served_seeds_;
};

} // namespace homeworldz::region
