#include "homeworldz/region_transit.h"

#include <algorithm>
#include <utility>

namespace homeworldz::region {

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

} // namespace homeworldz::region
