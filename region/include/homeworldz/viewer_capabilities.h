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

struct NewFileInventoryUpload {
    std::string folder_id;
    std::string name;
    std::string description;
    std::uint32_t everyone_permissions{};
    std::uint32_t group_permissions{};
    std::uint32_t next_permissions{0x7fffffff};
};

std::string seed_capability_xml(std::string_view public_endpoint, std::string_view grid_public_endpoint,
                                std::string_view session_id);
std::string event_queue_xml(std::uint64_t id,
                            const std::optional<EstablishAgentCommunication>& event = std::nullopt);
std::string simulator_features_xml(std::string_view currency = "C$");
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
