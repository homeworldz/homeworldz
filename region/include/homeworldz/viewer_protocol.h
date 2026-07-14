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
    std::vector<CachedTextureQuery> cache_entries;
    std::array<Uuid, 32> texture_ids{};
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
    std::uint8_t pcode{9};
    std::uint8_t material{3};
    std::array<float, 3> position{132.0F, 128.0F, 26.0F};
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
std::optional<AgentMessage> decode_logout_request(std::span<const std::byte> payload);
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
std::vector<std::byte> encode_static_object_update(std::uint64_t region_handle,
                                                   const StaticObject& object);
std::vector<std::byte> encode_avatar_object_update(std::uint64_t region_handle, std::uint32_t local_id,
                                                   const Uuid& agent_id,
                                                   std::array<float, 3> position);
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
