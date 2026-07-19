#include "homeworldz/region_transit.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string_view>
#include <utility>

namespace homeworldz::region {

std::optional<std::array<float, 3>> resolve_region_teleport_position(
    int region_grid_x, int region_grid_y, int region_size_x, int region_size_y,
    std::uint64_t requested_handle, std::array<float, 3> requested_position) {
    constexpr std::uint64_t map_cell_metres = 256;
    if (region_grid_x < 0 || region_grid_y < 0 || region_size_x <= 0 || region_size_y <= 0 ||
        region_size_x % map_cell_metres != 0 || region_size_y % map_cell_metres != 0 ||
        !std::isfinite(requested_position[0]) || !std::isfinite(requested_position[1]) ||
        !std::isfinite(requested_position[2]))
        return std::nullopt;
    const auto handle_x = requested_handle >> 32;
    const auto handle_y = requested_handle & 0xffffffffULL;
    if (handle_x % map_cell_metres != 0 || handle_y % map_cell_metres != 0) return std::nullopt;
    const auto origin_x = static_cast<std::uint64_t>(region_grid_x) * map_cell_metres;
    const auto origin_y = static_cast<std::uint64_t>(region_grid_y) * map_cell_metres;
    if (handle_x < origin_x || handle_x >= origin_x + static_cast<std::uint64_t>(region_size_x) ||
        handle_y < origin_y || handle_y >= origin_y + static_cast<std::uint64_t>(region_size_y))
        return std::nullopt;

    // Firestorm normally uses the Region's southwest handle and full variable-
    // Region coordinates. Some map and SLURL paths instead quantize the handle
    // to an internal 256 m tile and send coordinates relative to that tile.
    if (handle_x != origin_x || handle_y != origin_y) {
        requested_position[0] += static_cast<float>(handle_x - origin_x);
        requested_position[1] += static_cast<float>(handle_y - origin_y);
    }
    if (requested_position[0] < 0.0F || requested_position[0] > region_size_x ||
        requested_position[1] < 0.0F || requested_position[1] > region_size_y)
        return std::nullopt;
    return requested_position;
}

std::optional<AvatarBorderCrossing> plan_avatar_border_crossing(
    int source_grid_x, int source_grid_y, int source_size_x, int source_size_y,
    std::array<double, 3> source_position,
    std::span<const grid::RegionNeighbor> neighbors, double destination_inset) {
    if (source_size_x <= 0 || source_size_y <= 0 || destination_inset < 0.0 ||
        !std::isfinite(source_position[0]) || !std::isfinite(source_position[1]) ||
        !std::isfinite(source_position[2]))
        return std::nullopt;
    struct CrossedBorder {
        std::string_view direction;
        double overflow;
    };
    std::array<CrossedBorder, 4> crossed{};
    std::size_t crossed_count{};
    if (source_position[0] < 0.0)
        crossed[crossed_count++] = {"west", -source_position[0]};
    if (source_position[0] > source_size_x)
        crossed[crossed_count++] = {"east", source_position[0] - source_size_x};
    if (source_position[1] < 0.0)
        crossed[crossed_count++] = {"south", -source_position[1]};
    if (source_position[1] > source_size_y)
        crossed[crossed_count++] = {"north", source_position[1] - source_size_y};
    std::sort(crossed.begin(), crossed.begin() + crossed_count,
              [](const CrossedBorder& first, const CrossedBorder& second) {
                  return first.overflow > second.overflow;
              });
    constexpr double map_cell_metres = 256.0;
    const auto global_x = source_grid_x * map_cell_metres + source_position[0];
    const auto global_y = source_grid_y * map_cell_metres + source_position[1];
    for (std::size_t border_index = 0; border_index < crossed_count; ++border_index) {
        for (const auto& neighbor : neighbors) {
            if (!neighbor.online || neighbor.direction != crossed[border_index].direction ||
                neighbor.size_x <= 0 || neighbor.size_y <= 0)
                continue;
            const auto neighbor_x = neighbor.grid_x * map_cell_metres;
            const auto neighbor_y = neighbor.grid_y * map_cell_metres;
            const auto neighbor_max_x = neighbor_x + neighbor.size_x;
            const auto neighbor_max_y = neighbor_y + neighbor.size_y;
            const bool orthogonal_match =
                (neighbor.direction == "east" || neighbor.direction == "west")
                    ? global_y >= neighbor_y && global_y <= neighbor_max_y
                    : global_x >= neighbor_x && global_x <= neighbor_max_x;
            if (!orthogonal_match) continue;
            const auto maximum_x = std::max(destination_inset, neighbor.size_x - destination_inset);
            const auto maximum_y = std::max(destination_inset, neighbor.size_y - destination_inset);
            return AvatarBorderCrossing{
                neighbor,
                {static_cast<float>(std::clamp(global_x - neighbor_x,
                                               destination_inset, maximum_x)),
                 static_cast<float>(std::clamp(global_y - neighbor_y,
                                               destination_inset, maximum_y)),
                 static_cast<float>(source_position[2])}};
        }
    }
    return std::nullopt;
}

bool InboundTransitRegistry::stage(const grid::AvatarTransit& transit,
                                   std::string_view local_region_id,
                                   std::chrono::steady_clock::time_point now,
                                   std::chrono::seconds lifetime) {
    purge(now);
    if (transit.state != "accepted" || transit.id.empty() || transit.generation == 0 ||
        transit.agent_id.empty() || transit.session_id.empty() ||
        transit.source_region_id.empty() || transit.destination_region_id != local_region_id ||
        transit.source_region_id == transit.destination_region_id || lifetime <= std::chrono::seconds::zero())
        return false;
    const auto found = entries_.find(transit.session_id);
    if (found != entries_.end() &&
        (found->second.transit.id != transit.id ||
         found->second.transit.generation != transit.generation))
        return false;
    entries_.insert_or_assign(transit.session_id, Entry{transit, now + lifetime});
    return true;
}

const grid::AvatarTransit* InboundTransitRegistry::authorize(
    std::string_view agent_id, std::string_view session_id,
    std::chrono::steady_clock::time_point now) {
    purge(now);
    const auto found = entries_.find(std::string(session_id));
    if (found == entries_.end() || found->second.transit.agent_id != agent_id) return nullptr;
    return &found->second.transit;
}

std::optional<grid::AvatarTransit> InboundTransitRegistry::consume(
    std::string_view session_id, std::chrono::steady_clock::time_point now) {
    purge(now);
    const auto found = entries_.find(std::string(session_id));
    if (found == entries_.end()) return std::nullopt;
    auto transit = std::move(found->second.transit);
    entries_.erase(found);
    return transit;
}

void InboundTransitRegistry::remove(std::string_view session_id) {
    entries_.erase(std::string(session_id));
}

std::size_t InboundTransitRegistry::size(std::chrono::steady_clock::time_point now) {
    purge(now);
    return entries_.size();
}

void InboundTransitRegistry::purge(std::chrono::steady_clock::time_point now) {
    std::erase_if(entries_, [&](const auto& entry) { return entry.second.expires_at <= now; });
}

bool CapabilityArrivalGate::mark_seed_served(
    std::string_view session_id, std::string_view visit_id) {
    if (session_id.empty() || visit_id.empty()) return false;
    return served_seeds_.insert(key(session_id, visit_id)).second;
}

bool CapabilityArrivalGate::consume_seed(
    std::string_view session_id, std::string_view visit_id) {
    if (session_id.empty() || visit_id.empty()) return false;
    return served_seeds_.erase(key(session_id, visit_id)) != 0;
}

void CapabilityArrivalGate::clear_session(std::string_view session_id) {
    if (session_id.empty()) return;
    const auto prefix = std::string(session_id) + '|';
    std::erase_if(served_seeds_, [&](const std::string& value) {
        return value.starts_with(prefix);
    });
}

std::size_t CapabilityArrivalGate::size() const {
    return served_seeds_.size();
}

std::string CapabilityArrivalGate::key(
    std::string_view session_id, std::string_view visit_id) {
    return std::string(session_id) + '|' + std::string(visit_id);
}

} // namespace homeworldz::region
