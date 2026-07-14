#include "homeworldz/viewer_protocol.h"

#include <array>
#include <chrono>
#include <cstring>

namespace {
using namespace homeworldz::viewer;
using namespace std::chrono_literals;

std::vector<std::byte> bytes(std::initializer_list<unsigned> values) {
    std::vector<std::byte> result;
    for (const auto value : values) result.push_back(static_cast<std::byte>(value));
    return result;
}

void write_f32(std::vector<std::byte>& output, std::size_t offset, float value) {
    std::uint32_t bits{};
    std::memcpy(&bits, &value, sizeof(bits));
    for (std::size_t index = 0; index < 4; ++index)
        output[offset + index] = static_cast<std::byte>(bits >> (index * 8));
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
    const auto economy = encode_economy_data(0, 15000, 3);
    auto logout_payload = bytes({0xff, 0xff, 0x00, 0xfc});
    logout_payload.insert(logout_payload.end(), expected.agent_id.begin(), expected.agent_id.end());
    logout_payload.insert(logout_payload.end(), expected.session_id.begin(), expected.session_id.end());
    const auto logout = decode_logout_request(logout_payload);
    const auto logout_reply = logout ? encode_logout_reply(*logout) : std::vector<std::byte>{};
    auto create_folder_payload = bytes({0xff, 0xff, 0x01, 0x11});
    create_folder_payload.insert(create_folder_payload.end(), expected.agent_id.begin(), expected.agent_id.end());
    create_folder_payload.insert(create_folder_payload.end(), expected.session_id.begin(), expected.session_id.end());
    create_folder_payload.insert(create_folder_payload.end(), expected.session_id.begin(), expected.session_id.end());
    create_folder_payload.insert(create_folder_payload.end(), expected.agent_id.begin(), expected.agent_id.end());
    create_folder_payload.push_back(std::byte{0xff});
    create_folder_payload.push_back(std::byte{9});
    for (const char value : std::string("Projects\0", 9))
        create_folder_payload.push_back(static_cast<std::byte>(value));
    const auto create_folder = decode_create_inventory_folder(create_folder_payload);
    auto copy_item_payload = bytes({0xff, 0xff, 0x01, 0x0d});
    copy_item_payload.insert(copy_item_payload.end(), expected.agent_id.begin(), expected.agent_id.end());
    copy_item_payload.insert(copy_item_payload.end(), expected.session_id.begin(), expected.session_id.end());
    copy_item_payload.insert(copy_item_payload.end(),
                             {std::byte{1}, std::byte{0x78}, std::byte{0x56}, std::byte{0x34}, std::byte{0x12}});
    copy_item_payload.insert(copy_item_payload.end(), expected.agent_id.begin(), expected.agent_id.end());
    copy_item_payload.insert(copy_item_payload.end(), expected.session_id.begin(), expected.session_id.end());
    copy_item_payload.insert(copy_item_payload.end(), 16, std::byte{});
    copy_item_payload.push_back(std::byte{1});
    copy_item_payload.push_back(std::byte{});
    const auto copy_item = decode_copy_inventory_item(copy_item_payload);
    InventoryItem copied_item;
    copied_item.item_id = expected.session_id;
    copied_item.creator_id = expected.agent_id;
    copied_item.owner_id = expected.agent_id;
    copied_item.folder_id = expected.agent_id;
    copied_item.asset_id = expected.session_id;
    copied_item.asset_type = 5;
    copied_item.inventory_type = 18;
    copied_item.name = "Default Shirt";
    copied_item.flags = 4;
    copied_item.base_permissions = 0x7fffffff;
    copied_item.current_permissions = 0x7fffffff;
    copied_item.next_permissions = 0x7fffffff;
    const auto copy_reply = copy_item ? encode_update_create_inventory_item(
        AgentMessage{expected.agent_id, expected.session_id}, copy_item->callback_id, copied_item)
                                      : std::vector<std::byte>{};
    auto move_folder_payload = bytes({0xff, 0xff, 0x01, 0x13});
    move_folder_payload.insert(move_folder_payload.end(), expected.agent_id.begin(), expected.agent_id.end());
    move_folder_payload.insert(move_folder_payload.end(), expected.session_id.begin(), expected.session_id.end());
    move_folder_payload.push_back(std::byte{1});
    move_folder_payload.push_back(std::byte{2});
    move_folder_payload.insert(move_folder_payload.end(), expected.agent_id.begin(), expected.agent_id.end());
    move_folder_payload.insert(move_folder_payload.end(), expected.session_id.begin(), expected.session_id.end());
    move_folder_payload.insert(move_folder_payload.end(), expected.session_id.begin(), expected.session_id.end());
    move_folder_payload.insert(move_folder_payload.end(), expected.agent_id.begin(), expected.agent_id.end());
    const auto move_folder = decode_move_inventory_folder(move_folder_payload);
    auto move_item_payload = bytes({0xff, 0xff, 0x01, 0x0c});
    move_item_payload.insert(move_item_payload.end(), expected.agent_id.begin(), expected.agent_id.end());
    move_item_payload.insert(move_item_payload.end(), expected.session_id.begin(), expected.session_id.end());
    move_item_payload.push_back(std::byte{1});
    move_item_payload.push_back(std::byte{2});
    move_item_payload.insert(move_item_payload.end(), expected.agent_id.begin(), expected.agent_id.end());
    move_item_payload.insert(move_item_payload.end(), expected.session_id.begin(), expected.session_id.end());
    move_item_payload.push_back(std::byte{});
    move_item_payload.insert(move_item_payload.end(), expected.session_id.begin(), expected.session_id.end());
    move_item_payload.insert(move_item_payload.end(), expected.agent_id.begin(), expected.agent_id.end());
    move_item_payload.push_back(std::byte{8});
    for (const char value : std::string("Renamed\0", 8))
        move_item_payload.push_back(static_cast<std::byte>(value));
    const auto move_item = decode_move_inventory_item(move_item_payload);
    std::vector<std::byte> object_add_payload(146);
    object_add_payload[0] = std::byte{0xff};
    object_add_payload[1] = std::byte{0x01};
    std::copy(expected.agent_id.begin(), expected.agent_id.end(), object_add_payload.begin() + 2);
    std::copy(expected.session_id.begin(), expected.session_id.end(), object_add_payload.begin() + 18);
    std::copy(expected.agent_id.begin(), expected.agent_id.end(), object_add_payload.begin() + 34);
    object_add_payload[50] = std::byte{9};
    object_add_payload[51] = std::byte{3};
    object_add_payload[52] = std::byte{0x02};
    object_add_payload[56] = std::byte{16};
    object_add_payload[57] = std::byte{1};
    object_add_payload[79] = std::byte{1};
    write_f32(object_add_payload, 80, 128.0F);
    write_f32(object_add_payload, 84, 128.0F);
    write_f32(object_add_payload, 88, 30.0F);
    write_f32(object_add_payload, 92, 132.0F);
    write_f32(object_add_payload, 96, 129.0F);
    write_f32(object_add_payload, 100, 22.0F);
    write_f32(object_add_payload, 121, 0.5F);
    write_f32(object_add_payload, 125, 0.75F);
    write_f32(object_add_payload, 129, 1.0F);
    const auto object_add = decode_object_add(object_add_payload);
    auto derez_payload = bytes({0xff, 0xff, 0x01, 0x23});
    derez_payload.insert(derez_payload.end(), expected.agent_id.begin(), expected.agent_id.end());
    derez_payload.insert(derez_payload.end(), expected.session_id.begin(), expected.session_id.end());
    derez_payload.insert(derez_payload.end(), 16, std::byte{});
    derez_payload.push_back(std::byte{6});
    derez_payload.insert(derez_payload.end(), expected.agent_id.begin(), expected.agent_id.end());
    derez_payload.insert(derez_payload.end(), expected.session_id.begin(), expected.session_id.end());
    derez_payload.insert(derez_payload.end(), {std::byte{1}, std::byte{1}, std::byte{2},
                                               std::byte{0x78}, std::byte{0x56},
                                               std::byte{0x34}, std::byte{0x12},
                                               std::byte{0x04}, std::byte{0x03},
                                               std::byte{0x02}, std::byte{0x01}});
    const auto derez = decode_derez_object(derez_payload);
    const std::array<std::uint32_t, 2> killed_ids{0x12345678, 0x01020304};
    const auto killed = encode_kill_object(killed_ids);
    auto select_payload = bytes({0xff, 0xff, 0x00, 0x6e});
    select_payload.insert(select_payload.end(), expected.agent_id.begin(), expected.agent_id.end());
    select_payload.insert(select_payload.end(), expected.session_id.begin(), expected.session_id.end());
    select_payload.insert(select_payload.end(), {std::byte{1}, std::byte{0x78}, std::byte{0x56},
                                                  std::byte{0x34}, std::byte{0x12}});
    const auto selected = decode_object_select(select_payload);
    auto transform_payload = bytes({0xff, 0x02});
    transform_payload.insert(transform_payload.end(), expected.agent_id.begin(), expected.agent_id.end());
    transform_payload.insert(transform_payload.end(), expected.session_id.begin(), expected.session_id.end());
    transform_payload.insert(transform_payload.end(), {std::byte{1}, std::byte{0x78}, std::byte{0x56},
                                                        std::byte{0x34}, std::byte{0x12}, std::byte{0x0d},
                                                        std::byte{24}});
    const auto transform_data = transform_payload.size();
    transform_payload.resize(transform_data + 24);
    write_f32(transform_payload, transform_data, 130.0F);
    write_f32(transform_payload, transform_data + 4, 131.0F);
    write_f32(transform_payload, transform_data + 8, 25.0F);
    write_f32(transform_payload, transform_data + 12, 1.0F);
    write_f32(transform_payload, transform_data + 16, 2.0F);
    write_f32(transform_payload, transform_data + 20, 3.0F);
    const auto transform = decode_multiple_object_update(transform_payload);
    auto object_name_payload = bytes({0xff, 0xff, 0x00, 0x6b});
    object_name_payload.insert(object_name_payload.end(), expected.agent_id.begin(), expected.agent_id.end());
    object_name_payload.insert(object_name_payload.end(), expected.session_id.begin(), expected.session_id.end());
    object_name_payload.insert(object_name_payload.end(), {std::byte{1}, std::byte{0x78}, std::byte{0x56},
                                                            std::byte{0x34}, std::byte{0x12}, std::byte{6}});
    for (const char value : std::string("Prim1\0", 6))
        object_name_payload.push_back(static_cast<std::byte>(value));
    const auto object_name = decode_object_name(object_name_payload);
    auto family_payload = bytes({0xff, 0x05});
    family_payload.insert(family_payload.end(), expected.agent_id.begin(), expected.agent_id.end());
    family_payload.insert(family_payload.end(), expected.session_id.begin(), expected.session_id.end());
    family_payload.insert(family_payload.end(), {std::byte{0x04}, std::byte{0x03},
                                                  std::byte{0x02}, std::byte{0x01}});
    family_payload.insert(family_payload.end(), expected.agent_id.begin(), expected.agent_id.end());
    const auto family = decode_request_object_properties_family(family_payload);
    ObjectProperties properties;
    properties.object_id = expected.agent_id;
    properties.creator_id = expected.agent_id;
    properties.owner_id = expected.session_id;
    properties.creation_date = 123;
    properties.name = "Primitive";
    const std::array property_list{properties};
    const auto encoded_properties = encode_object_properties(property_list);
    const auto encoded_family = encode_object_properties_family(0x01020304, properties);
    auto name_request_payload = bytes({0xff, 0xff, 0x00, 0xeb, 0x02});
    name_request_payload.insert(name_request_payload.end(), expected.agent_id.begin(), expected.agent_id.end());
    name_request_payload.insert(name_request_payload.end(), expected.session_id.begin(), expected.session_id.end());
    const auto name_request = decode_uuid_name_request(name_request_payload);
    const std::array names{
        UuidName{expected.agent_id, "Jim", "Tarber"},
        UuidName{expected.session_id, "Demo", "Avatar"}};
    const auto name_reply = encode_uuid_name_reply(names);
    auto cached_payload = bytes({0xff, 0xff, 0x01, 0x80});
    cached_payload.insert(cached_payload.end(), expected.agent_id.begin(), expected.agent_id.end());
    cached_payload.insert(cached_payload.end(), expected.session_id.begin(), expected.session_id.end());
    cached_payload.insert(cached_payload.end(), {std::byte{7}, std::byte{}, std::byte{}, std::byte{}, std::byte{2}});
    cached_payload.insert(cached_payload.end(), 16, std::byte{1});
    cached_payload.push_back(std::byte{8});
    cached_payload.insert(cached_payload.end(), 16, std::byte{2});
    cached_payload.push_back(std::byte{9});
    const auto cached = decode_agent_cached_texture(cached_payload);
    const auto cached_response = cached ? encode_agent_cached_texture_response(*cached) : std::vector<std::byte>{};
    auto appearance_payload = bytes({0xff, 0xff, 0x00, 0x54});
    appearance_payload.insert(appearance_payload.end(), expected.agent_id.begin(), expected.agent_id.end());
    appearance_payload.insert(appearance_payload.end(), expected.session_id.begin(), expected.session_id.end());
    appearance_payload.resize(52, std::byte{});
    appearance_payload.push_back(std::byte{1});
    appearance_payload.insert(appearance_payload.end(), expected.session_id.begin(), expected.session_id.end());
    appearance_payload.push_back(std::byte{8});
    appearance_payload.insert(appearance_payload.end(), {std::byte{35}, std::byte{0}});
    appearance_payload.insert(appearance_payload.end(), expected.session_id.begin(), expected.session_id.end());
    appearance_payload.insert(appearance_payload.end(), {std::byte{0x82}, std::byte{0}});
    appearance_payload.insert(appearance_payload.end(), expected.agent_id.begin(), expected.agent_id.end());
    appearance_payload.insert(appearance_payload.end(), {std::byte{0}, std::byte{0}});
    const auto appearance = decode_agent_set_appearance(appearance_payload);
    auto image_payload = bytes({8});
    image_payload.insert(image_payload.end(), expected.agent_id.begin(), expected.agent_id.end());
    image_payload.insert(image_payload.end(), expected.session_id.begin(), expected.session_id.end());
    image_payload.push_back(std::byte{1});
    image_payload.insert(image_payload.end(), expected.agent_id.begin(), expected.agent_id.end());
    image_payload.insert(image_payload.end(),
                         {std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0x80}, std::byte{0x3f},
                          std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0}, std::byte{1}});
    const auto image_request = decode_request_image(image_payload);
    const std::vector<std::byte> image_content(1601, std::byte{0x5a});
    const auto image_transfer = encode_image_transfer(expected.agent_id, image_content);
    const auto resumed_transfer = encode_image_transfer(expected.agent_id, image_content, 2);
    return ping == bytes({1, 7, 4, 3, 2, 1}) && ping_id && *ping_id == 7 &&
           !decode_start_ping_check(bytes({1, 7})) && encode_complete_ping_check(*ping_id) == bytes({2, 7}) &&
           is_economy_data_request(bytes({0xff, 0xff, 0x00, 0x18})) &&
           !is_economy_data_request(bytes({0xff, 0xff, 0x00, 0x19})) &&
           economy.size() == 72 && economy[3] == std::byte{0x19} &&
           economy[4] == std::byte{0x98} && economy[5] == std::byte{0x3a} &&
           economy[8] == std::byte{3} && economy[36] == std::byte{0} &&
           economy[52] == std::byte{0} && economy[53] == std::byte{0} &&
           economy[54] == std::byte{0x80} && economy[55] == std::byte{0x3f} &&
           logout && logout->agent_id == expected.agent_id && logout->session_id == expected.session_id &&
           create_folder && create_folder->agent_id == expected.agent_id &&
           create_folder->session_id == expected.session_id && create_folder->folder_id == expected.session_id &&
           create_folder->parent_id == expected.agent_id && create_folder->type == -1 &&
           create_folder->name == "Projects" &&
           copy_item && copy_item->agent_id == expected.agent_id &&
           copy_item->session_id == expected.session_id && copy_item->old_agent_id == expected.agent_id &&
           copy_item->old_item_id == expected.session_id && copy_item->callback_id == 0x12345678 &&
           copy_item->new_name.empty() && copy_reply.size() > 180 && copy_reply[3] == std::byte{0x0b} &&
           copy_reply[70] == std::byte{0x78} && copy_reply[71] == std::byte{0x56} &&
           move_folder && move_folder->agent_id == expected.agent_id &&
           move_folder->session_id == expected.session_id && move_folder->stamp &&
           move_folder->folders.size() == 2 && move_folder->folders[0].folder_id == expected.agent_id &&
           move_folder->folders[0].parent_id == expected.session_id &&
           move_folder->folders[1].folder_id == expected.session_id &&
           move_folder->folders[1].parent_id == expected.agent_id &&
           move_item && move_item->agent_id == expected.agent_id &&
           move_item->session_id == expected.session_id && move_item->stamp &&
           move_item->items.size() == 2 && move_item->items[0].item_id == expected.agent_id &&
           move_item->items[0].folder_id == expected.session_id && move_item->items[0].new_name.empty() &&
           move_item->items[1].item_id == expected.session_id &&
           move_item->items[1].folder_id == expected.agent_id && move_item->items[1].new_name == "Renamed" &&
           object_add && object_add->agent_id == expected.agent_id &&
           object_add->session_id == expected.session_id && object_add->group_id == expected.agent_id &&
           object_add->pcode == 9 && object_add->material == 3 && object_add->add_flags == 2 &&
           object_add->path_curve == 16 && object_add->profile_curve == 1 && object_add->bypass_raycast &&
           object_add->ray_start[2] == 30.0F && object_add->ray_end[0] == 132.0F &&
           object_add->scale[1] == 0.75F &&
           derez && derez->agent_id == expected.agent_id && derez->session_id == expected.session_id &&
           derez->destination == 6 && derez->destination_id == expected.agent_id &&
           derez->transaction_id == expected.session_id && derez->packet_count == 1 &&
           derez->packet_number == 1 && derez->local_ids == std::vector<std::uint32_t>(killed_ids.begin(), killed_ids.end()) &&
           killed == bytes({0x10, 2, 0x78, 0x56, 0x34, 0x12, 0x04, 0x03, 0x02, 0x01}) &&
           selected && selected->agent_id == expected.agent_id && selected->session_id == expected.session_id &&
           selected->local_ids == std::vector<std::uint32_t>{0x12345678} &&
           transform && transform->agent_id == expected.agent_id &&
           transform->session_id == expected.session_id && transform->objects.size() == 1 &&
           transform->objects[0].local_id == 0x12345678 && transform->objects[0].type == 0x0d &&
           transform->objects[0].position && (*transform->objects[0].position)[1] == 131.0F &&
           !transform->objects[0].rotation && transform->objects[0].scale &&
           (*transform->objects[0].scale)[2] == 3.0F &&
           object_name && object_name->agent_id == expected.agent_id &&
           object_name->session_id == expected.session_id && object_name->objects.size() == 1 &&
           object_name->objects[0].local_id == 0x12345678 && object_name->objects[0].name == "Prim1" &&
           family && family->request_flags == 0x01020304 && family->object_id == expected.agent_id &&
           encoded_properties.size() > 180 && encoded_properties[0] == std::byte{0xff} &&
           encoded_properties[1] == std::byte{0x09} && encoded_properties[2] == std::byte{1} &&
           encoded_properties[75] == std::byte{0x00} && encoded_properties[76] == std::byte{0xe0} &&
           encoded_properties[77] == std::byte{0x09} && encoded_properties[104] == std::byte{0x3f} &&
           encoded_family.size() > 100 && encoded_family[1] == std::byte{0x0a} &&
           encoded_family[2] == std::byte{0x04} && encoded_family[54] == std::byte{0x00} &&
           encoded_family[55] == std::byte{0xe0} && encoded_family[56] == std::byte{0x09} &&
           name_request && name_request->size() == 2 && (*name_request)[0] == expected.agent_id &&
           (*name_request)[1] == expected.session_id && name_reply.size() == 64 &&
           name_reply[0] == std::byte{0xff} && name_reply[3] == std::byte{0xec} &&
           name_reply[4] == std::byte{2} && name_reply[21] == std::byte{4} &&
           logout_reply.size() == 53 && logout_reply[3] == std::byte{0xfd} && logout_reply[36] == std::byte{1} &&
           cached && cached->serial == 7 && cached->queries.size() == 2 &&
           cached->queries[0].texture_index == 8 && cached->queries[1].texture_index == 9 &&
           cached_response.size() == 79 && cached_response[3] == std::byte{0x81} &&
           cached_response[40] == std::byte{2} && cached_response[57] == std::byte{8} &&
           cached_response[76] == std::byte{9} && appearance && appearance->cache_entries.size() == 1 &&
           appearance->cache_entries[0].cache_id == expected.session_id &&
           appearance->cache_entries[0].texture_index == 8 && appearance->texture_ids[8] == expected.agent_id &&
           appearance->texture_ids[9] == expected.session_id && image_request && image_request->requests.size() == 1 &&
           image_request->requests[0].image_id == expected.agent_id &&
           image_request->requests[0].download_priority == 1.0F && image_request->requests[0].type == 1 &&
           image_transfer.size() == 3 && image_transfer[0].size() == 626 &&
           image_transfer[0][0] == std::byte{9} && image_transfer[0][22] == std::byte{3} &&
           image_transfer[1].size() == 1021 && image_transfer[1][0] == std::byte{10} &&
           image_transfer[1][17] == std::byte{1} && image_transfer[2].size() == 22 &&
           resumed_transfer.size() == 1 && resumed_transfer[0] == image_transfer[2];
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
    if (!registry.remove("127.0.0.1:50000") || registry.remove("127.0.0.1:50000") || registry.size() != 0)
        return false;
    if (!registry.receive("127.0.0.1:50000", datagram, start + 4ms)) return false;
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
    if (!(length + 4 == terrain.size() && terrain[4] == std::byte{8} && terrain[5] == std::byte{1} &&
           terrain[6] == std::byte{16} && terrain[7] == std::byte{0x4c} && terrain[8] == std::byte{0x84} &&
           terrain[9] == std::byte{} && terrain[10] == std::byte{} && terrain[11] == std::byte{0xc4} &&
           terrain[12] == std::byte{0x41} && terrain[13] == std::byte{1} && terrain[14] == std::byte{} &&
           terrain[15] == std::byte{0x20} && terrain[16] == std::byte{0x28})) return false;
    std::array<float, 256 * 256> heightmap{};
    for (std::size_t y = 0; y < 256; ++y)
        for (std::size_t x = 0; x < 256; ++x)
            heightmap[y * 256 + x] = 10.0F + static_cast<float>(x + y) / 16.0F;
    const auto shaped = encode_terrain(patches, heightmap);
    return shaped.size() > terrain.size() && shaped[0] == std::byte{11} && shaped[1] == std::byte{0x4c} &&
           encode_terrain(patches, std::span<const float>(heightmap.data(), 100)).empty();
}

bool static_object_codec() {
    StaticObject object;
    object.id = *parse_uuid("12345678-1234-4234-8234-123456789abc");
    object.update_flags = 0x1002013c;
    object.rotation = {0.25F, 0.0F, 0.0F};
    const auto encoded = encode_static_object_update(0x0102030405060708ULL, object);
    if (encoded.size() <= 220 || encoded[0] != std::byte{12} || encoded[1] != std::byte{8} ||
        encoded[8] != std::byte{1} || encoded[11] != std::byte{1} || encoded[37] != std::byte{9} ||
        encoded[89] != std::byte{0x00} || encoded[90] != std::byte{0x00} ||
        encoded[91] != std::byte{0x80} || encoded[92] != std::byte{0x3e} ||
        encoded[117] != std::byte{0x3c} || encoded[118] != std::byte{0x01} ||
        encoded[119] != std::byte{0x02} || encoded[120] != std::byte{0x10} ||
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
