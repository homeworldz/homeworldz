#include "homeworldz/viewer_capabilities.h"

#include "homeworldz/viewer_protocol.h"

#include <array>
#include <charconv>
#include <random>
#include <span>

namespace homeworldz::viewer {
namespace {
std::string xml_escape(std::string_view value) {
    std::string result;
    for (const char character : value) {
        if (character == '&') result += "&amp;";
        else if (character == '<') result += "&lt;";
        else if (character == '>') result += "&gt;";
        else if (character == '\"') result += "&quot;";
        else if (character == '\'') result += "&apos;";
        else result.push_back(character);
    }
    return result;
}

std::string xml_unescape(std::string_view value) {
    std::string result;
    for (std::size_t index = 0; index < value.size();) {
        if (value.substr(index).starts_with("&amp;")) {
            result.push_back('&');
            index += 5;
        } else if (value.substr(index).starts_with("&lt;")) {
            result.push_back('<');
            index += 4;
        } else if (value.substr(index).starts_with("&gt;")) {
            result.push_back('>');
            index += 4;
        } else if (value.substr(index).starts_with("&quot;")) {
            result.push_back('"');
            index += 6;
        } else if (value.substr(index).starts_with("&apos;")) {
            result.push_back('\'');
            index += 6;
        } else {
            result.push_back(value[index++]);
        }
    }
    return result;
}

std::optional<std::string> llsd_value(std::string_view xml, std::string_view key) {
    const auto marker = "<key>" + std::string(key) + "</key>";
    const auto found = xml.find(marker);
    if (found == std::string_view::npos) return std::nullopt;
    auto position = found + marker.size();
    while (position < xml.size() && (xml[position] == ' ' || xml[position] == '\r' ||
           xml[position] == '\n' || xml[position] == '\t')) ++position;
    for (const std::string_view tag : {"string", "uuid", "integer"}) {
        const auto open = '<' + std::string(tag) + '>';
        if (!xml.substr(position).starts_with(open)) continue;
        position += open.size();
        const auto close = "</" + std::string(tag) + '>';
        const auto end = xml.find(close, position);
        if (end == std::string_view::npos) return std::nullopt;
        return xml_unescape(xml.substr(position, end - position));
    }
    return std::nullopt;
}

std::optional<std::uint32_t> llsd_u32(std::string_view xml, std::string_view key) {
    const auto value = llsd_value(xml, key);
    if (!value) return std::nullopt;
    std::uint32_t parsed{};
    const auto result = std::from_chars(value->data(), value->data() + value->size(), parsed);
    if (result.ec != std::errc{} || result.ptr != value->data() + value->size()) return std::nullopt;
    return parsed;
}

std::string base64(std::span<const std::uint8_t> bytes) {
    static constexpr std::string_view alphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    result.reserve(((bytes.size() + 2) / 3) * 4);
    for (std::size_t offset = 0; offset < bytes.size(); offset += 3) {
        const auto remaining = bytes.size() - offset;
        const std::uint32_t value =
            static_cast<std::uint32_t>(bytes[offset]) << 16 |
            (remaining > 1 ? static_cast<std::uint32_t>(bytes[offset + 1]) << 8 : 0U) |
            (remaining > 2 ? static_cast<std::uint32_t>(bytes[offset + 2]) : 0U);
        result.push_back(alphabet[(value >> 18) & 0x3fU]);
        result.push_back(alphabet[(value >> 12) & 0x3fU]);
        result.push_back(remaining > 1 ? alphabet[(value >> 6) & 0x3fU] : '=');
        result.push_back(remaining > 2 ? alphabet[value & 0x3fU] : '=');
    }
    return result;
}

std::string region_handle_binary(std::uint64_t handle) {
    std::array<std::uint8_t, 8> bytes{};
    for (std::size_t index = 0; index < bytes.size(); ++index)
        bytes[index] = static_cast<std::uint8_t>(handle >> ((7 - index) * 8));
    return base64(bytes);
}

std::string ip_binary(const SimulatorEventEndpoint& simulator) {
    return base64(simulator.address);
}
} // namespace

std::string seed_capability_xml(std::string_view public_endpoint, std::string_view grid_public_endpoint,
                                std::string_view session_id, std::string_view visit_id) {
    auto base = std::string(public_endpoint);
    while (!base.empty() && base.back() == '/') base.pop_back();
    const auto visit_suffix = visit_id.empty() ? std::string{} : "/" + std::string(visit_id);
    const auto event_url = xml_escape(base + "/caps/event/" + std::string(session_id) + visit_suffix);
    const auto texture_url = xml_escape(base + "/caps/texture/" + std::string(session_id));
    const auto asset_url = xml_escape(base + "/caps/assets/" + std::string(session_id));
    const auto simulator_features_url =
        xml_escape(base + "/caps/simulator-features/" + std::string(session_id));
    const auto environment_url = xml_escape(base + "/caps/environment/" + std::string(session_id));
    const auto baked_upload_url = xml_escape(base + "/caps/upload-baked/" + std::string(session_id));
    const auto file_upload_url = xml_escape(base + "/caps/upload-file/" + std::string(session_id));
    auto grid_base = std::string(grid_public_endpoint);
    while (!grid_base.empty() && grid_base.back() == '/') grid_base.pop_back();
    const auto inventory_url = xml_escape(grid_base + "/caps/inventory/descendents/" + std::string(session_id));
    const auto inventory_items_url = xml_escape(grid_base + "/caps/inventory/items/" + std::string(session_id));
    const auto create_inventory_folder_url =
        xml_escape(grid_base + "/caps/inventory/create-folder/" + std::string(session_id));
    const auto inventory_ais_url =
        xml_escape(grid_base + "/caps/inventory/ais/" + std::string(session_id));
    const auto library_ais_url =
        xml_escape(grid_base + "/caps/inventory/library/" + std::string(session_id));
    return "<?xml version=\"1.0\"?><llsd><map><key>EventQueueGet</key><uri>" + event_url +
           "</uri><key>GetTexture</key><uri>" + texture_url +
           "</uri><key>ViewerAsset</key><uri>" + asset_url +
           "</uri><key>SimulatorFeatures</key><uri>" + simulator_features_url +
           "</uri><key>EnvironmentSettings</key><uri>" + environment_url +
           "</uri><key>UploadBakedTexture</key><uri>" + baked_upload_url +
           "</uri><key>NewFileAgentInventory</key><uri>" + file_upload_url +
           "</uri><key>FetchInventoryDescendents2</key><uri>" + inventory_url +
           "</uri><key>FetchInventory2</key><uri>" + inventory_items_url +
           "</uri><key>CreateInventoryCategory</key><uri>" + create_inventory_folder_url +
           "</uri><key>InventoryAPIv3</key><uri>" + inventory_ais_url +
           "</uri><key>LibraryAPIv3</key><uri>" + library_ais_url +
           "</uri></map></llsd>";
}

std::string establish_agent_communication_event_xml(const EstablishAgentCommunication& event) {
    return "<map><key>message</key><string>EstablishAgentCommunication</string>"
           "<key>body</key><map><key>agent-id</key><uuid>" + xml_escape(event.agent_id) +
           "</uuid><key>sim-ip-and-port</key><string>" + xml_escape(event.simulator_endpoint) +
           "</string><key>seed-capability</key><uri>" + xml_escape(event.seed_capability) +
           "</uri></map></map>";
}

std::string enable_simulator_event_xml(std::uint64_t region_handle,
                                       const SimulatorEventEndpoint& simulator) {
    return "<map><key>message</key><string>EnableSimulator</string><key>body</key><map>"
           "<key>SimulatorInfo</key><array><map><key>Handle</key><binary>" +
           region_handle_binary(region_handle) + "</binary><key>IP</key><binary>" +
           ip_binary(simulator) + "</binary><key>Port</key><integer>" +
           std::to_string(simulator.port) + "</integer></map></array></map></map>";
}

std::string teleport_finish_event_xml(const TeleportFinish& event) {
    return "<map><key>message</key><string>TeleportFinish</string><key>body</key><map>"
           "<key>Info</key><array><map><key>AgentID</key><uuid>" + xml_escape(event.agent_id) +
           "</uuid><key>LocationID</key><integer>4</integer><key>RegionHandle</key><binary>" +
           region_handle_binary(event.region_handle) +
           "</binary><key>SeedCapability</key><string>" + xml_escape(event.seed_capability) +
           "</string><key>SimAccess</key><integer>" + std::to_string(event.simulator_access) +
           "</integer><key>SimIP</key><binary>" + ip_binary(event.simulator) +
           "</binary><key>SimPort</key><integer>" + std::to_string(event.simulator.port) +
           "</integer><key>TeleportFlags</key><integer>" + std::to_string(event.teleport_flags) +
           "</integer></map></array></map></map>";
}

std::string event_queue_xml(std::uint64_t id, const std::vector<std::string>& events) {
    std::string encoded_events = events.empty() ? "<array/>" : "<array>";
    for (const auto& event : events) encoded_events += event;
    if (!events.empty()) encoded_events += "</array>";
    return "<?xml version=\"1.0\"?><llsd><map><key>events</key>" + encoded_events +
           "<key>id</key><integer>" + std::to_string(id) + "</integer></map></llsd>";
}

std::string simulator_features_xml(std::string_view currency, std::string_view map_server_url) {
    return "<?xml version=\"1.0\"?><llsd><map><key>OpenSimExtras</key><map>"
           "<key>currency</key><string>" + xml_escape(currency) +
           "</string><key>map-server-url</key><string>" + xml_escape(map_server_url) +
           "</string></map></map></llsd>";
}

std::string environment_settings_xml(std::string_view region_id) {
    return "<?xml version=\"1.0\"?><llsd><array><map>"
           "<key>messageID</key><uuid>00000000-0000-0000-0000-000000000000</uuid>"
           "<key>regionID</key><uuid>" + xml_escape(region_id) +
           "</uuid></map>"
           "<array><array><real>0</real><string>HomeWorldz Default</string></array></array>"
           "<map><key>HomeWorldz Default</key><map>"
           "<key>gamma</key><array><real>1</real><real>0</real><real>0</real><real>1</real></array>"
           "</map></map>"
           "<map><key>blurMultiplier</key><real>0.04</real></map>"
           "</array></llsd>";
}

std::string baked_texture_upload_xml(std::string_view uploader) {
    return "<?xml version=\"1.0\"?><llsd><map><key>state</key><string>upload</string>"
           "<key>uploader</key><uri>" + xml_escape(uploader) + "</uri></map></llsd>";
}

std::string baked_texture_complete_xml(std::string_view asset_id) {
    return "<?xml version=\"1.0\"?><llsd><map><key>state</key><string>complete</string>"
           "<key>new_asset</key><uuid>" + xml_escape(asset_id) + "</uuid></map></llsd>";
}

std::optional<NewFileInventoryUpload> parse_new_file_inventory_upload(std::string_view xml) {
    if (!xml.starts_with("<?xml") && !xml.starts_with("<llsd")) return std::nullopt;
    const auto folder = llsd_value(xml, "folder_id");
    const auto asset_type = llsd_value(xml, "asset_type");
    const auto inventory_type = llsd_value(xml, "inventory_type");
    const auto name = llsd_value(xml, "name");
    const auto description = llsd_value(xml, "description");
    std::int8_t encoded_asset_type{-1};
    std::int8_t encoded_inventory_type{-1};
    if (asset_type == "texture" && inventory_type == "texture") {
        encoded_asset_type = 0;
        encoded_inventory_type = 0;
    } else if ((asset_type == "texture" || asset_type == "snapshot") &&
               inventory_type == "snapshot") {
        encoded_asset_type = 0;
        encoded_inventory_type = 15;
    } else if (asset_type == "sound" && inventory_type == "sound") {
        encoded_asset_type = 1;
        encoded_inventory_type = 1;
    } else if (asset_type == "animation" && inventory_type == "animation") {
        encoded_asset_type = 20;
        encoded_inventory_type = 19;
    }
    if (!folder || !parse_uuid(*folder) || encoded_asset_type < 0 ||
        !name || name->empty() || name->size() > 255 ||
        !description || description->size() > 1024) return std::nullopt;
    NewFileInventoryUpload result{*folder, encoded_asset_type, encoded_inventory_type,
                                  *name, *description};
    if (const auto permissions = llsd_u32(xml, "everyone_mask"))
        result.everyone_permissions = *permissions;
    if (const auto permissions = llsd_u32(xml, "group_mask"))
        result.group_permissions = *permissions;
    if (const auto permissions = llsd_u32(xml, "next_owner_mask"))
        result.next_permissions = *permissions;
    return result;
}

bool valid_new_file_inventory_upload_content(const NewFileInventoryUpload& upload,
                                             std::string_view content) {
    if (content.empty() || content.size() > 1024 * 1024) return false;
    if (upload.asset_type == 0) {
        const auto byte = [&](std::size_t index) {
            return static_cast<unsigned char>(content[index]);
        };
        const bool codestream = content.size() >= 4 &&
                                byte(0) == 0xff && byte(1) == 0x4f &&
                                byte(2) == 0xff && byte(3) == 0x51;
        const bool jp2 = content.size() >= 12 &&
                         byte(0) == 0 && byte(1) == 0 && byte(2) == 0 && byte(3) == 12 &&
                         content.substr(4, 4) == "jP  " && byte(8) == 0x0d &&
                         byte(9) == 0x0a && byte(10) == 0x87 && byte(11) == 0x0a;
        return codestream || jp2;
    }
    if (upload.asset_type == 1)
        return content.size() >= 4 && content.substr(0, 4) == "OggS";
    if (upload.asset_type == 20)
        return content.size() >= 4 &&
               static_cast<unsigned char>(content[0]) == 0x01 &&
               static_cast<unsigned char>(content[1]) == 0x00 &&
               static_cast<unsigned char>(content[2]) == 0x00 &&
               static_cast<unsigned char>(content[3]) == 0x00;
    return false;
}

std::string new_file_inventory_upload_xml(std::string_view uploader) {
    return "<?xml version=\"1.0\"?><llsd><map><key>state</key><string>upload</string>"
           "<key>uploader</key><uri>" + xml_escape(uploader) + "</uri>"
           "<key>resource_cost</key><integer>0</integer>"
           "<key>upload_price</key><integer>0</integer></map></llsd>";
}

std::string new_file_inventory_complete_xml(std::string_view item_id, std::string_view asset_id,
                                            std::uint32_t everyone_permissions,
                                            std::uint32_t next_permissions) {
    return "<?xml version=\"1.0\"?><llsd><map><key>state</key><string>complete</string>"
           "<key>new_inventory_item</key><uuid>" + xml_escape(item_id) + "</uuid>"
           "<key>new_asset</key><uuid>" + xml_escape(asset_id) + "</uuid>"
           "<key>new_base_mask</key><integer>2147483647</integer>"
           "<key>new_owner_mask</key><integer>2147483647</integer>"
           "<key>new_everyone_mask</key><integer>" + std::to_string(everyone_permissions) + "</integer>"
           "<key>new_next_owner_mask</key><integer>" + std::to_string(next_permissions) +
           "</integer></map></llsd>";
}

std::string random_uuid() {
    std::array<unsigned char, 16> bytes{};
    std::random_device source;
    for (auto& value : bytes) value = static_cast<unsigned char>(source());
    bytes[6] = static_cast<unsigned char>((bytes[6] & 0x0fU) | 0x40U);
    bytes[8] = static_cast<unsigned char>((bytes[8] & 0x3fU) | 0x80U);
    constexpr char hex[] = "0123456789abcdef";
    std::string encoded(32, '0');
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        encoded[index * 2] = hex[bytes[index] >> 4];
        encoded[index * 2 + 1] = hex[bytes[index] & 0x0fU];
    }
    return encoded.substr(0, 8) + '-' + encoded.substr(8, 4) + '-' + encoded.substr(12, 4) + '-' +
           encoded.substr(16, 4) + '-' + encoded.substr(20, 12);
}

} // namespace homeworldz::viewer
