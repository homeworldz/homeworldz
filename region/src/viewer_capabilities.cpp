#include "homeworldz/viewer_capabilities.h"

#include "homeworldz/viewer_protocol.h"

#include <array>
#include <charconv>
#include <random>

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
} // namespace

std::string seed_capability_xml(std::string_view public_endpoint, std::string_view grid_public_endpoint,
                                std::string_view session_id) {
    auto base = std::string(public_endpoint);
    while (!base.empty() && base.back() == '/') base.pop_back();
    const auto event_url = xml_escape(base + "/caps/event/" + std::string(session_id));
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

std::string event_queue_xml(std::uint64_t id, const std::optional<EstablishAgentCommunication>& event) {
    std::string events = "<array/>";
    if (event) {
        events = "<array><map><key>message</key><string>EstablishAgentCommunication</string>"
                 "<key>body</key><map><key>agent-id</key><uuid>" + xml_escape(event->agent_id) +
                 "</uuid><key>sim-ip-and-port</key><string>" + xml_escape(event->simulator_endpoint) +
                 "</string><key>seed-capability</key><uri>" + xml_escape(event->seed_capability) +
                 "</uri></map></map></array>";
    }
    return "<?xml version=\"1.0\"?><llsd><map><key>events</key>" + events +
           "<key>id</key><integer>" + std::to_string(id) + "</integer></map></llsd>";
}

std::string simulator_features_xml(std::string_view currency) {
    return "<?xml version=\"1.0\"?><llsd><map><key>OpenSimExtras</key><map>"
           "<key>currency</key><string>" + xml_escape(currency) +
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
    if (!folder || !parse_uuid(*folder) || asset_type != "texture" ||
        inventory_type != "texture" || !name || name->empty() || name->size() > 255 ||
        !description || description->size() > 1024) return std::nullopt;
    NewFileInventoryUpload result{*folder, *name, *description};
    if (const auto permissions = llsd_u32(xml, "everyone_mask"))
        result.everyone_permissions = *permissions;
    if (const auto permissions = llsd_u32(xml, "group_mask"))
        result.group_permissions = *permissions;
    if (const auto permissions = llsd_u32(xml, "next_owner_mask"))
        result.next_permissions = *permissions;
    return result;
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
