#pragma once

#include <cstdint>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace homeworldz::viewer {

struct EstablishAgentCommunication {
    std::string agent_id;
    std::string simulator_endpoint;
    std::string seed_capability;
};

std::string seed_capability_xml(std::string_view public_endpoint, std::string_view grid_public_endpoint,
                                std::string_view session_id);
std::string event_queue_xml(std::uint64_t id,
                            const std::optional<EstablishAgentCommunication>& event = std::nullopt);
std::string environment_settings_xml(std::string_view region_id);
std::string baked_texture_upload_xml(std::string_view uploader);
std::string baked_texture_complete_xml(std::string_view asset_id);
std::string baked_texture_asset_id(std::span<const std::byte> content);

} // namespace homeworldz::viewer
