#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace homeworldz::viewer {

struct EstablishAgentCommunication {
    std::string agent_id;
    std::string simulator_endpoint;
    std::string seed_capability;
};

std::string seed_capability_xml(std::string_view public_endpoint, std::string_view session_id);
std::string event_queue_xml(std::uint64_t id,
                            const std::optional<EstablishAgentCommunication>& event = std::nullopt);
std::string environment_settings_xml(std::string_view region_id);

} // namespace homeworldz::viewer
