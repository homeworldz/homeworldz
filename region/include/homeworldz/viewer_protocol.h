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

std::vector<std::byte> encode_use_circuit_code(const UseCircuitCode& message);
std::optional<UseCircuitCode> decode_use_circuit_code(std::span<const std::byte> payload);
std::vector<std::byte> encode_region_handshake(const RegionHandshake& message);
std::optional<AgentMessage> decode_region_handshake_reply(std::span<const std::byte> payload);
std::optional<CompleteAgentMovement> decode_complete_agent_movement(std::span<const std::byte> payload);
std::vector<std::byte> encode_agent_movement_complete(const AgentMovementComplete& message);
std::optional<AgentUpdate> decode_agent_update(std::span<const std::byte> payload);
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
