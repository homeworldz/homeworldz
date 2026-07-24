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

// The ParcelProperties event, delivered to the viewer over the Event Queue as
// LLSD (matching Halcyon/OpenSim). Field names mirror the ParcelProperties
// message so About Land, "Landmark This Place", and parcel selection populate.
struct ParcelPropertiesEvent {
    std::int32_t request_result{};
    std::int32_t sequence_id{};
    bool snap_selection{};
    std::int32_t self_count{};
    std::int32_t other_count{};
    std::int32_t public_count{};
    std::int32_t local_id{};
    std::string owner_id;
    bool is_group_owned{};
    std::uint32_t auction_id{};
    std::int32_t claim_date{};
    std::int32_t claim_price{};
    std::int32_t rent_price{};
    std::array<float, 3> aabb_min{};
    std::array<float, 3> aabb_max{};
    std::vector<std::uint8_t> bitmap;
    std::int32_t area{};
    std::uint8_t status{};
    std::int32_t sim_wide_max_prims{};
    std::int32_t sim_wide_total_prims{};
    std::int32_t max_prims{};
    std::int32_t total_prims{};
    std::int32_t owner_prims{};
    std::int32_t group_prims{};
    std::int32_t other_prims{};
    std::int32_t selected_prims{};
    float parcel_prim_bonus{1.0F};
    std::int32_t other_clean_time{};
    std::uint32_t parcel_flags{};
    std::int32_t sale_price{};
    std::string name;
    std::string description;
    std::string music_url;
    std::string media_url;
    std::string media_id;
    std::uint8_t media_auto_scale{};
    std::string group_id;
    std::int32_t pass_price{};
    float pass_hours{};
    std::uint8_t category{};
    std::string auth_buyer_id;
    std::string snapshot_id;
    std::array<float, 3> user_location{};
    std::array<float, 3> user_look_at{};
    std::uint8_t landing_type{2};
    bool region_push_override{};
    bool region_deny_anonymous{};
    bool region_deny_identified{};
    bool region_deny_transacted{};
    bool region_deny_age_unverified{};
    std::string media_type{"none/none"};
    std::string media_desc;
    std::int32_t media_width{};
    std::int32_t media_height{};
    bool media_loop{};
    bool obscure_media{};
    bool obscure_music{};
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
std::string parcel_properties_event_xml(const ParcelPropertiesEvent& event);
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
    std::string_view asset_id, bool script, bool compiled = false,
    std::string_view diagnostic = {});
std::string new_file_inventory_upload_xml(std::string_view uploader);
std::string new_file_inventory_complete_xml(std::string_view item_id, std::string_view asset_id,
                                            std::uint32_t everyone_permissions,
                                            std::uint32_t next_permissions);
std::string random_uuid();

} // namespace homeworldz::viewer
