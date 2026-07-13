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
        payload[2] != std::byte{0} || payload[3] != std::byte{3})
        return false;
    const std::array<std::uint32_t, 2> sequences{0x01020304, 0xa0b0c0d0};
    const auto ack = encode_packet_ack(sequences);
    const auto ack_decoded = decode_packet_ack(ack);
    return ack_decoded && *ack_decoded == std::vector<std::uint32_t>(sequences.begin(), sequences.end());
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
}

int main() {
    if (!packet_round_trip()) return 1;
    if (!message_codecs()) return 2;
    if (!reliability()) return 3;
    if (!resend_throttle_and_timeout()) return 4;
    if (!circuit_registry()) return 5;
    if (decode_packet(std::array<std::byte, 2>{})) return 6;
    return 0;
}
