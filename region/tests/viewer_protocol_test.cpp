#include "homeworldz/viewer_protocol.h"

#include <array>
#include <chrono>

namespace {
using namespace homeworldz::viewer;
using namespace std::chrono_literals;

std::vector<std::byte> bytes(std::initializer_list<unsigned> values) {
    std::vector<std::byte> result;
    for (const auto value : values) result.push_back(static_cast<std::byte>(value));
    return result;
}

bool packet_round_trip() {
    Packet packet;
    packet.flags = flag_zero_coded | flag_reliable;
    packet.sequence = 0x10203040;
    packet.extra_header = bytes({9, 8});
    packet.payload = bytes({1, 0, 0, 0, 2, 0, 3});
    packet.acknowledgements = {4, 0x55667788};
    const auto encoded = encode_packet(packet);
    const auto decoded = decode_packet(encoded);
    return decoded && decoded->sequence == packet.sequence && decoded->extra_header == packet.extra_header &&
           decoded->payload == packet.payload && decoded->acknowledgements == packet.acknowledgements &&
           (decoded->flags & flag_appended_acks);
}

bool reliability() {
    const auto start = Circuit::Clock::time_point{};
    Circuit sender(start);
    Circuit receiver(start);
    const auto first = sender.send(bytes({1, 2, 3}), true, start);
    if (!first || sender.pending_reliable() != 1) return false;
    const auto received = receiver.receive(*first, start + 1ms);
    if (!received || received->payload != bytes({1, 2, 3})) return false;
    if (receiver.receive(*first, start + 2ms)) return false;
    const auto acknowledgements = receiver.poll(start + 3ms);
    if (acknowledgements.size() != 1) return false;
    const auto acknowledgement_packet = decode_packet(acknowledgements.front());
    if (!acknowledgement_packet) return false;
    const auto acknowledged = decode_packet_ack(acknowledgement_packet->payload);
    if (!acknowledged || *acknowledged != std::vector<std::uint32_t>{1}) return false;
    if (!sender.receive(acknowledgements.front(), start + 4ms) || sender.pending_reliable() != 0) return false;
    return true;
}

bool message_codecs() {
    UseCircuitCode expected;
    expected.circuit_code = 0x10203040;
    for (std::size_t index = 0; index < expected.session_id.size(); ++index) {
        expected.session_id[index] = static_cast<std::byte>(index);
        expected.agent_id[index] = static_cast<std::byte>(index + 16);
    }
    const auto payload = encode_use_circuit_code(expected);
    const auto decoded = decode_use_circuit_code(payload);
    if (!decoded || decoded->circuit_code != expected.circuit_code || decoded->session_id != expected.session_id ||
        decoded->agent_id != expected.agent_id)
        return false;
    if (payload.size() != 40 || payload[0] != std::byte{0xff} || payload[1] != std::byte{0xff} ||
        payload[2] != std::byte{0} || payload[3] != std::byte{3} || payload[4] != std::byte{0x40} ||
        payload[5] != std::byte{0x30} || payload[6] != std::byte{0x20} || payload[7] != std::byte{0x10})
        return false;
    const std::array<std::uint32_t, 2> sequences{0x01020304, 0xa0b0c0d0};
    const auto ack = encode_packet_ack(sequences);
    if (ack.size() != 13 || ack[5] != std::byte{4} || ack[6] != std::byte{3} ||
        ack[7] != std::byte{2} || ack[8] != std::byte{1}) return false;
    const auto ack_decoded = decode_packet_ack(ack);
    if (!ack_decoded || *ack_decoded != std::vector<std::uint32_t>(sequences.begin(), sequences.end())) return false;

    auto reply = bytes({0xff, 0xff, 0x00, 0x95});
    reply.insert(reply.end(), expected.agent_id.begin(), expected.agent_id.end());
    reply.insert(reply.end(), expected.session_id.begin(), expected.session_id.end());
    reply.insert(reply.end(), {std::byte{7}, std::byte{}, std::byte{}, std::byte{}});
    const auto handshake_reply = decode_region_handshake_reply(reply);
    if (!handshake_reply || handshake_reply->agent_id != expected.agent_id ||
        handshake_reply->session_id != expected.session_id) return false;
    reply[3] = std::byte{0xf9};
    const auto movement = decode_complete_agent_movement(reply);
    if (!movement || movement->circuit_code != 7 || movement->agent_id != expected.agent_id) return false;

    RegionHandshake handshake{"Test Region", expected.agent_id, expected.session_id, 21.5F};
    const auto encoded_handshake = encode_region_handshake(handshake);
    if (encoded_handshake.size() < 250 || encoded_handshake[3] != std::byte{0x94}) return false;
    AgentMovementComplete complete;
    complete.agent_id = expected.agent_id;
    complete.session_id = expected.session_id;
    complete.region_handle = 0x0102030405060708ULL;
    const auto encoded_complete = encode_agent_movement_complete(complete);
    if (encoded_complete.size() <= 80 || encoded_complete[3] != std::byte{0xfa}) return false;
    const auto ping = encode_start_ping_check(7, 0x01020304);
    const auto ping_id = decode_start_ping_check(ping);
    return ping == bytes({1, 7, 4, 3, 2, 1}) && ping_id && *ping_id == 7 &&
           !decode_start_ping_check(bytes({1, 7})) && encode_complete_ping_check(*ping_id) == bytes({2, 7});
}

bool resend_throttle_and_timeout() {
    const auto start = Circuit::Clock::time_point{};
    Circuit circuit(start, 1200, 2s);
    if (!circuit.send(bytes({1}), true, start)) return false;
    if (!circuit.poll(start + 400ms).empty()) return false;
    const auto resend = circuit.poll(start + 600ms);
    if (resend.size() != 1) return false;
    const auto packet = decode_packet(resend.front());
    if (!packet || !(packet->flags & flag_resent)) return false;
    const std::vector<std::byte> oversized(2000, std::byte{1});
    if (circuit.send(oversized, false, start + 601ms)) return false;
    return !circuit.expired(start + 2s) && circuit.expired(start + 3s);
}

bool circuit_registry() {
    const auto start = Circuit::Clock::time_point{};
    const auto session = parse_uuid("11111111-2222-4333-8444-555555555555");
    const auto agent = parse_uuid("aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee");
    if (!session || !agent || parse_uuid("not-a-uuid")) return false;
    if (format_uuid(*session) != "11111111-2222-4333-8444-555555555555") return false;
    UseCircuitCode expected{987654, *session, *agent};
    unsigned authorizations = 0;
    CircuitRegistry registry([&](const UseCircuitCode& candidate) {
        ++authorizations;
        return candidate.circuit_code == expected.circuit_code && candidate.session_id == expected.session_id &&
               candidate.agent_id == expected.agent_id;
    });
    Packet opening;
    opening.flags = flag_reliable;
    opening.sequence = 42;
    opening.payload = encode_use_circuit_code(expected);
    const auto datagram = encode_packet(opening);
    if (!registry.receive("127.0.0.1:50000", datagram, start) || registry.size() != 1 || authorizations != 1)
        return false;
    if (!registry.identity("127.0.0.1:50000") ||
        registry.identity("127.0.0.1:50000")->circuit_code != expected.circuit_code)
        return false;
    if (registry.receive("127.0.0.1:50001", datagram, start + 1ms) || registry.size() != 1) return false;
    const auto replies = registry.poll(start + 2ms);
    if (replies.size() != 1 || replies.front().endpoint != "127.0.0.1:50000") return false;
    const auto reply = decode_packet(replies.front().bytes);
    if (!reply || !decode_packet_ack(reply->payload)) return false;
    if (!registry.send("127.0.0.1:50000", bytes({7, 8}), true, start + 3ms) ||
        registry.send("127.0.0.1:50002", bytes({7, 8}), false, start + 3ms))
        return false;
    registry.poll(start + 31s);
    return registry.size() == 0;
}

bool agent_update_codec() {
    auto payload = bytes({4});
    const auto agent = parse_uuid("aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee");
    const auto session = parse_uuid("11111111-2222-4333-8444-555555555555");
    if (!agent || !session) return false;
    payload.insert(payload.end(), agent->begin(), agent->end());
    payload.insert(payload.end(), session->begin(), session->end());
    payload.resize(115, std::byte{});
    // Body rotation x = 1.0, camera center x = 2.0, draw distance = 128.0.
    payload[35] = std::byte{0x80}; payload[36] = std::byte{0x3f};
    payload[60] = std::byte{0x00}; payload[61] = std::byte{0x40};
    payload[108] = std::byte{0x00}; payload[109] = std::byte{0x43};
    payload[110] = std::byte{0x01}; payload[111] = std::byte{0x20};
    const auto update = decode_agent_update(payload);
    return update && update->agent_id == *agent && update->session_id == *session &&
           update->body_rotation[0] == 1.0F && update->camera_center[0] == 2.0F &&
           update->draw_distance == 128.0F && update->control_flags == 0x2001;
}

bool chat_codecs() {
    const auto agent = parse_uuid("aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee");
    const auto session = parse_uuid("11111111-2222-4333-8444-555555555555");
    if (!agent || !session) return false;
    auto payload = bytes({0xff, 0xff, 0x00, 0x50});
    payload.insert(payload.end(), agent->begin(), agent->end());
    payload.insert(payload.end(), session->begin(), session->end());
    payload.insert(payload.end(), {std::byte{3}, std::byte{}, std::byte{'h'}, std::byte{'i'}, std::byte{},
                                   std::byte{1}, std::byte{}, std::byte{}, std::byte{}, std::byte{}});
    const auto incoming = decode_chat_from_viewer(payload);
    if (!incoming || incoming->message != "hi" || incoming->type != 1 || incoming->channel != 0) return false;
    ChatFromSimulator outgoing{"Test User", *agent, *agent, 1, 1, 1, {1.F, 2.F, 3.F}, "hello"};
    const auto encoded = encode_chat_from_simulator(outgoing);
    return encoded.size() > 60 && encoded[3] == std::byte{0x8b};
}

bool flat_terrain_codec() {
    const std::array<TerrainPatch, 2> patches{{{1, 0}, {15, 15}}};
    const auto terrain = encode_flat_terrain(patches, 25.0F);
    if (terrain.size() < 20 || terrain[0] != std::byte{11} || terrain[1] != std::byte{0x4c}) return false;
    const auto length = std::to_integer<unsigned>(terrain[2]) |
                        (std::to_integer<unsigned>(terrain[3]) << 8);
    return length + 4 == terrain.size() && terrain[4] == std::byte{8} && terrain[5] == std::byte{1} &&
           terrain[6] == std::byte{16} && terrain[7] == std::byte{0x4c} && terrain[8] == std::byte{0x84} &&
           terrain[9] == std::byte{} && terrain[10] == std::byte{} && terrain[11] == std::byte{0xc4} &&
           terrain[12] == std::byte{0x41} && terrain[13] == std::byte{1} && terrain[14] == std::byte{} &&
           terrain[15] == std::byte{0x20} && terrain[16] == std::byte{0x28};
}

bool static_object_codec() {
    StaticObject object;
    object.id = *parse_uuid("12345678-1234-4234-8234-123456789abc");
    const auto encoded = encode_static_object_update(0x0102030405060708ULL, object);
    if (encoded.size() <= 220 || encoded[0] != std::byte{12} || encoded[1] != std::byte{8} ||
        encoded[8] != std::byte{1} || encoded[11] != std::byte{1} || encoded[37] != std::byte{9} ||
        encoded[136] != std::byte{})
        return false;
    const auto agent = *parse_uuid("aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee");
    const auto avatar = encode_avatar_object_update(0x0102030405060708ULL, 42, agent, {128.F, 128.F, 25.F});
    return avatar.size() == encoded.size() && avatar[37] == std::byte{47} && avatar[38] == std::byte{4} &&
           std::equal(agent.begin(), agent.end(), avatar.begin() + 17);
}
}

int main() {
    if (!packet_round_trip()) return 1;
    if (!message_codecs()) return 2;
    if (!reliability()) return 3;
    if (!resend_throttle_and_timeout()) return 4;
    if (!circuit_registry()) return 5;
    if (!agent_update_codec()) return 6;
    if (!chat_codecs()) return 7;
    if (!flat_terrain_codec()) return 8;
    if (!static_object_codec()) return 9;
    if (decode_packet(std::array<std::byte, 2>{})) return 10;
    return 0;
}
