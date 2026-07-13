#include "homeworldz/viewer_capabilities.h"

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

std::string seed_capability_xml(std::string_view public_endpoint, std::string_view session_id) {
    auto base = std::string(public_endpoint);
    while (!base.empty() && base.back() == '/') base.pop_back();
    const auto event_url = xml_escape(base + "/caps/event/" + std::string(session_id));
    const auto texture_url = xml_escape(base + "/caps/texture/" + std::string(session_id));
    const auto environment_url = xml_escape(base + "/caps/environment/" + std::string(session_id));
    return "<?xml version=\"1.0\"?><llsd><map><key>EventQueueGet</key><uri>" + event_url +
           "</uri><key>GetTexture</key><uri>" + texture_url +
           "</uri><key>EnvironmentSettings</key><uri>" + environment_url + "</uri></map></llsd>";
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
           "</uuid></map></array></llsd>";
}

} // namespace homeworldz::viewer
