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

struct UseCircuitCode {
    std::uint32_t circuit_code{};
    Uuid session_id{};
    Uuid agent_id{};
};

struct AgentMessage {
    Uuid agent_id{};
    Uuid session_id{};
};

struct CreateInventoryFolder : AgentMessage {
    Uuid folder_id{};
    Uuid parent_id{};
    std::int8_t type{-1};
    std::string name;
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

struct ObjectAdd : AgentMessage {
    Uuid group_id{};
    std::uint8_t pcode{};
    std::uint8_t material{};
    std::uint32_t add_flags{};
    std::uint8_t path_curve{};
    std::uint8_t profile_curve{};
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

struct RequestObjectPropertiesFamily : AgentMessage {
    std::uint32_t request_flags{};
    Uuid object_id{};
};

struct UuidName {
    Uuid id{};
    std::string first_name;
    std::string last_name;
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
    std::vector<std::uint8_t> visual_params;
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
};

std::vector<std::byte> encode_use_circuit_code(const UseCircuitCode& message);
std::optional<UseCircuitCode> decode_use_circuit_code(std::span<const std::byte> payload);
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
std::optional<CopyInventoryItem> decode_copy_inventory_item(std::span<const std::byte> payload);
std::optional<MoveInventoryFolder> decode_move_inventory_folder(std::span<const std::byte> payload);
std::optional<MoveInventoryItem> decode_move_inventory_item(std::span<const std::byte> payload);
std::optional<ObjectAdd> decode_object_add(std::span<const std::byte> payload);
std::optional<DeRezObject> decode_derez_object(std::span<const std::byte> payload);
bool valid_derez_batch(std::uint8_t packet_count, std::uint8_t packet_number);
std::optional<RezObject> decode_rez_object(std::span<const std::byte> payload);
std::vector<std::byte> encode_kill_object(std::span<const std::uint32_t> local_ids);
std::optional<ObjectSelect> decode_object_select(std::span<const std::byte> payload);
std::optional<MultipleObjectUpdate> decode_multiple_object_update(std::span<const std::byte> payload);
std::optional<ObjectName> decode_object_name(std::span<const std::byte> payload);
std::optional<ObjectDescription> decode_object_description(std::span<const std::byte> payload);
std::optional<ObjectPermissions> decode_object_permissions(std::span<const std::byte> payload);
std::optional<ObjectDuplicate> decode_object_duplicate(std::span<const std::byte> payload);
std::optional<ObjectMaterial> decode_object_material(std::span<const std::byte> payload);
std::optional<RequestObjectPropertiesFamily> decode_request_object_properties_family(
    std::span<const std::byte> payload);
std::vector<std::byte> encode_object_properties(std::span<const ObjectProperties> objects);
std::vector<std::byte> encode_object_properties_family(
    std::uint32_t request_flags, const ObjectProperties& object);
std::optional<std::vector<Uuid>> decode_uuid_name_request(std::span<const std::byte> payload);
std::vector<std::byte> encode_uuid_name_reply(std::span<const UuidName> names);
std::vector<std::byte> encode_update_create_inventory_item(const AgentMessage& message,
                                                           std::uint32_t callback_id,
                                                           const InventoryItem& item);
std::vector<std::byte> encode_logout_reply(const AgentMessage& message);
std::optional<AgentCachedTexture> decode_agent_cached_texture(std::span<const std::byte> payload);
std::vector<std::byte> encode_agent_cached_texture_response(const AgentCachedTexture& message);
std::optional<AgentSetAppearance> decode_agent_set_appearance(std::span<const std::byte> payload);
std::optional<RequestImage> decode_request_image(std::span<const std::byte> payload);
std::vector<std::vector<std::byte>> encode_image_transfer(
    const Uuid& image_id, std::span<const std::byte> content, std::uint32_t start_packet = 0);
std::optional<AgentUpdate> decode_agent_update(std::span<const std::byte> payload);
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
};

} // namespace homeworldz::viewer
