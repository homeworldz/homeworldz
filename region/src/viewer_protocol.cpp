#include "homeworldz/viewer_protocol.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstring>
#include <limits>

namespace homeworldz::viewer {
namespace {

constexpr std::array<std::byte, 4> use_circuit_code_id{
    std::byte{0xff}, std::byte{0xff}, std::byte{0x00}, std::byte{0x03}};
constexpr std::array<std::byte, 4> packet_ack_id{
    std::byte{0xff}, std::byte{0xff}, std::byte{0xff}, std::byte{0xfb}};
constexpr std::array<std::byte, 4> region_handshake_id{
    std::byte{0xff}, std::byte{0xff}, std::byte{0x00}, std::byte{0x94}};
constexpr std::array<std::byte, 4> region_handshake_reply_id{
    std::byte{0xff}, std::byte{0xff}, std::byte{0x00}, std::byte{0x95}};
constexpr std::array<std::byte, 4> complete_agent_movement_id{
    std::byte{0xff}, std::byte{0xff}, std::byte{0x00}, std::byte{0xf9}};
constexpr std::array<std::byte, 4> agent_movement_complete_id{
    std::byte{0xff}, std::byte{0xff}, std::byte{0x00}, std::byte{0xfa}};
constexpr std::array<std::byte, 4> chat_from_viewer_id{
    std::byte{0xff}, std::byte{0xff}, std::byte{0x00}, std::byte{0x50}};
constexpr std::array<std::byte, 4> chat_from_simulator_id{
    std::byte{0xff}, std::byte{0xff}, std::byte{0x00}, std::byte{0x8b}};

class BitWriter {
public:
    void write(std::uint32_t value, unsigned bits) {
        for (unsigned remaining = bits; remaining > 0; --remaining) {
            current_ = static_cast<std::uint8_t>((current_ << 1) | ((value >> (remaining - 1)) & 1));
            if (++used_ == 8) flush_byte();
        }
    }
    void write_byte(std::uint8_t value) { write(value, 8); }
    std::vector<std::byte> finish() {
        if (used_ != 0) {
            current_ <<= (8 - used_);
            flush_byte();
        }
        return std::move(bytes_);
    }
private:
    void flush_byte() {
        bytes_.push_back(static_cast<std::byte>(current_));
        current_ = 0;
        used_ = 0;
    }
    std::vector<std::byte> bytes_;
    std::uint8_t current_{};
    unsigned used_{};
};

std::uint32_t read_be_u32(std::span<const std::byte> data, std::size_t offset) {
    return (std::to_integer<std::uint32_t>(data[offset]) << 24) |
           (std::to_integer<std::uint32_t>(data[offset + 1]) << 16) |
           (std::to_integer<std::uint32_t>(data[offset + 2]) << 8) |
           std::to_integer<std::uint32_t>(data[offset + 3]);
}

void append_be_u32(std::vector<std::byte>& output, std::uint32_t value) {
    output.push_back(static_cast<std::byte>((value >> 24) & 0xff));
    output.push_back(static_cast<std::byte>((value >> 16) & 0xff));
    output.push_back(static_cast<std::byte>((value >> 8) & 0xff));
    output.push_back(static_cast<std::byte>(value & 0xff));
}

std::uint32_t read_le_u32(std::span<const std::byte> data, std::size_t offset) {
    return std::to_integer<std::uint32_t>(data[offset]) |
           (std::to_integer<std::uint32_t>(data[offset + 1]) << 8) |
           (std::to_integer<std::uint32_t>(data[offset + 2]) << 16) |
           (std::to_integer<std::uint32_t>(data[offset + 3]) << 24);
}

float read_f32(std::span<const std::byte> data, std::size_t offset) {
    const auto bits = read_le_u32(data, offset);
    float result{};
    std::memcpy(&result, &bits, sizeof(result));
    return result;
}

std::array<float, 3> read_vector3(std::span<const std::byte> data, std::size_t offset) {
    return {read_f32(data, offset), read_f32(data, offset + 4), read_f32(data, offset + 8)};
}

std::optional<std::pair<std::string, std::size_t>> read_variable2(
    std::span<const std::byte> data, std::size_t offset) {
    if (offset + 2 > data.size()) return std::nullopt;
    const auto size = std::to_integer<std::size_t>(data[offset]) |
                      (std::to_integer<std::size_t>(data[offset + 1]) << 8);
    if (size == 0 || offset + 2 + size > data.size() || data[offset + 1 + size] != std::byte{})
        return std::nullopt;
    const auto begin = reinterpret_cast<const char*>(data.data() + offset + 2);
    return std::pair{std::string(begin, size - 1), offset + 2 + size};
}

void append_le_u32(std::vector<std::byte>& output, std::uint32_t value) {
    output.push_back(static_cast<std::byte>(value & 0xff));
    output.push_back(static_cast<std::byte>((value >> 8) & 0xff));
    output.push_back(static_cast<std::byte>((value >> 16) & 0xff));
    output.push_back(static_cast<std::byte>((value >> 24) & 0xff));
}

void append_le_u16(std::vector<std::byte>& output, std::uint16_t value) {
    output.push_back(static_cast<std::byte>(value));
    output.push_back(static_cast<std::byte>(value >> 8));
}

void append_le_u64(std::vector<std::byte>& output, std::uint64_t value) {
    append_le_u32(output, static_cast<std::uint32_t>(value));
    append_le_u32(output, static_cast<std::uint32_t>(value >> 32));
}

void append_f32(std::vector<std::byte>& output, float value) {
    static_assert(sizeof(float) == sizeof(std::uint32_t));
    std::uint32_t bits{};
    std::memcpy(&bits, &value, sizeof(bits));
    append_le_u32(output, bits);
}

void append_uuid(std::vector<std::byte>& output, const Uuid& value) {
    output.insert(output.end(), value.begin(), value.end());
}

bool append_variable1(std::vector<std::byte>& output, std::string_view value) {
    if (value.size() > 254) return false;
    output.push_back(static_cast<std::byte>(value.size() + 1));
    output.insert(output.end(), reinterpret_cast<const std::byte*>(value.data()),
                  reinterpret_cast<const std::byte*>(value.data() + value.size()));
    output.push_back(std::byte{});
    return true;
}

bool append_variable2(std::vector<std::byte>& output, std::string_view value) {
    if (value.size() > 65534) return false;
    const auto size = static_cast<std::uint16_t>(value.size() + 1);
    output.push_back(static_cast<std::byte>(size & 0xff));
    output.push_back(static_cast<std::byte>((size >> 8) & 0xff));
    output.insert(output.end(), reinterpret_cast<const std::byte*>(value.data()),
                  reinterpret_cast<const std::byte*>(value.data() + value.size()));
    output.push_back(std::byte{});
    return true;
}

bool append_binary(std::vector<std::byte>& output, std::span<const std::byte> value, unsigned length_bytes) {
    if ((length_bytes == 1 && value.size() > 255) || value.size() > 65535) return false;
    output.push_back(static_cast<std::byte>(value.size()));
    if (length_bytes == 2) output.push_back(static_cast<std::byte>(value.size() >> 8));
    output.insert(output.end(), value.begin(), value.end());
    return true;
}

std::vector<std::byte> zero_encode(std::span<const std::byte> input) {
    std::vector<std::byte> output;
    output.reserve(input.size());
    for (std::size_t index = 0; index < input.size();) {
        if (input[index] != std::byte{}) {
            output.push_back(input[index++]);
            continue;
        }
        std::size_t count = 0;
        while (index < input.size() && input[index] == std::byte{} && count < 255) {
            ++index;
            ++count;
        }
        output.push_back(std::byte{});
        output.push_back(static_cast<std::byte>(count));
    }
    return output;
}

std::optional<std::vector<std::byte>> zero_decode(std::span<const std::byte> input) {
    std::vector<std::byte> output;
    for (std::size_t index = 0; index < input.size(); ++index) {
        if (input[index] != std::byte{}) {
            output.push_back(input[index]);
            continue;
        }
        if (++index >= input.size()) return std::nullopt;
        const auto count = std::to_integer<unsigned>(input[index]);
        if (count == 0 || output.size() > 65535 - count) return std::nullopt;
        output.insert(output.end(), count, std::byte{});
    }
    return output;
}

} // namespace

std::optional<Uuid> parse_uuid(std::string_view text) {
    if (text.size() != 36 || text[8] != '-' || text[13] != '-' || text[18] != '-' || text[23] != '-')
        return std::nullopt;
    Uuid result;
    std::size_t input = 0;
    for (auto& byte : result) {
        if (input == 8 || input == 13 || input == 18 || input == 23) ++input;
        unsigned value{};
        const auto parsed = std::from_chars(text.data() + input, text.data() + input + 2, value, 16);
        if (parsed.ec != std::errc{} || parsed.ptr != text.data() + input + 2) return std::nullopt;
        byte = static_cast<std::byte>(value);
        input += 2;
    }
    return result;
}

std::string format_uuid(const Uuid& value) {
    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(36);
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (index == 4 || index == 6 || index == 8 || index == 10) result.push_back('-');
        const auto byte = std::to_integer<unsigned>(value[index]);
        result.push_back(digits[byte >> 4]);
        result.push_back(digits[byte & 0x0f]);
    }
    return result;
}

std::vector<std::byte> encode_use_circuit_code(const UseCircuitCode& message) {
    std::vector<std::byte> output(use_circuit_code_id.begin(), use_circuit_code_id.end());
    output.reserve(40);
    append_le_u32(output, message.circuit_code);
    output.insert(output.end(), message.session_id.begin(), message.session_id.end());
    output.insert(output.end(), message.agent_id.begin(), message.agent_id.end());
    return output;
}

std::optional<UseCircuitCode> decode_use_circuit_code(std::span<const std::byte> payload) {
    if (payload.size() != 40 || !std::equal(use_circuit_code_id.begin(), use_circuit_code_id.end(), payload.begin()))
        return std::nullopt;
    UseCircuitCode message;
    message.circuit_code = read_le_u32(payload, 4);
    std::copy_n(payload.begin() + 8, 16, message.session_id.begin());
    std::copy_n(payload.begin() + 24, 16, message.agent_id.begin());
    return message;
}

std::vector<std::byte> encode_region_handshake(const RegionHandshake& message) {
    std::vector<std::byte> output(region_handshake_id.begin(), region_handshake_id.end());
    append_le_u32(output, 0); // region flags
    output.push_back(std::byte{13}); // PG access
    if (!append_variable1(output, message.name)) return {};
    append_uuid(output, message.owner_id);
    output.push_back(std::byte{}); // estate manager
    append_f32(output, message.water_height);
    append_f32(output, 1.0F); // billable factor
    Uuid zero{};
    append_uuid(output, zero); // cache ID
    for (int index = 0; index < 8; ++index) append_uuid(output, zero); // terrain textures
    for (const float value : {10.F, 10.F, 10.F, 10.F, 60.F, 60.F, 60.F, 60.F}) append_f32(output, value);
    append_uuid(output, message.region_id);
    append_le_u32(output, 0); // CPU class
    append_le_u32(output, 1); // CPU ratio
    if (!append_variable1(output, "") || !append_variable1(output, "homeworldz") ||
        !append_variable1(output, "HomeWorldz Region")) return {};
    output.push_back(std::byte{}); // no RegionInfo4 blocks
    return output;
}

std::optional<AgentMessage> decode_region_handshake_reply(std::span<const std::byte> payload) {
    if (payload.size() != 40 || !std::equal(region_handshake_reply_id.begin(), region_handshake_reply_id.end(), payload.begin()))
        return std::nullopt;
    AgentMessage result;
    std::copy_n(payload.begin() + 4, 16, result.agent_id.begin());
    std::copy_n(payload.begin() + 20, 16, result.session_id.begin());
    return result;
}

std::optional<CompleteAgentMovement> decode_complete_agent_movement(std::span<const std::byte> payload) {
    if (payload.size() != 40 || !std::equal(complete_agent_movement_id.begin(), complete_agent_movement_id.end(), payload.begin()))
        return std::nullopt;
    CompleteAgentMovement result;
    std::copy_n(payload.begin() + 4, 16, result.agent_id.begin());
    std::copy_n(payload.begin() + 20, 16, result.session_id.begin());
    result.circuit_code = read_le_u32(payload, 36);
    return result;
}

std::vector<std::byte> encode_agent_movement_complete(const AgentMovementComplete& message) {
    std::vector<std::byte> output(agent_movement_complete_id.begin(), agent_movement_complete_id.end());
    append_uuid(output, message.agent_id);
    append_uuid(output, message.session_id);
    for (const auto value : message.position) append_f32(output, value);
    for (const auto value : message.look_at) append_f32(output, value);
    append_le_u64(output, message.region_handle);
    append_le_u32(output, message.timestamp);
    if (!append_variable2(output, message.channel_version)) return {};
    return output;
}

std::vector<std::byte> encode_start_ping_check(std::uint8_t ping_id, std::uint32_t oldest_unacked) {
    std::vector<std::byte> output{std::byte{1}, static_cast<std::byte>(ping_id)};
    append_le_u32(output, oldest_unacked);
    return output;
}

std::optional<AgentUpdate> decode_agent_update(std::span<const std::byte> payload) {
    if (payload.size() != 115 || payload[0] != std::byte{4}) return std::nullopt;
    AgentUpdate result;
    std::copy_n(payload.begin() + 1, 16, result.agent_id.begin());
    std::copy_n(payload.begin() + 17, 16, result.session_id.begin());
    result.body_rotation = read_vector3(payload, 33);
    result.head_rotation = read_vector3(payload, 45);
    result.state = std::to_integer<std::uint8_t>(payload[57]);
    result.camera_center = read_vector3(payload, 58);
    result.camera_at = read_vector3(payload, 70);
    result.camera_left = read_vector3(payload, 82);
    result.camera_up = read_vector3(payload, 94);
    result.draw_distance = read_f32(payload, 106);
    result.control_flags = read_le_u32(payload, 110);
    result.flags = std::to_integer<std::uint8_t>(payload[114]);
    return result;
}

std::optional<ChatFromViewer> decode_chat_from_viewer(std::span<const std::byte> payload) {
    if (payload.size() < 43 || !std::equal(chat_from_viewer_id.begin(), chat_from_viewer_id.end(), payload.begin()))
        return std::nullopt;
    ChatFromViewer result;
    std::copy_n(payload.begin() + 4, 16, result.agent_id.begin());
    std::copy_n(payload.begin() + 20, 16, result.session_id.begin());
    const auto message = read_variable2(payload, 36);
    if (!message || message->second + 5 != payload.size()) return std::nullopt;
    result.message = message->first;
    result.type = std::to_integer<std::uint8_t>(payload[message->second]);
    result.channel = static_cast<std::int32_t>(read_le_u32(payload, message->second + 1));
    return result;
}

std::vector<std::byte> encode_chat_from_simulator(const ChatFromSimulator& message) {
    std::vector<std::byte> output(chat_from_simulator_id.begin(), chat_from_simulator_id.end());
    if (!append_variable1(output, message.from_name)) return {};
    append_uuid(output, message.source_id);
    append_uuid(output, message.owner_id);
    output.push_back(static_cast<std::byte>(message.source_type));
    output.push_back(static_cast<std::byte>(message.chat_type));
    output.push_back(static_cast<std::byte>(message.audible));
    for (const auto value : message.position) append_f32(output, value);
    if (!append_variable2(output, message.message)) return {};
    return output;
}

std::vector<std::byte> encode_flat_terrain(std::span<const TerrainPatch> patches, float height) {
    if (patches.empty() || patches.size() > 32 || !std::isfinite(height)) return {};
    BitWriter bits;
    bits.write_byte(0x08); bits.write_byte(0x01); // stride 264, little endian
    bits.write_byte(16); bits.write_byte(0x4c); // 16x16 land layer
    float offset = height - 0.5F;
    std::uint32_t offset_bits{};
    std::memcpy(&offset_bits, &offset, sizeof(offset_bits));
    for (const auto patch : patches) {
        if (patch.x >= 16 || patch.y >= 16) return {};
        bits.write_byte(0x84); // prequant 10, six-bit coefficient words
        bits.write_byte(static_cast<std::uint8_t>(offset_bits));
        bits.write_byte(static_cast<std::uint8_t>(offset_bits >> 8));
        bits.write_byte(static_cast<std::uint8_t>(offset_bits >> 16));
        bits.write_byte(static_cast<std::uint8_t>(offset_bits >> 24));
        bits.write_byte(1); bits.write_byte(0); // range 1
        // LLBitPack serializes a little-endian integer one byte at a time, so
        // this 10-bit field is its low byte followed by its upper two bits.
        const auto patch_id = (static_cast<std::uint32_t>(patch.x) << 5) | patch.y;
        bits.write_byte(static_cast<std::uint8_t>(patch_id));
        bits.write(patch_id >> 8, 2);
        bits.write(2, 2); // zero end-of-block
    }
    bits.write_byte(97); // end of patches
    const auto encoded = bits.finish();
    if (encoded.size() > 65535) return {};
    std::vector<std::byte> output{std::byte{11}, std::byte{0x4c}}; // LayerData, land
    const auto size = static_cast<std::uint16_t>(encoded.size());
    output.push_back(static_cast<std::byte>(size));
    output.push_back(static_cast<std::byte>(size >> 8));
    output.insert(output.end(), encoded.begin(), encoded.end());
    return output;
}

std::vector<std::byte> encode_static_object_update(std::uint64_t region_handle, const StaticObject& object) {
    std::vector<std::byte> output{std::byte{12}}; // high-frequency ObjectUpdate
    append_le_u64(output, region_handle);
    append_le_u16(output, 65535); // full time dilation
    output.push_back(std::byte{1}); // one ObjectData block
    append_le_u32(output, object.local_id);
    output.push_back(std::byte{}); // state
    append_uuid(output, object.id);
    append_le_u32(output, 0); // CRC
    output.push_back(static_cast<std::byte>(object.pcode));
    output.push_back(static_cast<std::byte>(object.material));
    output.push_back(std::byte{}); // click action
    for (const auto value : object.scale) append_f32(output, value);
    std::vector<std::byte> transform;
    for (const auto value : object.position) append_f32(transform, value);
    for (int index = 0; index < 12; ++index) append_f32(transform, 0.0F); // velocity, acceleration, rotation, omega
    if (!append_binary(output, transform, 1)) return {};
    append_le_u32(output, 0); // parent
    append_le_u32(output, 0); // update flags
    output.push_back(std::byte{16}); // straight path
    output.push_back(std::byte{1}); // square profile
    append_le_u16(output, 0); append_le_u16(output, 0); // path begin/end
    output.push_back(std::byte{100}); output.push_back(std::byte{100}); // path scale
    for (int index = 0; index < 7; ++index) output.push_back(std::byte{}); // shear through taper
    output.push_back(std::byte{}); // one path revolution: (value * 0.015) + 1.0
    output.push_back(std::byte{}); // skew
    append_le_u16(output, 0); append_le_u16(output, 0); append_le_u16(output, 0); // profile
    if (!append_binary(output, {}, 2) || !append_binary(output, {}, 1) ||
        !append_binary(output, {}, 2)) return {}; // texture, animation, name/value
    const std::array<std::byte, 1> prim_count{std::byte{1}};
    if (!append_binary(output, prim_count, 2) || !append_binary(output, {}, 1)) return {}; // data, text
    output.insert(output.end(), 4, std::byte{}); // text color
    if (!append_binary(output, {}, 1) || !append_binary(output, {}, 1)) return {}; // media, particles
    const std::array<std::byte, 1> no_extra_params{std::byte{0}};
    if (!append_binary(output, no_extra_params, 1)) return {};
    Uuid zero{};
    append_uuid(output, zero); append_uuid(output, object.owner_id); // sound and owner
    append_f32(output, 0.0F); output.push_back(std::byte{}); append_f32(output, 0.0F);
    output.push_back(std::byte{}); // joint type
    for (int index = 0; index < 6; ++index) append_f32(output, 0.0F);
    return output;
}

std::vector<std::byte> encode_avatar_object_update(std::uint64_t region_handle, std::uint32_t local_id,
                                                   const Uuid& agent_id,
                                                   std::array<float, 3> position) {
    StaticObject avatar;
    avatar.local_id = local_id;
    avatar.id = agent_id;
    avatar.owner_id = agent_id;
    avatar.pcode = 47; // avatar
    avatar.material = 4; // flesh
    avatar.position = position;
    avatar.scale = {0.45F, 0.60F, 1.90F};
    return encode_static_object_update(region_handle, avatar);
}

std::vector<std::byte> encode_packet_ack(std::span<const std::uint32_t> sequences) {
    if (sequences.empty() || sequences.size() > 255) return {};
    std::vector<std::byte> output(packet_ack_id.begin(), packet_ack_id.end());
    output.reserve(5 + sequences.size() * 4);
    output.push_back(static_cast<std::byte>(sequences.size()));
    for (const auto sequence : sequences) append_le_u32(output, sequence);
    return output;
}

std::optional<std::vector<std::uint32_t>> decode_packet_ack(std::span<const std::byte> payload) {
    if (payload.size() < 9 || !std::equal(packet_ack_id.begin(), packet_ack_id.end(), payload.begin()))
        return std::nullopt;
    const auto count = std::to_integer<std::size_t>(payload[4]);
    if (count == 0 || payload.size() != 5 + count * 4) return std::nullopt;
    std::vector<std::uint32_t> result;
    result.reserve(count);
    for (std::size_t offset = 5; offset < payload.size(); offset += 4) result.push_back(read_le_u32(payload, offset));
    return result;
}

std::vector<std::byte> encode_packet(const Packet& packet) {
    if (packet.extra_header.size() > 255 || packet.acknowledgements.size() > 255) return {};
    auto flags = packet.flags;
    if (!packet.acknowledgements.empty()) flags |= flag_appended_acks;
    std::vector<std::byte> output;
    output.reserve(6 + packet.extra_header.size() + packet.payload.size() + packet.acknowledgements.size() * 4 + 1);
    output.push_back(static_cast<std::byte>(flags));
    append_be_u32(output, packet.sequence);
    output.push_back(static_cast<std::byte>(packet.extra_header.size()));
    output.insert(output.end(), packet.extra_header.begin(), packet.extra_header.end());
    const auto encoded = (flags & flag_zero_coded) ? zero_encode(packet.payload) : packet.payload;
    output.insert(output.end(), encoded.begin(), encoded.end());
    for (const auto acknowledgement : packet.acknowledgements) append_be_u32(output, acknowledgement);
    if (!packet.acknowledgements.empty()) output.push_back(static_cast<std::byte>(packet.acknowledgements.size()));
    return output;
}

std::optional<Packet> decode_packet(std::span<const std::byte> datagram) {
    if (datagram.size() < 6) return std::nullopt;
    Packet packet;
    packet.flags = std::to_integer<std::uint8_t>(datagram[0]);
    packet.sequence = read_be_u32(datagram, 1);
    const auto extra_size = std::to_integer<std::size_t>(datagram[5]);
    if (6 + extra_size > datagram.size()) return std::nullopt;
    packet.extra_header.assign(datagram.begin() + 6, datagram.begin() + 6 + extra_size);
    std::size_t payload_end = datagram.size();
    if (packet.flags & flag_appended_acks) {
        if (payload_end < 7) return std::nullopt;
        const auto count = std::to_integer<std::size_t>(datagram[payload_end - 1]);
        if (count == 0 || count > (payload_end - 7) / 4) return std::nullopt;
        const auto ack_start = payload_end - 1 - count * 4;
        for (std::size_t offset = ack_start; offset < payload_end - 1; offset += 4)
            packet.acknowledgements.push_back(read_be_u32(datagram, offset));
        payload_end = ack_start;
    }
    const auto encoded = datagram.subspan(6 + extra_size, payload_end - 6 - extra_size);
    if (packet.flags & flag_zero_coded) {
        auto decoded = zero_decode(encoded);
        if (!decoded) return std::nullopt;
        packet.payload = std::move(*decoded);
    } else {
        packet.payload.assign(encoded.begin(), encoded.end());
    }
    return packet;
}

Circuit::Circuit(Clock::time_point now, double bytes_per_second, std::chrono::seconds idle_timeout)
    : last_activity_(now), token_time_(now), rate_(std::max(1.0, bytes_per_second)),
      capacity_(std::max(1200.0, bytes_per_second)), tokens_(capacity_), idle_timeout_(idle_timeout) {}

std::optional<std::vector<std::byte>> Circuit::send(std::vector<std::byte> payload, bool reliable,
                                                    Clock::time_point now, bool zero_coded) {
    Packet packet;
    packet.flags = (reliable ? flag_reliable : 0) | (zero_coded ? flag_zero_coded : 0);
    packet.sequence = next_sequence_++;
    packet.payload = std::move(payload);
    packet.acknowledgements = take_acks();
    auto datagram = encode_packet(packet);
    if (datagram.empty() || !consume(datagram.size(), now)) {
        queued_acks_.insert(queued_acks_.begin(), packet.acknowledgements.begin(), packet.acknowledgements.end());
        return std::nullopt;
    }
    if (reliable) pending_.emplace(packet.sequence, Pending{packet, now});
    last_activity_ = now;
    return datagram;
}

std::optional<Packet> Circuit::receive(std::span<const std::byte> datagram, Clock::time_point now) {
    auto packet = decode_packet(datagram);
    if (!packet) return std::nullopt;
    last_activity_ = now;
    for (const auto acknowledgement : packet->acknowledgements) pending_.erase(acknowledgement);
    if (const auto acknowledgements = decode_packet_ack(packet->payload))
        for (const auto acknowledgement : *acknowledgements) pending_.erase(acknowledgement);
    if (packet->flags & flag_reliable) {
        if (std::find(queued_acks_.begin(), queued_acks_.end(), packet->sequence) == queued_acks_.end())
            queued_acks_.push_back(packet->sequence);
        if (!received_reliable_.insert(packet->sequence).second) return std::nullopt;
        if (received_reliable_.size() > 4096) received_reliable_.clear();
    }
    return packet;
}

std::vector<std::vector<std::byte>> Circuit::poll(Clock::time_point now) {
    std::vector<std::vector<std::byte>> output;
    constexpr auto resend_after = std::chrono::milliseconds(500);
    for (auto& [sequence, pending] : pending_) {
        static_cast<void>(sequence);
        if (now - pending.sent_at < resend_after || pending.attempts >= 5) continue;
        auto resent = pending.packet;
        resent.flags |= flag_resent;
        resent.acknowledgements = take_acks();
        auto datagram = encode_packet(resent);
        if (!consume(datagram.size(), now)) {
            queued_acks_.insert(queued_acks_.begin(), resent.acknowledgements.begin(), resent.acknowledgements.end());
            continue;
        }
        pending.sent_at = now;
        ++pending.attempts;
        output.push_back(std::move(datagram));
    }
    if (output.empty() && !queued_acks_.empty()) {
        Packet ack;
        ack.sequence = next_sequence_++;
        const auto acknowledgements = take_acks();
        ack.payload = encode_packet_ack(acknowledgements);
        auto datagram = encode_packet(ack);
        if (consume(datagram.size(), now)) output.push_back(std::move(datagram));
        else queued_acks_.insert(queued_acks_.begin(), acknowledgements.begin(), acknowledgements.end());
    }
    return output;
}

bool Circuit::expired(Clock::time_point now) const { return now - last_activity_ > idle_timeout_; }

bool Circuit::consume(std::size_t bytes, Clock::time_point now) {
    const auto elapsed = std::chrono::duration<double>(now - token_time_).count();
    tokens_ = std::min(capacity_, tokens_ + std::max(0.0, elapsed) * rate_);
    token_time_ = now;
    if (tokens_ < static_cast<double>(bytes)) return false;
    tokens_ -= static_cast<double>(bytes);
    return true;
}

std::vector<std::uint32_t> Circuit::take_acks() {
    const auto count = std::min<std::size_t>(queued_acks_.size(), 255);
    std::vector<std::uint32_t> result(queued_acks_.begin(), queued_acks_.begin() + count);
    queued_acks_.erase(queued_acks_.begin(), queued_acks_.begin() + count);
    return result;
}

std::optional<Packet> CircuitRegistry::receive(std::string_view endpoint, std::span<const std::byte> datagram,
                                               Clock::time_point now) {
    auto found = circuits_.find(std::string(endpoint));
    if (found == circuits_.end()) {
        const auto packet = decode_packet(datagram);
        if (!packet || !(packet->flags & flag_reliable)) return std::nullopt;
        const auto requested = decode_use_circuit_code(packet->payload);
        if (!requested) return std::nullopt;
        bool authorized = false;
        try {
            authorized = authorizer_ && authorizer_(*requested);
        } catch (...) {
            return std::nullopt;
        }
        if (!authorized) return std::nullopt;
        for (const auto& [key, entry] : circuits_) {
            static_cast<void>(key);
            if (entry.identity.circuit_code == requested->circuit_code ||
                entry.identity.session_id == requested->session_id || entry.identity.agent_id == requested->agent_id)
                return std::nullopt;
        }
        found = circuits_.emplace(std::string(endpoint), Entry{*requested, Circuit(now)}).first;
    }
    return found->second.circuit.receive(datagram, now);
}

std::optional<std::vector<std::byte>> CircuitRegistry::send(std::string_view endpoint,
                                                            std::vector<std::byte> payload, bool reliable,
                                                            Clock::time_point now, bool zero_coded) {
    const auto found = circuits_.find(std::string(endpoint));
    if (found == circuits_.end()) return std::nullopt;
    return found->second.circuit.send(std::move(payload), reliable, now, zero_coded);
}

std::vector<OutboundDatagram> CircuitRegistry::poll(Clock::time_point now) {
    std::vector<OutboundDatagram> output;
    for (auto iterator = circuits_.begin(); iterator != circuits_.end();) {
        if (iterator->second.circuit.expired(now)) {
            iterator = circuits_.erase(iterator);
            continue;
        }
        auto datagrams = iterator->second.circuit.poll(now);
        for (auto& datagram : datagrams) output.push_back({iterator->first, std::move(datagram)});
        ++iterator;
    }
    return output;
}

const UseCircuitCode* CircuitRegistry::identity(std::string_view endpoint) const {
    const auto found = circuits_.find(std::string(endpoint));
    return found == circuits_.end() ? nullptr : &found->second.identity;
}

} // namespace homeworldz::viewer
