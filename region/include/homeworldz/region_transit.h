#pragma once

#include "homeworldz/grid_client.h"

#include <chrono>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace homeworldz::region {

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

} // namespace homeworldz::region
