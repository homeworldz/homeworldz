#pragma once

#include <cstdint>
#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace homeworldz::viewer {

struct EstablishAgentCommunication {
    std::string agent_id;
    std::string simulator_endpoint;
    std::string seed_capability;
};

struct SimulatorEventEndpoint {
    std::array<std::uint8_t, 4> address{};
    std::uint16_t port{};
};

struct TeleportFinish {
    std::string agent_id;
    std::uint64_t region_handle{};
    SimulatorEventEndpoint simulator;
    std::string seed_capability;
    std::uint8_t simulator_access{};
    std::uint32_t teleport_flags{0x00000010U};
};

struct NewFileInventoryUpload {
    std::string folder_id;
    std::string name;
    std::string description;
    std::uint32_t everyone_permissions{};
    std::uint32_t group_permissions{};
    std::uint32_t next_permissions{0x7fffffff};
};

std::string seed_capability_xml(std::string_view public_endpoint, std::string_view grid_public_endpoint,
                                std::string_view session_id, std::string_view visit_id = {});
std::string establish_agent_communication_event_xml(const EstablishAgentCommunication& event);
std::string enable_simulator_event_xml(std::uint64_t region_handle,
                                       const SimulatorEventEndpoint& simulator);
std::string teleport_finish_event_xml(const TeleportFinish& event);
std::string event_queue_xml(std::uint64_t id, const std::vector<std::string>& events = {});
std::string simulator_features_xml(std::string_view currency = "C$",
                                   std::string_view map_server_url = {});
std::string environment_settings_xml(std::string_view region_id);
std::string baked_texture_upload_xml(std::string_view uploader);
std::string baked_texture_complete_xml(std::string_view asset_id);
std::optional<NewFileInventoryUpload> parse_new_file_inventory_upload(std::string_view xml);
std::string new_file_inventory_upload_xml(std::string_view uploader);
std::string new_file_inventory_complete_xml(std::string_view item_id, std::string_view asset_id,
                                            std::uint32_t everyone_permissions,
                                            std::uint32_t next_permissions);
std::string random_uuid();

} // namespace homeworldz::viewer
