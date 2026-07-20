#pragma once

#include <cstdint>
#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace homeworldz::viewer {

inline constexpr std::uint32_t teleport_flags_via_location = 0x00000010U;
inline constexpr std::uint32_t teleport_flags_is_flying = 0x00002000U;

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
    std::uint32_t teleport_flags{teleport_flags_via_location};
    std::uint32_t region_size_x{256};
    std::uint32_t region_size_y{256};
};

struct CrossedRegion {
    std::string agent_id;
    std::string session_id;
    std::uint64_t region_handle{};
    SimulatorEventEndpoint simulator;
    std::string seed_capability;
    std::array<float, 3> position{};
    std::array<float, 3> look_at{};
    std::uint32_t region_size_x{256};
    std::uint32_t region_size_y{256};
};

struct NewFileInventoryUpload {
    std::string folder_id;
    std::int8_t asset_type{-1};
    std::int8_t inventory_type{-1};
    std::string name;
    std::string description;
    std::uint32_t everyone_permissions{};
    std::uint32_t group_permissions{};
    std::uint32_t next_permissions{0x7fffffff};
};

struct InventoryAssetUpdate {
    std::string item_id;
    std::string target;
    std::string task_id;
    bool script_running{};
};

std::string seed_capability_xml(std::string_view public_endpoint, std::string_view grid_public_endpoint,
                                std::string_view session_id, std::string_view visit_id = {});
std::string establish_agent_communication_event_xml(const EstablishAgentCommunication& event);
std::string enable_simulator_event_xml(std::uint64_t region_handle,
                                       const SimulatorEventEndpoint& simulator,
                                       std::uint32_t region_size_x = 256,
                                       std::uint32_t region_size_y = 256);
std::string teleport_finish_event_xml(const TeleportFinish& event);
std::string crossed_region_event_xml(const CrossedRegion& event);
std::string event_queue_xml(std::uint64_t id, const std::vector<std::string>& events = {});
std::string simulator_features_xml(std::string_view currency = "C$",
                                   std::string_view map_server_url = {});
std::string environment_settings_xml(std::string_view region_id);
std::string baked_texture_upload_xml(std::string_view uploader);
std::string baked_texture_complete_xml(std::string_view asset_id);
std::optional<NewFileInventoryUpload> parse_new_file_inventory_upload(std::string_view xml);
bool valid_new_file_inventory_upload_content(const NewFileInventoryUpload& upload,
                                             std::string_view content);
std::optional<InventoryAssetUpdate> parse_inventory_asset_update(std::string_view xml);
std::string inventory_asset_update_upload_xml(std::string_view uploader);
std::string inventory_asset_update_complete_xml(
    std::string_view asset_id, bool script, bool compiled = false);
std::string new_file_inventory_upload_xml(std::string_view uploader);
std::string new_file_inventory_complete_xml(std::string_view item_id, std::string_view asset_id,
                                            std::uint32_t everyone_permissions,
                                            std::uint32_t next_permissions);
std::string random_uuid();

} // namespace homeworldz::viewer
