#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include <array>

namespace homeworldz::viewer {

inline constexpr std::uint8_t flag_zero_coded = 0x80;
inline constexpr std::uint8_t flag_reliable = 0x40;
inline constexpr std::uint8_t flag_resent = 0x20;
inline constexpr std::uint8_t flag_appended_acks = 0x10;

struct Packet {
    std::uint8_t flags{};
    std::uint32_t sequence{};
    std::vector<std::byte> extra_header;
    std::vector<std::byte> payload;
    std::vector<std::uint32_t> acknowledgements;
};

using Uuid = std::array<std::byte, 16>;

std::optional<Uuid> parse_uuid(std::string_view text);
std::string format_uuid(const Uuid& value);
Uuid combine_uuids(const Uuid& first, const Uuid& second);

struct UseCircuitCode {
    std::uint32_t circuit_code{};
    Uuid session_id{};
    Uuid agent_id{};
};

struct AgentMessage {
    Uuid agent_id{};
    Uuid session_id{};
};

struct TeleportLocationRequest : AgentMessage {
    std::uint64_t region_handle{};
    std::array<float, 3> position{};
    std::array<float, 3> look_at{};
};

struct TeleportStart {
    std::uint32_t flags{};
};

struct TeleportFailed {
    Uuid agent_id{};
    std::string reason;
};

struct CreateInventoryFolder : AgentMessage {
    Uuid folder_id{};
    Uuid parent_id{};
    std::int8_t type{-1};
    std::string name;
};

struct CreateInventoryItem : AgentMessage {
    std::uint32_t callback_id{};
    Uuid folder_id{};
    Uuid transaction_id{};
    std::uint32_t next_owner_permissions{};
    std::int8_t asset_type{-1};
    std::int8_t inventory_type{-1};
    std::uint8_t wearable_type{};
    std::string name;
    std::string description;
};

struct CopyInventoryItem : AgentMessage {
    std::uint32_t callback_id{};
    Uuid old_agent_id{};
    Uuid old_item_id{};
    Uuid new_folder_id{};
    std::string new_name;
};

struct InventoryFolderMove {
    Uuid folder_id{};
    Uuid parent_id{};
};

struct MoveInventoryFolder : AgentMessage {
    bool stamp{};
    std::vector<InventoryFolderMove> folders;
};

struct InventoryItemMove {
    Uuid item_id{};
    Uuid folder_id{};
    std::string new_name;
};

struct MoveInventoryItem : AgentMessage {
    bool stamp{};
    std::vector<InventoryItemMove> items;
};

struct RequestTaskInventory : AgentMessage {
    std::uint32_t local_id{};
};

struct ReplyTaskInventory {
    Uuid task_id{};
    std::int16_t serial{};
    std::string filename;
};

struct UpdateTaskInventory : AgentMessage {
    std::uint32_t local_id{};
    std::uint8_t key{};
    Uuid item_id{};
    Uuid transaction_id{};
};

struct RequestXfer {
    std::uint64_t id{};
    std::string filename;
};

struct ObjectAdd : AgentMessage {
    Uuid group_id{};
    std::uint8_t pcode{};
    std::uint8_t material{};
    std::uint32_t add_flags{};
    std::uint8_t path_curve{};
    std::uint8_t profile_curve{};
    std::uint16_t path_begin{};
    std::uint16_t path_end{};
    std::uint8_t path_scale_x{};
    std::uint8_t path_scale_y{};
    std::uint8_t path_shear_x{};
    std::uint8_t path_shear_y{};
    std::uint8_t path_twist{};
    std::uint8_t path_twist_begin{};
    std::uint8_t path_radius_offset{};
    std::uint8_t path_taper_x{};
    std::uint8_t path_taper_y{};
    std::uint8_t path_revolutions{};
    std::uint8_t path_skew{};
    std::uint16_t profile_begin{};
    std::uint16_t profile_end{};
    std::uint16_t profile_hollow{};
    std::array<float, 3> ray_start{};
    std::array<float, 3> ray_end{};
    Uuid ray_target_id{};
    bool bypass_raycast{};
    bool ray_end_is_intersection{};
    std::array<float, 3> scale{};
    std::array<float, 3> rotation{};
    std::uint8_t state{};
};

struct DeRezObject : AgentMessage {
    Uuid group_id{};
    std::uint8_t destination{};
    Uuid destination_id{};
    Uuid transaction_id{};
    std::uint8_t packet_count{};
    std::uint8_t packet_number{};
    std::vector<std::uint32_t> local_ids;
};

struct RezObject : AgentMessage {
    Uuid group_id{};
    Uuid from_task_id{};
    std::uint8_t bypass_raycast{};
    std::array<float, 3> ray_start{};
    std::array<float, 3> ray_end{};
    Uuid ray_target_id{};
    bool ray_end_is_intersection{};
    bool rez_selected{};
    bool remove_item{};
    Uuid item_id{};
};

struct ObjectSelect : AgentMessage {
    std::vector<std::uint32_t> local_ids;
};

struct ObjectGrabUpdate : AgentMessage {
    Uuid object_id{};
    std::array<float, 3> grab_offset_initial{};
    std::array<float, 3> grab_position{};
    std::uint32_t time_since_last{};
};

struct ObjectTransformUpdate {
    std::uint32_t local_id{};
    std::uint8_t type{};
    std::optional<std::array<float, 3>> position;
    std::optional<std::array<float, 3>> rotation;
    std::optional<std::array<float, 3>> scale;
};

struct MultipleObjectUpdate : AgentMessage {
    std::vector<ObjectTransformUpdate> objects;
};

struct ObjectNameUpdate {
    std::uint32_t local_id{};
    std::string name;
};

struct ObjectName : AgentMessage {
    std::vector<ObjectNameUpdate> objects;
};

struct ObjectDescriptionUpdate {
    std::uint32_t local_id{};
    std::string description;
};

struct ObjectDescription : AgentMessage {
    std::vector<ObjectDescriptionUpdate> objects;
};

struct ObjectPermissionUpdate {
    std::uint32_t local_id{};
    std::uint8_t field{};
    bool set{};
    std::uint32_t mask{};
};

struct ObjectPermissions : AgentMessage {
    bool override_permissions{};
    std::vector<ObjectPermissionUpdate> objects;
};

struct ObjectDuplicate : AgentMessage {
    Uuid group_id{};
    std::array<float, 3> offset{};
    std::uint32_t duplicate_flags{};
    std::vector<std::uint32_t> local_ids;
};

struct ObjectMaterialUpdate {
    std::uint32_t local_id{};
    std::uint8_t material{};
};

struct ObjectMaterial : AgentMessage {
    std::vector<ObjectMaterialUpdate> objects;
};

struct ObjectImageUpdate {
    std::uint32_t local_id{};
    std::vector<std::byte> texture_entry;
};

struct ObjectImage : AgentMessage {
    std::vector<ObjectImageUpdate> objects;
};

struct ObjectFlagUpdate : AgentMessage {
    std::uint32_t local_id{};
    bool use_physics{};
    bool temporary{};
    bool phantom{};
    bool casts_shadows{};
    std::uint8_t physics_shape_type{};
    float density{1000.0F};
    float friction{0.6F};
    float restitution{0.5F};
    float gravity_multiplier{1.0F};
    bool has_extra_physics{};
};

struct RequestObjectPropertiesFamily : AgentMessage {
    std::uint32_t request_flags{};
    Uuid object_id{};
};

struct UuidName {
    Uuid id{};
    std::string first_name;
    std::string last_name;
};

struct MapBlockRequest : AgentMessage {
    std::uint32_t flags{};
    std::uint16_t min_x{};
    std::uint16_t max_x{};
    std::uint16_t min_y{};
    std::uint16_t max_y{};
};

struct MapNameRequest : AgentMessage {
    std::uint32_t flags{};
    std::string name;
};

struct MapBlock {
    std::uint16_t x{};
    std::uint16_t y{};
    std::string name;
    std::uint8_t access{13};
    std::uint32_t region_flags{};
    std::uint8_t water_height{20};
    std::uint8_t agents{};
    Uuid map_image_id{};
};

struct ObjectProperties {
    Uuid object_id{};
    Uuid creator_id{};
    Uuid owner_id{};
    std::uint32_t base_permissions{0x0009e000};
    std::uint32_t owner_permissions{0x0009e000};
    std::uint32_t group_permissions{};
    std::uint32_t everyone_permissions{};
    std::uint32_t next_owner_permissions{0x0008e000};
    std::uint64_t creation_date{};
    std::string name;
    std::string description;
};

struct InventoryItem {
    Uuid item_id{};
    Uuid creator_id{};
    Uuid owner_id{};
    Uuid folder_id{};
    Uuid asset_id{};
    std::int8_t asset_type{};
    std::int8_t inventory_type{};
    std::string name;
    std::string description;
    std::uint32_t flags{};
    std::uint32_t base_permissions{};
    std::uint32_t current_permissions{};
    std::uint32_t everyone_permissions{};
    std::uint32_t next_permissions{};
    std::uint8_t sale_type{};
    std::int32_t sale_price{};
    std::int32_t creation_date{};
};

struct CachedTextureQuery {
    Uuid cache_id{};
    std::uint8_t texture_index{};
    Uuid texture_id{};
};

struct AgentCachedTexture : AgentMessage {
    std::int32_t serial{};
    std::vector<CachedTextureQuery> queries;
};

struct AgentSetAppearance : AgentMessage {
    std::uint32_t serial{};
    std::array<float, 3> size{};
    std::vector<CachedTextureQuery> cache_entries;
    std::array<Uuid, 32> texture_ids{};
    std::vector<std::byte> texture_entry;
    std::vector<std::uint8_t> visual_params;
};

struct AvatarAppearance {
    Uuid sender_id{};
    std::uint32_t serial{};
    std::vector<std::byte> texture_entry;
    std::vector<std::uint8_t> visual_params;
    std::array<float, 3> hover{};
};

struct AgentAnimationEntry {
    Uuid animation_id{};
    bool start{};
};

struct AgentAnimation : AgentMessage {
    std::vector<AgentAnimationEntry> animations;
};

struct AvatarAnimationEntry {
    Uuid animation_id{};
    std::int32_t sequence{};
    Uuid source_id{};
};

struct AvatarAnimation {
    Uuid sender_id{};
    std::vector<AvatarAnimationEntry> animations;
};

struct AssetUploadRequest {
    Uuid transaction_id{};
    std::int8_t asset_type{};
    bool temporary{};
    bool store_local{};
    std::vector<std::byte> data;
};

struct UpdateInventoryAsset : AgentMessage {
    Uuid item_id{};
    Uuid transaction_id{};
};

struct XferPacket {
    std::uint64_t id{};
    std::uint32_t packet{};
    std::vector<std::byte> data;
};

struct ImageRequestBlock {
    Uuid image_id{};
    std::int8_t discard_level{};
    float download_priority{};
    std::uint32_t packet{};
    std::uint8_t type{};
};

struct RequestImage : AgentMessage {
    std::vector<ImageRequestBlock> requests;
};

struct CompleteAgentMovement : AgentMessage {
    std::uint32_t circuit_code{};
};

struct RegionHandshake {
    std::string name{"My Region"};
    Uuid region_id{};
    Uuid owner_id{};
    float water_height{20.0F};
    std::array<Uuid, 4> terrain_textures{};
};

struct AgentMovementComplete : AgentMessage {
    std::array<float, 3> position{128.0F, 128.0F, 25.0F};
    std::array<float, 3> look_at{1.0F, 0.0F, 0.0F};
    std::uint64_t region_handle{};
    std::uint32_t timestamp{};
    std::string channel_version{"HomeWorldz dev"};
};

struct AgentUpdate : AgentMessage {
    std::array<float, 3> body_rotation{};
    std::array<float, 3> head_rotation{};
    std::uint8_t state{};
    std::array<float, 3> camera_center{};
    std::array<float, 3> camera_at{};
    std::array<float, 3> camera_left{};
    std::array<float, 3> camera_up{};
    float draw_distance{};
    std::uint32_t control_flags{};
    std::uint8_t flags{};
};

struct ModifyLandArea {
    std::int32_t local_id{};
    float west{};
    float south{};
    float east{};
    float north{};
};

struct ModifyLand : AgentMessage {
    std::uint8_t action{};
    std::uint8_t brush_size{};
    float seconds{};
    float height{};
    std::vector<ModifyLandArea> areas;
    std::vector<float> extended_brush_sizes;
};

struct ChatFromViewer : AgentMessage {
    std::string message;
    std::uint8_t type{};
    std::int32_t channel{};
};

struct ChatFromSimulator {
    std::string from_name;
    Uuid source_id{};
    Uuid owner_id{};
    std::uint8_t source_type{1};
    std::uint8_t chat_type{1};
    std::uint8_t audible{1};
    std::array<float, 3> position{};
    std::string message;
};

struct TerrainPatch {
    std::uint8_t x{};
    std::uint8_t y{};
};

struct StaticObject {
    std::uint32_t local_id{1};
    std::uint32_t parent_local_id{};
    Uuid id{};
    Uuid owner_id{};
    std::uint32_t update_flags{};
    std::uint8_t pcode{9};
    std::uint8_t material{3};
    std::array<float, 3> position{132.0F, 128.0F, 26.0F};
    std::array<float, 3> velocity{};
    std::array<float, 3> acceleration{};
    std::array<float, 3> rotation{};
    std::array<float, 3> scale{2.0F, 2.0F, 2.0F};
    std::vector<std::byte> texture_entry;
    std::uint8_t path_curve{0x10};
    std::uint8_t profile_curve{0x01};
    std::uint16_t path_begin{};
    std::uint16_t path_end{};
    std::uint8_t path_scale_x{100};
    std::uint8_t path_scale_y{100};
    std::uint8_t path_shear_x{};
    std::uint8_t path_shear_y{};
    std::uint8_t path_twist{};
    std::uint8_t path_twist_begin{};
    std::uint8_t path_radius_offset{};
    std::uint8_t path_taper_x{};
    std::uint8_t path_taper_y{};
    std::uint8_t path_revolutions{};
    std::uint8_t path_skew{};
    std::uint16_t profile_begin{};
    std::uint16_t profile_end{};
    std::uint16_t profile_hollow{};
};

// Builds the canonical TextureEntry defaults for a newly created primitive:
// white tint, 1x repeats, zero offsets/rotation, and no per-face overrides.
std::vector<std::byte> default_texture_entry(const Uuid& texture_id);

// Replaces an absent, null, or viewer-local fallback default face with the
// supplied server-backed default while preserving valid face parameters.
bool normalize_primitive_texture_entry(
    std::vector<std::byte>& texture_entry, std::span<const std::byte> default_entry);

std::vector<std::byte> encode_use_circuit_code(const UseCircuitCode& message);
std::optional<UseCircuitCode> decode_use_circuit_code(std::span<const std::byte> payload);
std::optional<TeleportLocationRequest> decode_teleport_location_request(
    std::span<const std::byte> payload);
std::vector<std::byte> encode_teleport_start(const TeleportStart& message);
std::vector<std::byte> encode_teleport_failed(const TeleportFailed& message);
std::vector<std::byte> encode_region_handshake(const RegionHandshake& message);
std::optional<AgentMessage> decode_region_handshake_reply(std::span<const std::byte> payload);
std::optional<CompleteAgentMovement> decode_complete_agent_movement(std::span<const std::byte> payload);
std::vector<std::byte> encode_agent_movement_complete(const AgentMovementComplete& message);
std::vector<std::byte> encode_start_ping_check(std::uint8_t ping_id, std::uint32_t oldest_unacked = 0);
std::optional<std::uint8_t> decode_start_ping_check(std::span<const std::byte> payload);
std::vector<std::byte> encode_complete_ping_check(std::uint8_t ping_id);
bool is_economy_data_request(std::span<const std::byte> payload);
std::vector<std::byte> encode_economy_data(std::int32_t price_upload = 0,
                                           std::int32_t object_capacity = 15000,
                                           std::int32_t object_count = 0);
std::optional<AgentMessage> decode_logout_request(std::span<const std::byte> payload);
std::optional<CreateInventoryFolder> decode_create_inventory_folder(std::span<const std::byte> payload);
std::optional<CreateInventoryItem> decode_create_inventory_item(std::span<const std::byte> payload);
std::optional<CopyInventoryItem> decode_copy_inventory_item(std::span<const std::byte> payload);
std::optional<MoveInventoryFolder> decode_move_inventory_folder(std::span<const std::byte> payload);
std::optional<MoveInventoryItem> decode_move_inventory_item(std::span<const std::byte> payload);
std::optional<RequestTaskInventory> decode_request_task_inventory(std::span<const std::byte> payload);
std::vector<std::byte> encode_reply_task_inventory(const ReplyTaskInventory& message);
std::optional<UpdateTaskInventory> decode_update_task_inventory(std::span<const std::byte> payload);
std::optional<RequestXfer> decode_request_xfer(std::span<const std::byte> payload);
std::vector<std::byte> encode_send_xfer_packet(
    std::uint64_t id, std::uint32_t packet, std::span<const std::byte> data);
std::optional<ObjectAdd> decode_object_add(std::span<const std::byte> payload);
std::optional<DeRezObject> decode_derez_object(std::span<const std::byte> payload);
bool valid_derez_batch(std::uint8_t packet_count, std::uint8_t packet_number);
std::optional<RezObject> decode_rez_object(std::span<const std::byte> payload);
std::vector<std::byte> encode_kill_object(std::span<const std::uint32_t> local_ids);
std::optional<ObjectSelect> decode_object_select(std::span<const std::byte> payload);
std::optional<ObjectSelect> decode_object_deselect(std::span<const std::byte> payload);
std::optional<ObjectSelect> decode_object_link(std::span<const std::byte> payload);
std::optional<ObjectSelect> decode_object_delink(std::span<const std::byte> payload);
std::optional<ObjectGrabUpdate> decode_object_grab_update(std::span<const std::byte> payload);
std::optional<MultipleObjectUpdate> decode_multiple_object_update(std::span<const std::byte> payload);
std::optional<ObjectName> decode_object_name(std::span<const std::byte> payload);
std::optional<ObjectDescription> decode_object_description(std::span<const std::byte> payload);
std::optional<ObjectPermissions> decode_object_permissions(std::span<const std::byte> payload);
std::optional<ObjectDuplicate> decode_object_duplicate(std::span<const std::byte> payload);
std::optional<ObjectMaterial> decode_object_material(std::span<const std::byte> payload);
std::optional<ObjectImage> decode_object_image(std::span<const std::byte> payload);
std::optional<ObjectFlagUpdate> decode_object_flag_update(std::span<const std::byte> payload);
std::optional<RequestObjectPropertiesFamily> decode_request_object_properties_family(
    std::span<const std::byte> payload);
std::vector<std::byte> encode_object_properties(std::span<const ObjectProperties> objects);
std::vector<std::byte> encode_object_properties_family(
    std::uint32_t request_flags, const ObjectProperties& object);
std::optional<std::vector<Uuid>> decode_uuid_name_request(std::span<const std::byte> payload);
std::vector<std::byte> encode_uuid_name_reply(std::span<const UuidName> names);
std::optional<MapBlockRequest> decode_map_block_request(std::span<const std::byte> payload);
std::optional<MapNameRequest> decode_map_name_request(std::span<const std::byte> payload);
std::vector<std::byte> encode_map_block_reply(const Uuid& agent_id, std::uint32_t flags,
                                               std::span<const MapBlock> regions);
std::vector<std::byte> encode_update_create_inventory_item(const AgentMessage& message,
                                                           std::uint32_t callback_id,
                                                           const InventoryItem& item);
std::vector<std::byte> encode_logout_reply(const AgentMessage& message);
std::optional<AgentCachedTexture> decode_agent_cached_texture(std::span<const std::byte> payload);
std::vector<std::byte> encode_agent_cached_texture_response(const AgentCachedTexture& message);
std::optional<AgentSetAppearance> decode_agent_set_appearance(std::span<const std::byte> payload);
std::vector<std::byte> encode_avatar_appearance(const AvatarAppearance& message);
std::optional<AgentAnimation> decode_agent_animation(std::span<const std::byte> payload);
std::vector<std::byte> encode_avatar_animation(const AvatarAnimation& message);
std::optional<AssetUploadRequest> decode_asset_upload_request(std::span<const std::byte> payload);
std::vector<std::byte> encode_asset_upload_complete(const Uuid& asset_id,
                                                    std::int8_t asset_type, bool success);
std::optional<UpdateInventoryAsset> decode_update_inventory_asset(std::span<const std::byte> payload);
std::vector<std::byte> encode_request_xfer(std::uint64_t id, const Uuid& asset_id,
                                           std::int16_t asset_type);
std::optional<XferPacket> decode_send_xfer_packet(std::span<const std::byte> payload);
std::vector<std::byte> encode_confirm_xfer_packet(std::uint64_t id, std::uint32_t packet);
std::optional<RequestImage> decode_request_image(std::span<const std::byte> payload);
std::vector<std::vector<std::byte>> encode_image_transfer(
    const Uuid& image_id, std::span<const std::byte> content, std::uint32_t start_packet = 0);
std::optional<AgentUpdate> decode_agent_update(std::span<const std::byte> payload);
std::optional<ModifyLand> decode_modify_land(std::span<const std::byte> payload);
std::optional<ChatFromViewer> decode_chat_from_viewer(std::span<const std::byte> payload);
std::vector<std::byte> encode_chat_from_simulator(const ChatFromSimulator& message);
std::vector<std::byte> encode_flat_terrain(std::span<const TerrainPatch> patches, float height);
std::vector<std::byte> encode_terrain(std::span<const TerrainPatch> patches,
                                      std::span<const float> heightmap);
std::vector<std::byte> encode_static_object_update(std::uint64_t region_handle,
                                                   const StaticObject& object);
std::vector<std::byte> encode_avatar_object_update(std::uint64_t region_handle, std::uint32_t local_id,
                                                   const Uuid& agent_id,
                                                   std::array<float, 3> position,
                                                   std::array<float, 3> velocity = {},
                                                   std::array<float, 3> rotation = {});
std::vector<std::byte> encode_packet_ack(std::span<const std::uint32_t> sequences);
std::optional<std::vector<std::uint32_t>> decode_packet_ack(std::span<const std::byte> payload);

std::vector<std::byte> encode_packet(const Packet& packet);
std::optional<Packet> decode_packet(std::span<const std::byte> datagram);

class Circuit {
public:
    using Clock = std::chrono::steady_clock;

    explicit Circuit(Clock::time_point now, double bytes_per_second = 128000,
                     std::chrono::seconds idle_timeout = std::chrono::seconds(30));

    std::optional<std::vector<std::byte>> send(std::vector<std::byte> payload, bool reliable,
                                               Clock::time_point now, bool zero_coded = false);
    std::optional<Packet> receive(std::span<const std::byte> datagram, Clock::time_point now);
    std::vector<std::vector<std::byte>> poll(Clock::time_point now);
    bool expired(Clock::time_point now) const;
    std::size_t pending_reliable() const { return pending_.size(); }

private:
    struct Pending {
        Packet packet;
        Clock::time_point sent_at;
        unsigned attempts{1};
    };

    bool consume(std::size_t bytes, Clock::time_point now);
    std::vector<std::uint32_t> take_acks();

    std::uint32_t next_sequence_{1};
    std::unordered_map<std::uint32_t, Pending> pending_;
    std::unordered_set<std::uint32_t> received_reliable_;
    std::vector<std::uint32_t> queued_acks_;
    Clock::time_point last_activity_;
    Clock::time_point token_time_;
    double rate_;
    double capacity_;
    double tokens_;
    std::chrono::seconds idle_timeout_;
};

struct OutboundDatagram {
    std::string endpoint;
    std::vector<std::byte> bytes;
};

struct ReplacedCircuit {
    std::string endpoint;
    UseCircuitCode identity;
};

class CircuitRegistry {
public:
    using Clock = Circuit::Clock;
    using Authorizer = std::function<bool(const UseCircuitCode&)>;

    explicit CircuitRegistry(Authorizer authorizer) : authorizer_(std::move(authorizer)) {}
    std::optional<Packet> receive(std::string_view endpoint, std::span<const std::byte> datagram,
                                  Clock::time_point now);
    std::optional<std::vector<std::byte>> send(std::string_view endpoint, std::vector<std::byte> payload,
                                               bool reliable, Clock::time_point now, bool zero_coded = false);
    std::vector<OutboundDatagram> poll(Clock::time_point now);
    std::vector<ReplacedCircuit> take_replaced();
    const UseCircuitCode* identity(std::string_view endpoint) const;
    bool remove(std::string_view endpoint);
    std::size_t size() const { return circuits_.size(); }

private:
    struct Entry {
        UseCircuitCode identity;
        Circuit circuit;
    };

    Authorizer authorizer_;
    std::unordered_map<std::string, Entry> circuits_;
    std::vector<ReplacedCircuit> replaced_;
};

} // namespace homeworldz::viewer
