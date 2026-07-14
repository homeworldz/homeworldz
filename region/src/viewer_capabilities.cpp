#include "homeworldz/viewer_capabilities.h"

#include "homeworldz/sha256.h"

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
} // namespace

std::string seed_capability_xml(std::string_view public_endpoint, std::string_view grid_public_endpoint,
                                std::string_view session_id) {
    auto base = std::string(public_endpoint);
    while (!base.empty() && base.back() == '/') base.pop_back();
    const auto event_url = xml_escape(base + "/caps/event/" + std::string(session_id));
    const auto texture_url = xml_escape(base + "/caps/texture/" + std::string(session_id));
    const auto asset_url = xml_escape(base + "/caps/assets/" + std::string(session_id));
    const auto environment_url = xml_escape(base + "/caps/environment/" + std::string(session_id));
    const auto baked_upload_url = xml_escape(base + "/caps/upload-baked/" + std::string(session_id));
    auto grid_base = std::string(grid_public_endpoint);
    while (!grid_base.empty() && grid_base.back() == '/') grid_base.pop_back();
    const auto inventory_url = xml_escape(grid_base + "/caps/inventory/descendents/" + std::string(session_id));
    const auto inventory_items_url = xml_escape(grid_base + "/caps/inventory/items/" + std::string(session_id));
    const auto create_inventory_folder_url =
        xml_escape(grid_base + "/caps/inventory/create-folder/" + std::string(session_id));
    return "<?xml version=\"1.0\"?><llsd><map><key>EventQueueGet</key><uri>" + event_url +
           "</uri><key>GetTexture</key><uri>" + texture_url +
           "</uri><key>ViewerAsset</key><uri>" + asset_url +
           "</uri><key>EnvironmentSettings</key><uri>" + environment_url +
           "</uri><key>UploadBakedTexture</key><uri>" + baked_upload_url +
           "</uri><key>FetchInventoryDescendents2</key><uri>" + inventory_url +
           "</uri><key>FetchInventory2</key><uri>" + inventory_items_url +
           "</uri><key>CreateInventoryCategory</key><uri>" + create_inventory_folder_url +
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

std::string baked_texture_asset_id(std::span<const std::byte> content) {
    auto digest = crypto::sha256_hex(content);
    digest[12] = '4';
    digest[16] = '8';
    return digest.substr(0, 8) + '-' + digest.substr(8, 4) + '-' + digest.substr(12, 4) + '-' +
           digest.substr(16, 4) + '-' + digest.substr(20, 12);
}

} // namespace homeworldz::viewer
