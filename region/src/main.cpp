#include <array>
#include <algorithm>
#include <atomic>
#include <bit>
#include <charconv>
#include <cctype>
#include <csignal>
#include <chrono>
#include <thread>
#include <cmath>
#include <deque>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <iomanip>
#include <sstream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "homeworldz/api_models.h"
#include "homeworldz/appearance_bake.h"
#include "homeworldz/avatar_controller.h"
#include "homeworldz/falcon_runtime.h"
#include "homeworldz/grid_client.h"
#include "homeworldz/http_response.h"
#include "homeworldz/inventory_asset.h"
#include "homeworldz/object_asset.h"
#include "homeworldz/physics_adapters.h"
#include "homeworldz/physics_scene.h"
#include "homeworldz/region_config.h"
#include "homeworldz/region_storage.h"
#include "homeworldz/region_transit.h"
#include "homeworldz/scene.h"
#include "homeworldz/sha256.h"
#include "homeworldz/simulation_loop.h"
#include "homeworldz/terrain_edit.h"
#include "homeworldz/viewer_capabilities.h"
#include "homeworldz/viewer_protocol.h"
#include "homeworldz/visual_params.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
using socket_handle = SOCKET;
using socket_length = int;
constexpr socket_handle invalid_socket = INVALID_SOCKET;
static void close_socket(socket_handle socket) { closesocket(socket); }
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using socket_handle = int;
using socket_length = socklen_t;
constexpr socket_handle invalid_socket = -1;
static void close_socket(socket_handle socket) { close(socket); }
#endif

namespace {
std::atomic_bool running{true};
constexpr std::string_view system_creator_id = "00000000-0000-0000-0000-000000000002";
constexpr std::string_view default_map_tile_asset_id = "00000000-0000-1111-9999-000000000100";
homeworldz::config::RegionSettings configured_values;

struct LiveAvatar {
    LiveAvatar(homeworldz::viewer::AvatarController initial_controller,
               homeworldz::scene::EntityId initial_entity_id, std::string initial_user_id,
               std::chrono::steady_clock::time_point initial_next_ping,
               std::chrono::steady_clock::time_point initial_next_presence,
               std::chrono::steady_clock::time_point initial_next_transform,
               homeworldz::scene::Vector3 initial_sent_position)
        : controller(std::move(initial_controller)), entity_id(initial_entity_id),
          user_id(std::move(initial_user_id)), next_ping(initial_next_ping),
          next_presence(initial_next_presence), next_transform(initial_next_transform),
          last_sent_position(initial_sent_position) {}

    homeworldz::viewer::AvatarController controller;
    homeworldz::scene::EntityId entity_id{};
    std::string user_id;
    std::chrono::steady_clock::time_point next_ping{};
    std::chrono::steady_clock::time_point next_presence{};
    std::chrono::steady_clock::time_point next_transform{};
    std::chrono::steady_clock::time_point last_pong{};  // last CompletePingCheck reply from this viewer
    homeworldz::scene::Vector3 last_sent_position{};
    homeworldz::scene::Vector3 last_sent_velocity{};
    std::array<float, 3> last_sent_rotation{};
    std::uint8_t ping_id{};
    std::chrono::steady_clock::time_point last_agent_update{};
    std::uint32_t last_agent_update_sequence{};
    bool has_agent_update{};
    homeworldz::physics::CharacterId physics_character{};
    std::chrono::steady_clock::time_point restored_flying_until{};
    std::string outbound_transit_id;
    std::chrono::steady_clock::time_point outbound_transit_expires{};
    std::chrono::steady_clock::time_point next_crossing_attempt{};
};

bool sequence_is_newer(std::uint32_t candidate, std::uint32_t current) {
    return static_cast<std::int32_t>(candidate - current) > 0;
}

struct QueuedTexturePacket {
    std::string asset_id;
    std::vector<std::byte> payload;
    bool last{};
};

struct PendingInventoryUpload {
    std::string session_id;
    std::string agent_id;
    std::string item_id;
    std::string asset_id;
    homeworldz::viewer::NewFileInventoryUpload request;
};

struct PendingInventoryAssetUpdate {
    std::string session_id;
    std::string agent_id;
    std::string item_id;
    std::string asset_id;
    std::int8_t asset_type{};
    std::int8_t inventory_type{};
    std::string task_id;
    bool script_running{};
};

struct PendingInventoryAssetUpload {
    std::string asset_id;
    std::int8_t asset_type{};
};

struct PendingInventoryAssetXfer {
    std::string transaction_id;
    std::string asset_id;
    homeworldz::viewer::Uuid asset_uuid{};
    std::int8_t asset_type{};
    std::size_t expected_size{};
    std::size_t packet_size{1000};
    std::vector<std::byte> data;
    std::unordered_set<std::uint32_t> received_packets;
};

struct PendingTaskInventoryXfer {
    std::vector<std::byte> data;
    std::size_t offset{};
    std::uint32_t next_packet{1};
    std::uint32_t awaiting_confirmation{};
};

struct SentDynamicTransform {
    homeworldz::physics::BodyState state;
    std::chrono::steady_clock::time_point sent_at;
};

std::string task_inventory_type_name(std::int8_t type) {
    switch (type) {
    case 0: return "texture";
    case 1: return "sound";
    case 2: return "callcard";
    case 3: return "landmark";
    case 5: return "clothing";
    case 6: return "object";
    case 7: return "notecard";
    case 10: return "lsltext";
    case 13: return "bodypart";
    case 20: return "animation";
    case 21: return "gesture";
    default: return "unknown";
    }
}

std::string task_inventory_inv_type_name(std::int8_t type) {
    switch (type) {
    case 0: return "texture";
    case 1: return "sound";
    case 2: return "callcard";
    case 3: return "landmark";
    case 6: return "object";
    case 7: return "notecard";
    case 10: return "lsl";
    case 15: return "snapshot";
    case 17: return "attachment";
    case 18: return "wearable";
    case 19: return "animation";
    case 20: return "gesture";
    default: return "unknown";
    }
}

std::string permission_hex(std::uint32_t value) {
    std::ostringstream output;
    output << std::hex << std::setfill('0') << std::setw(8) << value;
    return output.str();
}

std::string task_inventory_field(std::string_view value) {
    std::string result(value);
    for (auto& character : result)
        if (character == '|' || static_cast<unsigned char>(character) < 0x20)
            character = ' ';
    return result;
}

std::vector<std::byte> task_inventory_file(const homeworldz::scene::Entity& entity) {
    std::string text = "\tinv_object\t0\n\t{\n\t\tobj_id\t" + entity.object_id +
        "\n\t\tparent_id\t00000000-0000-0000-0000-000000000000\n"
        "\t\ttype\tcategory\n\t\tname\tContents|\n";
    for (const auto& item : entity.task_inventory) {
        text += "\t}\n\tinv_item\t0\n\t{\n\t\titem_id\t" + item.item_id +
            "\n\t\tparent_id\t" + entity.object_id +
            "\n\tpermissions 0\n\t{\n\t\tbase_mask\t" + permission_hex(item.base_permissions) +
            "\n\t\towner_mask\t" + permission_hex(item.current_permissions) +
            "\n\t\tgroup_mask\t" + permission_hex(item.group_permissions) +
            "\n\t\teveryone_mask\t" + permission_hex(item.everyone_permissions) +
            "\n\t\tnext_owner_mask\t" + permission_hex(item.next_permissions) +
            "\n\t\tcreator_id\t" + item.creator_id +
            "\n\t\towner_id\t" + item.owner_id +
            "\n\t\tlast_owner_id\t" + item.last_owner_id +
            "\n\t\tgroup_id\t" + item.group_id +
            "\n\t}\n\t\tasset_id\t" + item.asset_id +
            "\n\t\ttype\t" + task_inventory_type_name(item.asset_type) +
            "\n\t\tinv_type\t" + task_inventory_inv_type_name(item.inventory_type) +
            "\n\t\tflags\t" + permission_hex(item.flags) +
            "\n\tsale_info\t0\n\t{\n\t\tsale_type\tnot\n\t\tsale_price\t0\n\t}\n"
            "\t\tname\t" + task_inventory_field(item.name) +
            "|\n\t\tdesc\t" + task_inventory_field(item.description) +
            "|\n\t\tcreation_date\t" + std::to_string(item.creation_date) + "\n";
    }
    text += "\t}";
    text.push_back('\0');
    return std::vector<std::byte>(reinterpret_cast<const std::byte*>(text.data()),
                                  reinterpret_cast<const std::byte*>(text.data() + text.size()));
}

struct PendingEventResponse {
    socket_handle client{invalid_socket};
    std::string request;
    std::string session_id;
    std::chrono::steady_clock::time_point deadline{};
};

struct PendingAgentMovementComplete {
    std::string endpoint;
    std::string session_id;
    std::string visit_id;
    std::vector<std::byte> payload;
    std::chrono::steady_clock::time_point deadline{};
};

void stop(int) { running = false; }

std::string configured_value(std::string_view name, std::string fallback = {}) {
    const auto configured = configured_values.find(std::string(name));
    if (configured != configured_values.end()) return configured->second;
    return fallback;
}

std::unique_ptr<homeworldz::terrain::Heightmap> load_raw_heightmap(
    const std::filesystem::path& path, std::size_t width) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) return {};
    const auto byte_count = static_cast<std::size_t>(input.tellg());
    const auto source_width = byte_count == width * width ? width :
        (byte_count == 256 * 256 ? std::size_t{256} : std::size_t{});
    if (source_width == 0) return {};
    input.seekg(0);
    std::vector<unsigned char> source(byte_count);
    input.read(reinterpret_cast<char*>(source.data()), source.size());
    if (!input) return {};
    auto result = std::make_unique<homeworldz::terrain::Heightmap>(width);
    for (std::size_t y = 0; y < width; ++y) {
        const auto source_y = y * (source_width - 1) / (width - 1);
        for (std::size_t x = 0; x < width; ++x) {
            const auto source_x = x * (source_width - 1) / (width - 1);
            (*result)[y * width + x] = static_cast<float>(source[source_y * source_width + source_x]);
        }
    }
    return result;
}

std::string encode_heightmap(const homeworldz::terrain::Heightmap& heightmap) {
    std::string output;
    output.reserve(heightmap.size() * sizeof(float));
    for (const auto height : heightmap) {
        const auto bits = std::bit_cast<std::uint32_t>(height);
        output.push_back(static_cast<char>(bits));
        output.push_back(static_cast<char>(bits >> 8));
        output.push_back(static_cast<char>(bits >> 16));
        output.push_back(static_cast<char>(bits >> 24));
    }
    return output;
}

homeworldz::scene::Vector3 default_spawn(const homeworldz::terrain::Heightmap& heightmap) {
    const auto width = heightmap.width();
    const auto center = width / 2;
    std::size_t selected_x = center;
    std::size_t selected_y = center;
    std::size_t selected_distance = (std::numeric_limits<std::size_t>::max)();
    for (std::size_t y = 16; y < width - 16; ++y) {
        for (std::size_t x = 16; x < width - 16; ++x) {
            const auto height = heightmap[y * width + x];
            if (height < 24.0F) continue;
            const auto dx = static_cast<std::int64_t>(x) - static_cast<std::int64_t>(center);
            const auto dy = static_cast<std::int64_t>(y) - static_cast<std::int64_t>(center);
            const auto distance = static_cast<std::size_t>(dx * dx + dy * dy);
            if (distance < selected_distance) {
                selected_x = x;
                selected_y = y;
                selected_distance = distance;
            }
        }
    }
    const auto height = heightmap[selected_y * width + selected_x];
    return {static_cast<double>(selected_x), static_cast<double>(selected_y), height + 1.0};
}

double ground_height(const homeworldz::terrain::Heightmap& heightmap,
                      const homeworldz::scene::Vector3& position) {
    const auto maximum = static_cast<int>(heightmap.width() - 1);
    const auto x = std::clamp(static_cast<int>(position.x), 0, maximum);
    const auto y = std::clamp(static_cast<int>(position.y), 0, maximum);
    return heightmap[static_cast<std::size_t>(y) * heightmap.width() + x];
}

int configured_int(std::string_view name, int fallback, int minimum, int maximum) {
    const auto value = configured_value(name);
    if (value.empty()) return fallback;
    int parsed{};
    const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
    return result.ec == std::errc{} && result.ptr == value.data() + value.size() &&
        parsed >= minimum && parsed <= maximum ? parsed : fallback;
}

int configured_port() {
    return configured_int("region.http_port", 42001, 1, 65535);
}

int configured_viewer_port() {
    return configured_int("region.viewer_port", 42002, 1, 65535);
}

bool configured_bind_address(sockaddr_in& address, std::string_view setting_name, int port) {
    address = {};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<unsigned short>(port));
    const auto host = configured_value(setting_name, "127.0.0.1");
    if (inet_pton(AF_INET, host.c_str(), &address.sin_addr) == 1) return true;
    std::cerr << "{\"level\":\"error\",\"message\":\"invalid IPv4 bind address\",\"setting\":"
              << homeworldz::api::json_string(setting_name) << ",\"address\":"
              << homeworldz::api::json_string(host) << "}" << std::endl;
    return false;
}

std::string udp_endpoint(const sockaddr_in& address) {
    std::array<char, INET_ADDRSTRLEN> ip{};
    if (!inet_ntop(AF_INET, &address.sin_addr, ip.data(), ip.size())) return {};
    return std::string(ip.data()) + ':' + std::to_string(ntohs(address.sin_port));
}

bool send_udp(socket_handle socket, std::string_view endpoint, std::span<const std::byte> bytes) {
    const auto colon = endpoint.rfind(':');
    if (colon == std::string_view::npos) return false;
    unsigned port{};
    const auto port_text = endpoint.substr(colon + 1);
    const auto parsed = std::from_chars(port_text.data(), port_text.data() + port_text.size(), port);
    if (parsed.ec != std::errc{} || parsed.ptr != port_text.data() + port_text.size() || port > 65535) return false;
    sockaddr_in destination{};
    destination.sin_family = AF_INET;
    destination.sin_port = htons(static_cast<unsigned short>(port));
    const std::string ip(endpoint.substr(0, colon));
    if (inet_pton(AF_INET, ip.c_str(), &destination.sin_addr) != 1) return false;
    return sendto(socket, reinterpret_cast<const char*>(bytes.data()), static_cast<int>(bytes.size()), 0,
                  reinterpret_cast<const sockaddr*>(&destination), sizeof(destination)) == static_cast<int>(bytes.size());
}

std::optional<std::string> receive_http_request(socket_handle client) {
    constexpr std::size_t maximum_header_size = 64 * 1024;
    constexpr std::size_t maximum_body_size = 1024 * 1024;
    std::string request;
    std::array<char, 4096> buffer{};
    std::optional<std::size_t> expected_size;
    while (request.size() <= maximum_header_size + maximum_body_size) {
        const auto received = recv(client, buffer.data(), static_cast<int>(buffer.size()), 0);
        if (received <= 0) return std::nullopt;
        request.append(buffer.data(), static_cast<std::size_t>(received));
        if (!expected_size) {
            const auto header_end = request.find("\r\n\r\n");
            if (header_end == std::string::npos) {
                if (request.size() > maximum_header_size) return std::nullopt;
                continue;
            }
            const auto content_length = homeworldz::http::request_content_length(
                std::string_view(request).substr(0, header_end + 4));
            if (!content_length || *content_length > maximum_body_size) return std::nullopt;
            expected_size = header_end + 4 + *content_length;
        }
        if (request.size() >= *expected_size) {
            request.resize(*expected_size);
            return request;
        }
    }
    return std::nullopt;
}

bool send_all(socket_handle client, std::string_view content) {
    std::size_t sent = 0;
    while (sent < content.size()) {
        const auto count = send(client, content.data() + sent, static_cast<int>(content.size() - sent), 0);
        if (count <= 0) return false;
        sent += static_cast<std::size_t>(count);
    }
    return true;
}

void finish_http_response(socket_handle client) {
#ifdef _WIN32
    shutdown(client, SD_SEND);
#else
    shutdown(client, SHUT_WR);
#endif
}

std::string capability_session(std::string_view path, std::string_view prefix) {
    if (!path.starts_with(prefix)) return {};
    auto session = path.substr(prefix.size());
    if (const auto separator = session.find('/'); separator != std::string_view::npos) {
        const auto visit = session.substr(separator + 1);
        if (visit.empty() || visit.find('/') != std::string_view::npos ||
            !homeworldz::viewer::parse_uuid(visit)) return {};
        session = session.substr(0, separator);
    }
    if (session.empty()) return {};
    return std::string(session);
}

std::string capability_visit(std::string_view path, std::string_view prefix) {
    if (!path.starts_with(prefix)) return {};
    const auto remainder = path.substr(prefix.size());
    const auto separator = remainder.find('/');
    if (separator == std::string_view::npos) return {};
    const auto visit = remainder.substr(separator + 1);
    if (visit.empty() || visit.find('/') != std::string_view::npos ||
        !homeworldz::viewer::parse_uuid(visit)) return {};
    return std::string(visit);
}

std::optional<std::pair<std::string, std::string>> texture_request(std::string_view path) {
    constexpr std::string_view prefix = "/caps/texture/";
    constexpr std::string_view key = "texture_id=";
    if (!path.starts_with(prefix)) return std::nullopt;
    // Accept both "<session>/?texture_id=<uuid>" and "<session>?texture_id=<uuid>";
    // viewers differ on whether they append the trailing slash before the query.
    const auto question = path.find('?', prefix.size());
    if (question == std::string_view::npos) return std::nullopt;
    auto session = path.substr(prefix.size(), question - prefix.size());
    if (!session.empty() && session.back() == '/') session.remove_suffix(1);
    const auto query = path.substr(question + 1);
    if (!query.starts_with(key)) return std::nullopt;
    const auto texture = query.substr(key.size());
    if (session.empty() || texture.empty() || texture.find('&') != std::string_view::npos)
        return std::nullopt;
    return std::pair{std::string(session), std::string(texture)};
}

std::optional<std::pair<std::string, std::string>> viewer_asset_request(std::string_view path) {
    constexpr std::string_view prefix = "/caps/assets/";
    if (!path.starts_with(prefix)) return std::nullopt;
    // Accept both "<session>/?<type>_id=<uuid>" and "<session>?<type>_id=<uuid>".
    // The type prefix varies (texture_id, bodypart_id, clothing_id, ...); the
    // "_id=" marker is common to all and identifies the requested asset UUID.
    const auto question = path.find('?', prefix.size());
    if (question == std::string_view::npos) return std::nullopt;
    auto session = path.substr(prefix.size(), question - prefix.size());
    if (!session.empty() && session.back() == '/') session.remove_suffix(1);
    const auto query = path.substr(question + 1);
    const auto id_marker = query.find("_id=");
    if (session.empty() || id_marker == std::string_view::npos || id_marker == 0 ||
        query.find('&') != std::string_view::npos) return std::nullopt;
    const auto asset = query.substr(id_marker + 4);
    if (asset.empty()) return std::nullopt;
    return std::pair{std::string(session), std::string(asset)};
}

struct InternalAssetRequest {
    std::string asset_id;
    bool replicate{};
};

std::optional<InternalAssetRequest> internal_asset_request(std::string_view path) {
    constexpr std::string_view prefix = "/api/v1/assets/";
    if (!path.starts_with(prefix)) return std::nullopt;
    auto asset_id = path.substr(prefix.size());
    constexpr std::string_view replicate_suffix = "/replicate";
    bool replicate = false;
    if (asset_id.ends_with(replicate_suffix)) {
        asset_id.remove_suffix(replicate_suffix.size());
        replicate = true;
    }
    if (asset_id.empty() || asset_id.find('/') != std::string_view::npos ||
        !homeworldz::viewer::parse_uuid(asset_id)) return std::nullopt;
    return InternalAssetRequest{std::string(asset_id), replicate};
}

std::optional<std::pair<std::string, std::string>> baked_upload_data_request(std::string_view path) {
    constexpr std::string_view prefix = "/caps/upload-baked-data/";
    if (!path.starts_with(prefix)) return std::nullopt;
    const auto separator = path.find('/', prefix.size());
    if (separator == std::string_view::npos) return std::nullopt;
    const auto session = path.substr(prefix.size(), separator - prefix.size());
    const auto token = path.substr(separator + 1);
    if (session.empty() || token.empty() || token.find('/') != std::string_view::npos) return std::nullopt;
    return std::pair{std::string(session), std::string(token)};
}

std::optional<std::pair<std::string, std::string>> file_upload_data_request(std::string_view path) {
    constexpr std::string_view prefix = "/caps/upload-file-data/";
    if (!path.starts_with(prefix)) return std::nullopt;
    const auto separator = path.find('/', prefix.size());
    if (separator == std::string_view::npos) return std::nullopt;
    const auto session = path.substr(prefix.size(), separator - prefix.size());
    const auto token = path.substr(separator + 1);
    if (session.empty() || token.empty() || token.find('/') != std::string_view::npos) return std::nullopt;
    return std::pair{std::string(session), std::string(token)};
}

std::optional<std::pair<std::string, std::string>> inventory_asset_update_data_request(
    std::string_view path) {
    constexpr std::string_view prefix = "/caps/update-inventory-asset-data/";
    if (!path.starts_with(prefix)) return std::nullopt;
    const auto separator = path.find('/', prefix.size());
    if (separator == std::string_view::npos) return std::nullopt;
    const auto session = path.substr(prefix.size(), separator - prefix.size());
    const auto token = path.substr(separator + 1);
    if (session.empty() || token.empty() || token.find('/') != std::string_view::npos)
        return std::nullopt;
    return std::pair{std::string(session), std::string(token)};
}

std::string_view http_request_body(std::string_view request) {
    const auto separator = request.find("\r\n\r\n");
    return separator == std::string_view::npos ? std::string_view{} : request.substr(separator + 4);
}

std::string simulator_endpoint(std::string_view public_endpoint, int viewer_port) {
    auto authority = public_endpoint;
    const auto scheme = authority.find("://");
    if (scheme != std::string_view::npos) authority.remove_prefix(scheme + 3);
    const auto slash = authority.find('/');
    if (slash != std::string_view::npos) authority = authority.substr(0, slash);
    const auto colon = authority.rfind(':');
    if (colon != std::string_view::npos) authority = authority.substr(0, colon);
    return std::string(authority) + ':' + std::to_string(viewer_port);
}

std::optional<homeworldz::viewer::SimulatorEventEndpoint> simulator_event_endpoint(
    std::string_view public_endpoint, int viewer_port) {
    auto authority = public_endpoint;
    const auto scheme = authority.find("://");
    if (scheme != std::string_view::npos) authority.remove_prefix(scheme + 3);
    const auto slash = authority.find('/');
    if (slash != std::string_view::npos) authority = authority.substr(0, slash);
    const auto colon = authority.rfind(':');
    if (colon != std::string_view::npos) authority = authority.substr(0, colon);
    const auto host = std::string(authority);
    if (host.empty() || viewer_port < 1 || viewer_port > 65535) return std::nullopt;
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    addrinfo* addresses{};
    if (getaddrinfo(host.c_str(), nullptr, &hints, &addresses) != 0) return std::nullopt;
    std::optional<homeworldz::viewer::SimulatorEventEndpoint> result;
    for (auto* address = addresses; address; address = address->ai_next) {
        if (address->ai_family != AF_INET || address->ai_addrlen < sizeof(sockaddr_in)) continue;
        const auto* ipv4 = reinterpret_cast<const sockaddr_in*>(address->ai_addr);
        const auto bytes = reinterpret_cast<const std::uint8_t*>(&ipv4->sin_addr.s_addr);
        result = homeworldz::viewer::SimulatorEventEndpoint{
            {bytes[0], bytes[1], bytes[2], bytes[3]}, static_cast<std::uint16_t>(viewer_port)};
        break;
    }
    freeaddrinfo(addresses);
    return result;
}

void apply_material_contact_defaults(homeworldz::scene::Entity& entity) {
    const auto material = homeworldz::physics::material_properties(entity.material);
    entity.physics_friction = material.friction;
    entity.physics_restitution = material.restitution;
}

const std::vector<std::byte>& default_prim_texture_entry() {
    static const auto entry = [] {
        const auto plywood = homeworldz::viewer::parse_uuid("89556747-24cb-43ed-920b-47caed15465f");
        if (!plywood) throw std::logic_error("default plywood texture UUID is invalid");
        return homeworldz::viewer::default_texture_entry(*plywood);
    }();
    return entry;
}

void apply_extra_physics(
    homeworldz::scene::Entity& entity, const homeworldz::viewer::ObjectFlagUpdate& update) {
    entity.physics_shape_type = std::min<std::uint8_t>(update.physics_shape_type, 0x02);
    entity.physics_density = std::clamp(static_cast<double>(update.density), 1.0, 22587.0);
    entity.physics_friction = std::clamp(static_cast<double>(update.friction), 0.0, 255.0);
    entity.physics_restitution = std::clamp(static_cast<double>(update.restitution), 0.0, 1.0);
    entity.physics_gravity_multiplier =
        std::clamp(static_cast<double>(update.gravity_multiplier), -1.0, 28.0);
}

std::optional<homeworldz::viewer::StaticObject> static_object_from_entity(
    const homeworldz::scene::Scene& scene, const homeworldz::scene::Entity& entity,
    std::string_view recipient_id, const homeworldz::script::FalconRuntime& falcon) {
    constexpr std::uint32_t object_scripted = 0x00000040;
    constexpr std::uint32_t object_handle_touch = 0x00000080;
    constexpr std::uint32_t object_modify = 0x00000004;
    constexpr std::uint32_t object_copy = 0x00000008;
    constexpr std::uint32_t object_any_owner = 0x00000010;
    constexpr std::uint32_t object_you_owner = 0x00000020;
    constexpr std::uint32_t object_move = 0x00000100;
    constexpr std::uint32_t object_transfer = 0x00020000;
    constexpr std::uint32_t object_owner_modify = 0x10000000;
    constexpr std::uint32_t object_physics = 0x00000001;
    constexpr std::uint32_t object_phantom = 0x00000400;
    constexpr std::uint32_t object_temporary = 0x40000000;
    if (entity.object_id.empty() || entity.id > (std::numeric_limits<std::uint32_t>::max)())
        return std::nullopt;
    const auto object_id = homeworldz::viewer::parse_uuid(entity.object_id);
    const auto owner_id = homeworldz::viewer::parse_uuid(entity.owner_id);
    if (!object_id || !owner_id) return std::nullopt;
    homeworldz::viewer::StaticObject object;
    object.local_id = static_cast<std::uint32_t>(entity.id);
    object.parent_local_id = static_cast<std::uint32_t>(entity.parent_id);
    object.id = *object_id;
    object.owner_id = *owner_id;
    const bool is_owner = entity.owner_id == recipient_id;
    const auto folded = homeworldz::scene::effective_permissions(scene, entity);
    const auto permissions = is_owner ? folded.owner : entity.everyone_permissions & folded.owner;
    object.update_flags = object_any_owner;
    if (entity.physical) object.update_flags |= object_physics;
    if (entity.phantom) object.update_flags |= object_phantom;
    if (entity.temporary) object.update_flags |= object_temporary;
    if ((permissions & homeworldz::scene::permission_modify) != 0) object.update_flags |= object_modify;
    if ((permissions & homeworldz::scene::permission_copy) != 0) object.update_flags |= object_copy;
    if ((permissions & homeworldz::scene::permission_move) != 0) object.update_flags |= object_move;
    if ((permissions & homeworldz::scene::permission_transfer) != 0) object.update_flags |= object_transfer;
    if (is_owner) object.update_flags |= object_you_owner | object_owner_modify;
    const auto script_status = falcon.object_script_status(entity.object_id);
    if (script_status.scripted) object.update_flags |= object_scripted;
    if (script_status.handles_touch) object.update_flags |= object_handle_touch;
    object.material = entity.material;
    const auto& protocol_position = entity.parent_id == 0 ? entity.position : entity.local_position;
    const auto& protocol_rotation = entity.parent_id == 0 ? entity.rotation : entity.local_rotation;
    object.position = {static_cast<float>(protocol_position.x), static_cast<float>(protocol_position.y),
                       static_cast<float>(protocol_position.z)};
    object.velocity = {static_cast<float>(entity.velocity.x), static_cast<float>(entity.velocity.y),
                       static_cast<float>(entity.velocity.z)};
    object.rotation = {static_cast<float>(protocol_rotation.x), static_cast<float>(protocol_rotation.y),
                       static_cast<float>(protocol_rotation.z)};
    object.scale = {static_cast<float>(entity.scale.x), static_cast<float>(entity.scale.y),
                    static_cast<float>(entity.scale.z)};
    object.texture_entry = entity.texture_entry;
    object.path_curve = entity.path_curve;
    object.profile_curve = entity.profile_curve;
    object.path_begin = entity.path_begin;
    object.path_end = entity.path_end;
    object.path_scale_x = entity.path_scale_x;
    object.path_scale_y = entity.path_scale_y;
    object.path_shear_x = entity.path_shear_x;
    object.path_shear_y = entity.path_shear_y;
    object.path_twist = entity.path_twist;
    object.path_twist_begin = entity.path_twist_begin;
    object.path_radius_offset = entity.path_radius_offset;
    object.path_taper_x = entity.path_taper_x;
    object.path_taper_y = entity.path_taper_y;
    object.path_revolutions = entity.path_revolutions;
    object.path_skew = entity.path_skew;
    object.profile_begin = entity.profile_begin;
    object.profile_end = entity.profile_end;
    object.profile_hollow = entity.profile_hollow;
    return object;
}

void regenerate_task_inventory_item_ids(homeworldz::scene::Entity& entity) {
    for (auto& item : entity.task_inventory)
        item.item_id = homeworldz::viewer::random_uuid();
}

void apply_object_asset(
    homeworldz::scene::Entity& entity, const homeworldz::asset::ObjectAsset& asset) {
    entity.scale = asset.scale;
    entity.rotation = asset.rotation;
    entity.material = asset.material;
    entity.physics_shape_type = asset.physics_shape_type;
    entity.physics_density = asset.physics_density;
    entity.physics_friction = asset.physics_friction;
    entity.physics_restitution = asset.physics_restitution;
    entity.physics_gravity_multiplier = asset.physics_gravity_multiplier;
    entity.texture_entry = asset.texture_entry;
    entity.path_curve = asset.path_curve;
    entity.profile_curve = asset.profile_curve;
    entity.path_begin = asset.path_begin;
    entity.path_end = asset.path_end;
    entity.path_scale_x = asset.path_scale_x;
    entity.path_scale_y = asset.path_scale_y;
    entity.path_shear_x = asset.path_shear_x;
    entity.path_shear_y = asset.path_shear_y;
    entity.path_twist = asset.path_twist;
    entity.path_twist_begin = asset.path_twist_begin;
    entity.path_radius_offset = asset.path_radius_offset;
    entity.path_taper_x = asset.path_taper_x;
    entity.path_taper_y = asset.path_taper_y;
    entity.path_revolutions = asset.path_revolutions;
    entity.path_skew = asset.path_skew;
    entity.profile_begin = asset.profile_begin;
    entity.profile_end = asset.profile_end;
    entity.profile_hollow = asset.profile_hollow;
    entity.physical = asset.physical;
    entity.phantom = asset.phantom;
    entity.task_inventory_serial = asset.task_inventory_serial;
    entity.task_inventory = asset.task_inventory;
    regenerate_task_inventory_item_ids(entity);
}

std::optional<homeworldz::viewer::ObjectProperties> object_properties_from_entity(
    const homeworldz::scene::Scene& scene, const homeworldz::scene::Entity& entity) {
    const auto object_id = homeworldz::viewer::parse_uuid(entity.object_id);
    const auto creator_id = homeworldz::viewer::parse_uuid(entity.creator_id);
    const auto owner_id = homeworldz::viewer::parse_uuid(entity.owner_id);
    if (!object_id || !creator_id || !owner_id) return std::nullopt;
    homeworldz::viewer::ObjectProperties properties;
    properties.object_id = *object_id;
    properties.creator_id = *creator_id;
    properties.owner_id = *owner_id;
    properties.base_permissions = entity.base_permissions;
    properties.owner_permissions = entity.owner_permissions;
    properties.group_permissions = entity.group_permissions;
    properties.everyone_permissions = entity.everyone_permissions;
    properties.next_owner_permissions = entity.next_owner_permissions;
    const auto folded = homeworldz::scene::effective_permissions(scene, entity);
    properties.folded_owner_permissions = folded.owner;
    properties.folded_next_owner_permissions = folded.next_owner;
    properties.creation_date = entity.creation_date;
    properties.name = entity.name;
    properties.description = entity.description;
    return properties;
}

std::pair<std::string, std::string> legacy_avatar_name(std::string_view username) {
    const auto separator = username.find('.');
    auto first = std::string(username.substr(0, separator));
    auto last = separator == std::string_view::npos
        ? std::string("Resident") : std::string(username.substr(separator + 1));
    const auto capitalize = [](std::string& value) {
        if (!value.empty())
            value.front() = static_cast<char>(std::toupper(static_cast<unsigned char>(value.front())));
    };
    capitalize(first);
    capitalize(last);
    return {std::move(first), std::move(last)};
}

std::string runtime_version() {
    std::ifstream input("VERSION");
    std::string value;
    if (input && std::getline(input, value)) {
        const auto first = value.find_first_not_of(" \t\r\n");
        const auto last = value.find_last_not_of(" \t\r\n");
        if (first != std::string::npos) value = value.substr(first, last - first + 1);
        const bool valid = !value.empty() && value.size() <= 64 &&
            std::all_of(value.begin(), value.end(), [](unsigned char character) {
                return std::isalnum(character) || character == '.' || character == '-' ||
                       character == '_';
            });
        if (valid) return value;
    }
    return HOMEWORLDZ_VERSION;
}
} // namespace

int main(int argc, char* argv[]) {
    std::filesystem::path config_path = "config/region.ini";
    std::string provisioned_region_id;
	std::string provisioned_region_name;
    std::string region_access_key;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument = argv[index];
        if (argument == "--config" && index + 1 < argc) {
            config_path = argv[++index];
        } else if (argument == "--region-id" && index + 1 < argc) {
            provisioned_region_id = argv[++index];
		} else if (argument == "--region-name" && index + 1 < argc) {
			provisioned_region_name = argv[++index];
        } else if (argument == "--access-key" && index + 1 < argc) {
            region_access_key = argv[++index];
        } else if (argument == "--help") {
            std::cout << "Usage: homeworldz-region [--config path-to-region.ini] "
						 "(--region-id uuid | --region-name name) --access-key key" << std::endl;
            return 0;
        } else {
            std::cerr << "Unknown or incomplete argument: " << argument << std::endl;
            return 2;
        }
    }
	if (provisioned_region_id.empty() == provisioned_region_name.empty() || region_access_key.empty()) {
		std::cerr << "Exactly one of --region-id or --region-name, plus --access-key, is required." << std::endl;
        return 2;
    }
	if (provisioned_region_id.empty()) provisioned_region_id = provisioned_region_name;
#ifdef _WIN32
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) return 1;
#endif
    std::signal(SIGINT, stop);
    std::signal(SIGTERM, stop);
    const auto region_version = runtime_version();

    try {
        configured_values = homeworldz::config::load_region_ini(config_path);
        if (!configured_values.empty()) {
            std::cout << "{\"level\":\"info\",\"message\":\"region configuration loaded\",\"path\":"
                      << homeworldz::api::json_string(config_path.string()) << ",\"settings\":"
                      << configured_values.size() << "}" << std::endl;
        }
    } catch (const std::exception& error) {
        std::cerr << "{\"level\":\"error\",\"message\":\"load region configuration failed\",\"error\":"
                  << homeworldz::api::json_string(error.what()) << "}" << std::endl;
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    std::string region_name;
    int region_grid_x{};
    int region_grid_y{};
	int region_size_x{256};
	int region_size_y{256};
    auto region_public_endpoint = configured_value(
        "region.public_endpoint", "http://localhost:" + std::to_string(configured_port()));
    auto grid_public_endpoint = configured_value(
        "grid.public_url", configured_value("grid.url", "http://localhost:42000"));
	auto region_viewer_port = configured_viewer_port();
    const auto region_data_path = std::filesystem::path(
        configured_value("region.data_path", "var/region"));
    const auto terrain_state_path = region_data_path / "terrain.f32";
    // Retire an avatar whose viewer stops answering pings for this long (a lost
    // connection: crash, force-kill, or sustained packet loss) so its KillObject
    // broadcasts promptly rather than waiting on the grid session TTL. The region
    // pings every 5s; this counts missed replies, so an idle-but-connected viewer
    // is never affected. Kept well above transient outages; raise via
    // region.connection_timeout_seconds if your users see longer blips.
    const auto connection_timeout =
        std::chrono::seconds(configured_int("region.connection_timeout_seconds", 60, 15, 3600));
    std::unique_ptr<homeworldz::grid::RegistrationLifecycle> registration;
    std::unique_ptr<homeworldz::grid::Client> viewer_grid;
    std::unique_ptr<homeworldz::grid::ViewerSessionCache> viewer_sessions;
    std::vector<homeworldz::grid::RegionNeighbor> region_neighbors;
    auto next_neighbor_refresh = std::chrono::steady_clock::time_point{};
    const auto refresh_region_neighbors = [&](bool required) {
        const auto retry_at = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        try {
            const auto discovered = viewer_grid ?
                viewer_grid->find_region_neighbors(provisioned_region_id) : std::nullopt;
            if (!discovered) {
                std::cerr << "{\"level\":\"" << (required ? "error" : "warning")
                          << "\",\"message\":\"region neighbor discovery failed\"}" << std::endl;
                next_neighbor_refresh = retry_at;
                return false;
            }
            for (const auto& neighbor : *discovered) {
				const auto source_size = region_size_x / 256;
				const auto neighbor_size = neighbor.size_x / 256;
				const auto overlaps = [](int first, int first_size, int second, int second_size) {
					return first < second + second_size && second < first + first_size;
				};
				bool adjacent{};
				if (neighbor.direction == "north")
					adjacent = neighbor.grid_y == region_grid_y + source_size &&
						overlaps(region_grid_x, source_size, neighbor.grid_x, neighbor_size);
				else if (neighbor.direction == "east")
					adjacent = neighbor.grid_x == region_grid_x + source_size &&
						overlaps(region_grid_y, source_size, neighbor.grid_y, neighbor_size);
				else if (neighbor.direction == "south")
					adjacent = neighbor.grid_y + neighbor_size == region_grid_y &&
						overlaps(region_grid_x, source_size, neighbor.grid_x, neighbor_size);
				else if (neighbor.direction == "west")
					adjacent = neighbor.grid_x + neighbor_size == region_grid_x &&
						overlaps(region_grid_y, source_size, neighbor.grid_y, neighbor_size);
				if (!adjacent) {
                    std::cerr << "{\"level\":\"" << (required ? "error" : "warning")
                              << "\",\"message\":\"invalid region neighbor topology\"}" << std::endl;
                    next_neighbor_refresh = retry_at;
                    return false;
                }
            }
            const bool changed = region_neighbors != *discovered;
            region_neighbors = *discovered;
            if (required || changed) {
                std::cout << "{\"level\":\"info\",\"message\":\"region neighbors discovered\",\"count\":"
                          << region_neighbors.size() << ",\"borders\":[";
                for (std::size_t index = 0; index < region_neighbors.size(); ++index) {
                    if (index != 0) std::cout << ',';
                    std::cout << homeworldz::api::json_string(region_neighbors[index].direction);
                }
                std::cout << "]}" << std::endl;
            }
            next_neighbor_refresh = retry_at;
            return true;
        } catch (const std::exception& error) {
            std::cerr << "{\"level\":\"" << (required ? "error" : "warning")
                      << "\",\"message\":\"region neighbor discovery failed\",\"error\":"
                      << homeworldz::api::json_string(error.what()) << "}" << std::endl;
            next_neighbor_refresh = retry_at;
            return false;
        }
    };
    const auto service_token = configured_value("grid.service_token");
    if (service_token.empty()) {
        std::cerr << "{\"level\":\"error\",\"message\":\"grid service token is required\"}" << std::endl;
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }
    {
        try {
            homeworldz::grid::RegionSettings settings{
                {}, 0, 0,
                region_public_endpoint,
                region_viewer_port,
                configured_int("region.lease_seconds", 60, 10, 300)};
            const auto grid_url = configured_value("grid.url", "http://localhost:42000");
            auto registration_transport = homeworldz::grid::socket_transport(grid_url, region_access_key);
            homeworldz::grid::Client registration_client(registration_transport);
            const auto provisioned = registration_client.register_provisioned_region(
                provisioned_region_id, settings);
            if (!provisioned) {
                std::cerr << "{\"level\":\"error\",\"message\":\"region registration failed\"}" << std::endl;
#ifdef _WIN32
                WSACleanup();
#endif
                return 1;
            }
            region_name = provisioned->name;
            region_grid_x = provisioned->grid_x;
            region_grid_y = provisioned->grid_y;
			region_size_x = provisioned->size_x;
			region_size_y = provisioned->size_y;
			region_public_endpoint = provisioned->public_endpoint;
			region_viewer_port = provisioned->viewer_port;
			grid_public_endpoint = provisioned->grid_public_url;
			settings.public_endpoint = region_public_endpoint;
			settings.viewer_port = region_viewer_port;
			provisioned_region_id = provisioned->id;
            auto viewer_transport = homeworldz::grid::socket_transport(grid_url, service_token);
            viewer_grid = std::make_unique<homeworldz::grid::Client>(std::move(viewer_transport));
            if (!refresh_region_neighbors(true)) {
#ifdef _WIN32
                WSACleanup();
#endif
                return 1;
            }
            viewer_sessions = std::make_unique<homeworldz::grid::ViewerSessionCache>(*viewer_grid);
            registration = std::make_unique<homeworldz::grid::RegistrationLifecycle>(
                std::move(registration_client), std::move(settings), provisioned->id);
            if (!registration->start(std::chrono::steady_clock::now())) {
                std::cerr << "{\"level\":\"error\",\"message\":\"region registration failed\"}" << std::endl;
#ifdef _WIN32
                WSACleanup();
#endif
                return 1;
            }
        } catch (const std::exception& error) {
            std::cerr << "{\"level\":\"error\",\"message\":\"region registration failed\",\"error\":"
                      << homeworldz::api::json_string(error.what()) << "}" << std::endl;
#ifdef _WIN32
            WSACleanup();
#endif
            return 1;
        }
    }

    const auto terrain_width = static_cast<std::size_t>(region_size_x);
    const auto default_heightmap = load_raw_heightmap(configured_value(
        "region.terrain_path", "assets/region/terrain/plateau-square.raw"), terrain_width);
    auto revert_heightmap = default_heightmap ?
        std::make_unique<homeworldz::terrain::Heightmap>(*default_heightmap) :
        std::make_unique<homeworldz::terrain::Heightmap>(terrain_width);
    if (!default_heightmap) revert_heightmap->fill(25.0F);
    auto terrain_heightmap = homeworldz::terrain::load_state(terrain_state_path, terrain_width);
    const bool loaded_region_state = static_cast<bool>(terrain_heightmap);
    if (!terrain_heightmap)
        terrain_heightmap = std::make_unique<homeworldz::terrain::Heightmap>(*revert_heightmap);
    std::cout << "{\"level\":\"info\",\"message\":\"terrain heightmap loaded\",\"source\":\""
              << (loaded_region_state ? "region-state" :
                  (default_heightmap ? "packaged-default" : "flat-fallback"))
              << "\",\"width\":" << terrain_width << "}" << std::endl;
    const auto initial_spawn = default_spawn(*terrain_heightmap);

    homeworldz::scene::Scene scene;
    std::unique_ptr<homeworldz::storage::RegionStorage> storage;
    try {
        storage = std::make_unique<homeworldz::storage::RegionStorage>(
            region_data_path);
        const auto imported_assets = storage->import_asset_directory(
            configured_value("region.asset_path", "assets/region"), system_creator_id);
        if (imported_assets != 0) {
            std::cout << "{\"level\":\"info\",\"message\":\"region assets imported\",\"count\":"
                      << imported_assets << "}" << std::endl;
        }
        if (viewer_grid) {
            const auto assets = storage->list_assets();
            for (const auto& asset : assets) {
                if (!viewer_grid->register_asset(asset.viewer_id, asset.creator_id, asset.sha256,
                                                 asset.size, region_public_endpoint, true)) {
                    const auto authoritative = viewer_grid->find_asset(asset.viewer_id);
                    if (!authoritative || authoritative->sha256 != asset.sha256 ||
                        authoritative->size != asset.size)
                        throw std::runtime_error("register local asset origin failed");
                    const auto reconciled = storage->reconcile_asset_creator(
                        asset.viewer_id, authoritative->creator_id, asset.sha256, asset.size);
                    if (!viewer_grid->register_asset(
                            reconciled.viewer_id, reconciled.creator_id, reconciled.sha256,
                            reconciled.size, region_public_endpoint, true))
                        throw std::runtime_error("register reconciled local asset origin failed");
                    std::cout << "{\"level\":\"warning\",\"message\":\"local asset provenance reconciled\","
                                 "\"assetId\":" << homeworldz::api::json_string(asset.viewer_id)
                              << ",\"creatorId\":"
                              << homeworldz::api::json_string(authoritative->creator_id) << "}"
                              << std::endl;
                }
            }
            if (!assets.empty()) {
                std::cout << "{\"level\":\"info\",\"message\":\"region asset origins registered\",\"count\":"
                          << assets.size() << "}" << std::endl;
            }
        }
        if (storage->load_snapshot(scene)) {
            std::cout << "{\"level\":\"info\",\"message\":\"scene snapshot restored\",\"revision\":"
                      << scene.revision() << ",\"entities\":" << scene.size() << "}" << std::endl;
            std::size_t repaired_texture_entries = 0;
            for (const auto& [entity_id, restored_entity] : scene.entities()) {
                if (restored_entity.object_id.empty()) continue;
                auto* entity = scene.find(entity_id);
                if (entity && homeworldz::viewer::normalize_primitive_texture_entry(
                                  entity->texture_entry, default_prim_texture_entry()))
                    ++repaired_texture_entries;
            }
            if (repaired_texture_entries != 0) {
                storage->save_snapshot(scene);
                std::cout << "{\"level\":\"info\",\"message\":\"legacy primitive textures repaired\",\"count\":"
                          << repaired_texture_entries << "}" << std::endl;
            }
        }
    } catch (const std::exception& error) {
        std::cerr << "{\"level\":\"error\",\"message\":\"open region storage failed\",\"error\":"
                  << homeworldz::api::json_string(error.what()) << "}" << std::endl;
        if (registration) registration->stop();
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }
    const auto apply_task_inventory_transfer = [&](const homeworldz::grid::TaskInventoryTransfer& transfer) {
        homeworldz::scene::Entity* target = nullptr;
        for (const auto& [entity_id, entity] : scene.entities()) {
            if (entity.object_id == transfer.object_id) {
                target = scene.find(entity_id);
                break;
            }
        }
        if (!target || target->owner_id != transfer.user_id) return false;
        const auto existing = std::find_if(
            target->task_inventory.begin(), target->task_inventory.end(),
            [&](const auto& item) { return item.item_id == transfer.task_item_id; });
        if (existing == target->task_inventory.end()) {
            const auto previous_serial = target->task_inventory_serial;
            const auto created = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count());
            const auto& item = transfer.item;
            target->task_inventory.push_back({
                transfer.task_item_id, item.asset_id, item.creator_id, transfer.user_id,
                item.owner_id, "00000000-0000-0000-0000-000000000000", item.name,
                item.description, static_cast<std::int8_t>(item.asset_type),
                static_cast<std::int8_t>(item.inventory_type), item.flags,
                item.base_permissions, item.current_permissions, 0,
                item.everyone_permissions, item.next_permissions,
                static_cast<std::uint8_t>(item.sale_type), item.sale_price, created});
            target->task_inventory_serial = previous_serial == 65535
                ? 1 : static_cast<std::uint16_t>(previous_serial + 1);
            try {
                storage->save_snapshot(scene);
            } catch (...) {
                target->task_inventory.pop_back();
                target->task_inventory_serial = previous_serial;
                throw;
            }
        }
        return viewer_grid && viewer_grid->finalize_task_inventory_transfer(
            transfer.id, provisioned_region_id);
    };
    const auto apply_task_inventory_extraction = [&](const homeworldz::grid::TaskInventoryExtraction& extraction)
        -> std::optional<homeworldz::grid::TaskInventoryExtraction> {
        homeworldz::scene::Entity* target = nullptr;
        for (const auto& [entity_id, entity] : scene.entities()) {
            if (entity.object_id == extraction.object_id) {
                target = scene.find(entity_id);
                break;
            }
        }
        if (!target || target->owner_id != extraction.user_id) return std::nullopt;
        const auto found = std::find_if(
            target->task_inventory.begin(), target->task_inventory.end(),
            [&](const auto& item) { return item.item_id == extraction.source_task_item_id; });
        if (found != target->task_inventory.end()) {
            if (found->owner_id != extraction.user_id ||
                found->asset_id != extraction.item.asset_id ||
                (found->current_permissions & homeworldz::scene::permission_copy) != 0)
                return std::nullopt;
            const auto index = static_cast<std::size_t>(
                std::distance(target->task_inventory.begin(), found));
            const auto removed = *found;
            const auto previous_serial = target->task_inventory_serial;
            target->task_inventory.erase(found);
            target->task_inventory_serial = previous_serial == 65535
                ? 1 : static_cast<std::uint16_t>(previous_serial + 1);
            try {
                storage->save_snapshot(scene);
            } catch (...) {
                target->task_inventory.insert(
                    target->task_inventory.begin() + static_cast<std::ptrdiff_t>(index), removed);
                target->task_inventory_serial = previous_serial;
                throw;
            }
        }
        return viewer_grid
            ? viewer_grid->finalize_task_inventory_extraction(extraction.id, provisioned_region_id)
            : std::nullopt;
    };
    if (viewer_grid) {
        try {
            const auto pending = viewer_grid->pending_task_inventory_transfers(provisioned_region_id);
            if (!pending) throw std::runtime_error("load pending task inventory transfers");
            std::size_t reconciled = 0;
            for (const auto& transfer : *pending)
                if (apply_task_inventory_transfer(transfer)) ++reconciled;
            if (!pending->empty()) {
                std::cout << "{\"level\":"
                          << (reconciled == pending->size() ? "\"info\"" : "\"warning\"")
                          << ",\"message\":\"pending task inventory transfers reconciled\",\"completed\":"
                          << reconciled << ",\"pending\":" << pending->size() << "}"
                          << std::endl;
            }
        } catch (const std::exception& error) {
            std::cerr << "{\"level\":\"warning\",\"message\":\"pending task inventory transfer reconciliation failed\",\"error\":"
                      << homeworldz::api::json_string(error.what()) << "}" << std::endl;
        }
        try {
            const auto pending = viewer_grid->pending_task_inventory_extractions(provisioned_region_id);
            if (!pending) throw std::runtime_error("load pending task inventory extractions");
            std::size_t reconciled = 0;
            for (const auto& extraction : *pending)
                if (apply_task_inventory_extraction(extraction)) ++reconciled;
            if (!pending->empty()) {
                std::cout << "{\"level\":"
                          << (reconciled == pending->size() ? "\"info\"" : "\"warning\"")
                          << ",\"message\":\"pending task inventory extractions reconciled\",\"completed\":"
                          << reconciled << ",\"pending\":" << pending->size() << "}"
                          << std::endl;
            }
        } catch (const std::exception& error) {
            std::cerr << "{\"level\":\"warning\",\"message\":\"pending task inventory extraction reconciliation failed\",\"error\":"
                      << homeworldz::api::json_string(error.what()) << "}" << std::endl;
        }
        try {
            const auto pending = viewer_grid->pending_object_rezzes(provisioned_region_id);
            if (!pending) throw std::runtime_error("load pending object rezzes");
            std::size_t reconciled = 0;
            for (const auto& rez : *pending) {
                const auto exists = std::any_of(scene.entities().begin(), scene.entities().end(),
                    [&](const auto& entry) { return entry.second.object_id == rez.object_id; });
                const auto completed = exists
                    ? viewer_grid->finalize_object_rez(rez.id, provisioned_region_id)
                    : viewer_grid->rollback_object_rez(rez.id, provisioned_region_id);
                if (completed) ++reconciled;
            }
            if (!pending->empty()) {
                std::cout << "{\"level\":"
                          << (reconciled == pending->size() ? "\"info\"" : "\"warning\"")
                          << ",\"message\":\"pending object rezzes reconciled\",\"completed\":"
                          << reconciled << ",\"pending\":" << pending->size() << "}"
                          << std::endl;
            }
        } catch (const std::exception& error) {
            std::cerr << "{\"level\":\"warning\",\"message\":\"pending object rez reconciliation failed\",\"error\":"
                      << homeworldz::api::json_string(error.what()) << "}" << std::endl;
        }
    }
    const auto read_federated_asset = [&](std::string_view asset_id) -> std::vector<std::byte> {
        try {
            return storage->read_asset(asset_id);
        } catch (const std::exception&) {
            if (!viewer_grid) throw;
        }
        const auto metadata = viewer_grid->find_asset(asset_id);
        if (!metadata) throw std::runtime_error("asset federation metadata was not found");
        const auto normalized_region_endpoint = [&] {
            auto endpoint = region_public_endpoint;
            while (!endpoint.empty() && endpoint.back() == '/') endpoint.pop_back();
            return endpoint;
        }();
        for (const auto& location : metadata->locations) {
            auto endpoint = location.endpoint;
            while (!endpoint.empty() && endpoint.back() == '/') endpoint.pop_back();
            if (endpoint == normalized_region_endpoint) continue;
            homeworldz::grid::HttpResponse response;
            try {
                response = homeworldz::grid::fetch_asset_from(
                    endpoint, service_token, metadata->asset_id);
            } catch (const std::exception&) {
                continue;
            }
            if (response.status_code != 200 || response.body.size() != metadata->size) continue;
            const auto content = std::span(
                reinterpret_cast<const std::byte*>(response.body.data()), response.body.size());
            if (homeworldz::crypto::sha256_hex(content) != metadata->sha256) continue;
            const auto stored = storage->store_asset(
                metadata->asset_id, metadata->creator_id, content);
            if (!viewer_grid->register_asset(
                    stored.viewer_id, stored.creator_id, stored.sha256, stored.size,
                    region_public_endpoint, false))
                throw std::runtime_error("register replicated asset failed");
            std::cout << "{\"level\":\"info\",\"message\":\"region asset replicated\",\"assetId\":"
                      << homeworldz::api::json_string(stored.viewer_id) << ",\"source\":"
                      << homeworldz::api::json_string(endpoint) << "}" << std::endl;
            return {content.begin(), content.end()};
        }
        throw std::runtime_error("no verified asset replica was available");
    };

    // --- Server-side appearance baking (ADR 0029) --------------------------
    // The default outfit is identical for every default avatar, so its bake is
    // computed once (lazily) and reused. Baking is inline; the one-time first
    // call fetches the bundled default wearables/textures, composites the bake
    // slots, and stores the results as content-addressed assets.
    // Unbaked bake slots fall back to this face. It must be IMG_DEFAULT_AVATAR
    // — the viewer's "no bake here, never rendered" sentinel — so slots we do
    // not produce (e.g. the skirt when no skirt is worn) render nothing and do
    // not block the avatar from being treated as fully baked. (IMG_WHITE draws
    // a solid mesh; IMG_INVISIBLE is treated as an unfinished bake -> cloud.)
    const auto server_bake_default_face =
        homeworldz::viewer::parse_uuid("c228d1cf-4b5d-4ba8-84f4-899a0796aa97").value();
    const auto fetch_asset_bytes =
        [&](const homeworldz::viewer::Uuid& id) -> std::optional<std::vector<std::byte>> {
        try {
            auto bytes = read_federated_asset(homeworldz::viewer::format_uuid(id));
            if (bytes.empty()) return std::nullopt;
            return bytes;
        } catch (const std::exception&) {
            return std::nullopt;
        }
    };
    // Clothing alpha masks (TGA) are bundled region files, read straight from
    // the default-avatar asset directory by name (they are not UUID-named
    // assets, so they are not imported into the content-addressed store).
    const std::filesystem::path mask_directory =
        std::filesystem::path(configured_value("region.asset_path", "assets/region")) /
        "default-avatar";
    const auto fetch_mask_bytes =
        [mask_directory](std::string_view name) -> std::optional<std::vector<std::uint8_t>> {
        std::ifstream input(mask_directory / (std::string(name) + ".tga"),
                            std::ios::binary | std::ios::ate);
        if (!input) return std::nullopt;
        const auto length = static_cast<std::streamsize>(input.tellg());
        if (length <= 0) return std::nullopt;
        input.seekg(0);
        std::vector<std::uint8_t> out(static_cast<std::size_t>(length));
        input.read(reinterpret_cast<char*>(out.data()), length);
        if (!input) return std::nullopt;
        return out;
    };
    std::optional<homeworldz::viewer::OutfitBake> default_outfit_bake;
    std::vector<std::uint8_t> default_outfit_visual_params;
    bool default_outfit_bake_attempted = false;
    const auto ensure_default_outfit_bake = [&]() -> const homeworldz::viewer::OutfitBake* {
        if (default_outfit_bake) return &*default_outfit_bake;
        if (default_outfit_bake_attempted) return nullptr;
        default_outfit_bake_attempted = true;
        static const char* const default_wearable_asset_ids[] = {
            "66c41e39-38f9-f75a-024e-585989bfab73",  // Default Shape
            "77c41e39-38f9-f75a-024e-585989bbabbb",  // Default Skin
            "d342e6c0-b9d2-11dc-95ff-0800200c9a66",  // Default Hair
            "4bb6fa4d-1cd2-498a-a84c-95c1a0e745a7",  // Default Eyes
            "00000000-38f9-1111-024e-222222111110",  // Default Shirt
            "00000000-38f9-1111-024e-222222111120",  // Default Pants
        };
        std::vector<homeworldz::viewer::Uuid> wearable_ids;
        for (const char* id : default_wearable_asset_ids)
            if (const auto parsed = homeworldz::viewer::parse_uuid(id))
                wearable_ids.push_back(*parsed);
        auto baked = homeworldz::viewer::bake_worn_outfit(
            wearable_ids, server_bake_default_face, fetch_asset_bytes, fetch_mask_bytes);
        if (!baked) {
            std::cerr << "{\"level\":\"warning\",\"message\":\"server default-outfit bake failed\"}"
                      << std::endl;
            return nullptr;
        }
        const std::string system_creator = "00000000-0000-0000-0000-000000000000";
        for (const auto& asset : baked->assets) {
            try {
                const auto content =
                    std::span<const std::byte>(asset.j2c.data(), asset.j2c.size());
                const auto record = storage->store_asset(
                    homeworldz::viewer::format_uuid(asset.id), system_creator, content);
                if (viewer_grid)
                    static_cast<void>(viewer_grid->register_asset(
                        record.viewer_id, record.creator_id, record.sha256, record.size,
                        region_public_endpoint, true));
            } catch (const std::exception& error) {
                std::cerr << "{\"level\":\"warning\",\"message\":\"store baked texture failed\",\"error\":"
                          << homeworldz::api::json_string(error.what()) << "}" << std::endl;
            }
        }
        // appearance_version 1 marks this as a server-side bake so the viewer
        // uses the baked textures directly instead of compositing locally.
        default_outfit_visual_params = homeworldz::viewer::build_visual_params(baked->worn, 1);
        default_outfit_bake = std::move(*baked);
        std::cout << "{\"level\":\"info\",\"message\":\"server default-outfit bake ready\",\"slots\":"
                  << default_outfit_bake->assets.size() << ",\"visualParams\":"
                  << default_outfit_visual_params.size() << "}" << std::endl;
        return &*default_outfit_bake;
    };

    homeworldz::simulation::FixedStepLoop simulation(scene);
#ifdef HOMEWORLDZ_JOLT_AVAILABLE
    auto physics_world = homeworldz::physics::make_jolt_world();
    std::cout << "{\"level\":\"info\",\"message\":\"production physics initialized\","
                 "\"backend\":\"jolt\"}" << std::endl;
#else
    std::unique_ptr<homeworldz::physics::World> physics_world;
    std::cout << "{\"level\":\"warning\",\"message\":\"production physics unavailable\"}" << std::endl;
#endif
    homeworldz::physics::BodyId physics_terrain{};
    const auto synchronize_physics_terrain = [&]() {
        if (!physics_world) return false;
        try {
            homeworldz::physics::HeightFieldDefinition definition;
            definition.samples.assign(terrain_heightmap->begin(), terrain_heightmap->end());
            definition.sample_count = static_cast<std::uint32_t>(terrain_heightmap->width());
            // Terrain samples describe the complete region extent. There are
            // sample_count - 1 intervals between the first sample at 0 and the
            // far region border; unit spacing would incorrectly end the collision
            // surface one metre early and let edge-bound bodies fall off.
            definition.spacing = static_cast<double>(terrain_heightmap->width()) /
                static_cast<double>(definition.sample_count - 1);
            const auto replacement = physics_world->create_heightfield(definition);
            if (replacement == 0) return false;
            if (physics_terrain != 0) physics_world->remove_body(physics_terrain);
            physics_terrain = replacement;
            return true;
        } catch (const std::exception& error) {
            std::cerr << "{\"level\":\"error\",\"message\":\"physics terrain synchronization failed\","
                         "\"error\":" << homeworldz::api::json_string(error.what()) << "}" << std::endl;
            return false;
        }
    };
    const auto physics_terrain_ready = synchronize_physics_terrain();
    std::cout << "{\"level\":" << (physics_terrain_ready ? "\"info\"" : "\"warning\"")
              << ",\"message\":\"physics terrain initialized\",\"samples\":"
              << terrain_heightmap->size() << ",\"synchronized\":"
              << (physics_terrain_ready ? "true" : "false") << "}" << std::endl;
    std::unique_ptr<homeworldz::physics::StaticSceneMirror> physics_scene;
    std::unordered_map<homeworldz::scene::EntityId, std::size_t> physics_edit_suspended;
    std::unordered_map<std::string,
        std::unordered_map<homeworldz::scene::EntityId, homeworldz::scene::EntityId>>
        physics_edit_selections;
    if (physics_world) {
        try {
            physics_scene = std::make_unique<homeworldz::physics::StaticSceneMirror>(*physics_world);
            physics_scene->synchronize(scene);
            std::cout << "{\"level\":\"info\",\"message\":\"static scene physics synchronized\","
                         "\"bodies\":" << physics_scene->size() << "}" << std::endl;
        } catch (const std::exception& error) {
            physics_scene.reset();
            std::cerr << "{\"level\":\"error\",\"message\":\"static scene physics synchronization failed\","
                         "\"error\":" << homeworldz::api::json_string(error.what()) << "}" << std::endl;
        }
    }
    const auto synchronize_physics_object = [&](const homeworldz::scene::Entity& entity) {
        if (!physics_scene) return;
        try {
            const auto root_id = entity.parent_id == 0 ? entity.id : entity.parent_id;
            if (!physics_scene->synchronize_linkset(
                    scene, root_id, physics_edit_suspended.contains(root_id)))
                std::cerr << "{\"level\":\"warning\",\"message\":\"object physics synchronization rejected\","
                             "\"entityId\":" << root_id << "}" << std::endl;
        } catch (const std::exception& error) {
            std::cerr << "{\"level\":\"error\",\"message\":\"static object physics synchronization failed\","
                         "\"entityId\":" << entity.id << ",\"error\":"
                      << homeworldz::api::json_string(error.what()) << "}" << std::endl;
        }
    };
    const auto remove_physics_object = [&](homeworldz::scene::EntityId entity_id) {
        physics_edit_suspended.erase(entity_id);
        for (auto& [selection_endpoint, selected] : physics_edit_selections) {
            static_cast<void>(selection_endpoint);
            std::erase_if(selected, [&](const auto& entry) {
                return entry.first == entity_id || entry.second == entity_id;
            });
        }
        if (physics_scene) physics_scene->remove(entity_id);
    };
    const auto collision_ground_height = [&](const homeworldz::scene::Vector3& position) {
        if (physics_world && physics_terrain != 0) {
            constexpr double ray_origin_height = 4096.0;
            constexpr double ray_distance = 8192.0;
            const auto hit = physics_world->ray_cast_body(
                physics_terrain, {position.x, position.y, ray_origin_height}, {0, 0, -1}, ray_distance);
            if (hit) return hit->point.z;
        }
        return ground_height(*terrain_heightmap, position);
    };
    const auto raise_physical_object_above_terrain = [&](homeworldz::scene::Entity& entity) {
        if (!entity.physical) return false;
        const auto squared = entity.rotation.x * entity.rotation.x +
                             entity.rotation.y * entity.rotation.y +
                             entity.rotation.z * entity.rotation.z;
        const std::array<double, 4> rotation{
            entity.rotation.x, entity.rotation.y, entity.rotation.z,
            std::sqrt((std::max)(0.0, 1.0 - squared))};
        const auto half_extents =
            homeworldz::physics::rotated_box_half_extents(entity.scale, rotation);
        const auto terrain_maximum = static_cast<int>(terrain_width - 1);
        const auto minimum_x = std::clamp(
            static_cast<int>(std::floor(entity.position.x - half_extents.x)), 0, terrain_maximum);
        const auto maximum_x = std::clamp(
            static_cast<int>(std::ceil(entity.position.x + half_extents.x)), 0, terrain_maximum);
        const auto minimum_y = std::clamp(
            static_cast<int>(std::floor(entity.position.y - half_extents.y)), 0, terrain_maximum);
        const auto maximum_y = std::clamp(
            static_cast<int>(std::ceil(entity.position.y + half_extents.y)), 0, terrain_maximum);
        double maximum_ground = -std::numeric_limits<double>::infinity();
        for (int y = minimum_y; y <= maximum_y; ++y)
            for (int x = minimum_x; x <= maximum_x; ++x)
                maximum_ground = (std::max)(maximum_ground,
                    static_cast<double>((*terrain_heightmap)[
                        static_cast<std::size_t>(y) * terrain_width + x]));
        constexpr double terrain_clearance = 0.01;
        const auto required_origin_z = maximum_ground + half_extents.z + terrain_clearance;
        if (entity.position.z >= required_origin_z) return false;
        entity.position.z = required_origin_z;
        entity.velocity = {};
        return true;
    };
    bool recovered_escaped_objects{};
    std::vector<homeworldz::scene::EntityId> persisted_entity_ids;
    persisted_entity_ids.reserve(scene.size());
    for (const auto& [entity_id, entity] : scene.entities()) {
        static_cast<void>(entity);
        persisted_entity_ids.push_back(entity_id);
    }
    for (const auto entity_id : persisted_entity_ids) {
        auto* entity = scene.find(entity_id);
        if (!entity || !entity->physical) continue;
        const auto original_x = entity->position.x;
        const auto original_y = entity->position.y;
        entity->position.x = std::clamp(entity->position.x, 0.0, static_cast<double>(region_size_x));
        entity->position.y = std::clamp(entity->position.y, 0.0, static_cast<double>(region_size_y));
        const bool escaped = entity->position.x != original_x || entity->position.y != original_y;
        if (escaped) {
            entity->velocity.x = 0.0;
            entity->velocity.y = 0.0;
        }
        const bool raised = (escaped || entity->position.z < -64.0) &&
            raise_physical_object_above_terrain(*entity);
        if (!escaped && !raised) continue;
        recovered_escaped_objects = true;
        synchronize_physics_object(*entity);
        std::cout << "{\"level\":\"warning\",\"message\":\"escaped physical object recovered\",\"entityId\":"
                  << entity->id << ",\"position\":[" << entity->position.x << ','
                  << entity->position.y << ',' << entity->position.z << "]}" << std::endl;
    }
    if (recovered_escaped_objects) {
        try {
            storage->save_snapshot(scene);
        } catch (const std::exception& error) {
            std::cerr << "{\"level\":\"error\",\"message\":\"escaped object recovery persistence failed\",\"error\":"
                      << homeworldz::api::json_string(error.what()) << "}" << std::endl;
            return 1;
        }
    }
    auto previous_tick = std::chrono::steady_clock::now();
    auto next_snapshot = previous_tick + std::chrono::seconds(30);
    auto next_dynamic_sync = previous_tick;

    const auto server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server == invalid_socket) return 1;
    int reuse = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&reuse), sizeof(reuse));

    sockaddr_in address{};
    if (!configured_bind_address(address, "region.bind_address", configured_port()) ||
        bind(server, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
        listen(server, 16) != 0) {
        close_socket(server);
        return 1;
    }

    const auto viewer_server = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (viewer_server == invalid_socket) {
        close_socket(server);
        return 1;
    }
    sockaddr_in viewer_address{};
    if (!configured_bind_address(viewer_address, "region.viewer_bind_address", region_viewer_port) ||
        bind(viewer_server, reinterpret_cast<sockaddr*>(&viewer_address), sizeof(viewer_address)) != 0) {
        close_socket(viewer_server);
        close_socket(server);
        return 1;
    }
    homeworldz::region::InboundTransitRegistry inbound_transits;
    homeworldz::region::CapabilityArrivalGate capability_arrival_gate;
    homeworldz::viewer::CircuitRegistry circuits([&](const homeworldz::viewer::UseCircuitCode& request) {
        const auto reject = [&](std::string_view reason) {
            std::cout << "{\"level\":\"warn\",\"message\":\"viewer circuit rejected\",\"reason\":"
                      << homeworldz::api::json_string(reason)
                      << ",\"circuitCode\":" << request.circuit_code
                      << ",\"sessionId\":"
                      << homeworldz::api::json_string(homeworldz::viewer::format_uuid(request.session_id))
                      << ",\"agentId\":"
                      << homeworldz::api::json_string(homeworldz::viewer::format_uuid(request.agent_id))
                      << "}" << std::endl;
            return false;
        };
        if (!registration || !viewer_sessions) return reject("region_not_registered");
        std::optional<homeworldz::grid::ViewerSession> session;
        try {
            session = viewer_sessions->validate(homeworldz::viewer::format_uuid(request.session_id));
        } catch (const std::exception& error) {
            std::cout << "{\"level\":\"error\",\"message\":\"viewer session validation failed\",\"error\":"
                      << homeworldz::api::json_string(error.what()) << "}" << std::endl;
            return reject("session_validation_error");
        }
        if (!session) return reject("session_not_found");
        if (session->circuit_code != request.circuit_code) return reject("circuit_code_mismatch");
        const auto agent = homeworldz::viewer::parse_uuid(session->agent_id);
        if (!agent) return reject("invalid_session_agent");
        if (*agent != request.agent_id) return reject("agent_id_mismatch");
        if (session->destination_region_id != registration->region_id()) {
            const auto transit = inbound_transits.authorize(
                session->agent_id, session->session_id, std::chrono::steady_clock::now());
            if (!transit || transit->destination_region_id != registration->region_id())
                return reject("destination_region_mismatch");
        }
        std::cout << "{\"level\":\"info\",\"message\":\"viewer circuit authorized\",\"circuitCode\":"
                  << request.circuit_code << ",\"sessionId\":"
                  << homeworldz::api::json_string(homeworldz::viewer::format_uuid(request.session_id))
                  << "}" << std::endl;
        return true;
    });
    std::unordered_set<std::string> handshake_replies;
    std::unordered_set<std::string> established_events;
    std::unordered_map<std::string, LiveAvatar> avatars;
    std::unordered_map<std::string, homeworldz::viewer::AvatarGeometry> avatar_geometries;
    std::unordered_map<std::string, homeworldz::viewer::AgentSetAppearance> avatar_appearances;
    std::unordered_map<std::string, std::vector<homeworldz::viewer::AvatarAnimationEntry>> avatar_animations;
    std::unordered_map<std::string, std::int32_t> next_animation_sequences;
    std::unordered_map<std::string, homeworldz::viewer::MovementAnimation> movement_animations;
    std::unordered_map<std::string, homeworldz::viewer::UuidName> resolved_avatar_names;
    std::unordered_map<std::string, std::deque<QueuedTexturePacket>> texture_packets;
    std::unordered_set<std::string> active_texture_transfers;
    std::unordered_map<std::string, PendingInventoryUpload> pending_inventory_uploads;
    std::unordered_map<std::string, PendingInventoryAssetUpdate> pending_inventory_asset_updates;
    std::unordered_map<std::string, PendingInventoryAssetUpload> pending_inventory_asset_uploads;
    std::unordered_map<std::string, PendingInventoryAssetXfer> pending_inventory_asset_xfers;
    std::unordered_map<std::string, std::vector<std::byte>> pending_task_inventory_files;
    std::unordered_map<std::string, PendingTaskInventoryXfer> pending_task_inventory_xfers;
    std::unordered_map<std::string,
        std::unordered_map<homeworldz::scene::EntityId, SentDynamicTransform>>
        sent_dynamic_transforms;
    std::unordered_map<homeworldz::scene::EntityId, std::chrono::steady_clock::time_point>
        temporary_expirations;
    std::unordered_map<std::string, std::deque<std::string>> queued_viewer_events;
    std::vector<PendingEventResponse> pending_event_responses;
    std::vector<PendingAgentMovementComplete> pending_agent_movement_completes;
    std::uint64_t event_id{};
    std::uint64_t next_inventory_asset_xfer{1};
    homeworldz::script::FalconRuntime falcon([&](homeworldz::script::FalconHostMessage message) {
        if (message.text.empty() || message.text.size() > 1023) return;
        const auto object_id = homeworldz::viewer::parse_uuid(message.identity.object_id);
        const auto owner_id = homeworldz::viewer::parse_uuid(message.identity.owner_id);
        if (!object_id || !owner_id) return;
        const homeworldz::scene::Entity* speaker = nullptr;
        for (const auto& [entity_id, candidate] : scene.entities()) {
            static_cast<void>(entity_id);
            if (candidate.object_id == message.identity.object_id) {
                speaker = &candidate;
                break;
            }
        }
        if (!speaker) return;
        if (!message.owner_only && message.channel != 0) return;

        homeworldz::viewer::ChatFromSimulator chat;
        chat.from_name = speaker->name.empty() ? "Object" : speaker->name;
        chat.source_id = *object_id;
        chat.owner_id = *owner_id;
        chat.source_type = 0x02;
        chat.chat_type = message.owner_only ? 0x08 : 0x01;
        chat.audible = 0x01;
        chat.position = {static_cast<float>(speaker->position.x),
                         static_cast<float>(speaker->position.y),
                         static_cast<float>(speaker->position.z)};
        chat.message = std::move(message.text);
        const auto payload = homeworldz::viewer::encode_chat_from_simulator(chat);
        const auto sent_at = std::chrono::steady_clock::now();
        for (const auto& [recipient_endpoint, recipient] : avatars) {
            if (message.owner_only) {
                if (recipient.user_id != message.identity.owner_id) continue;
            } else {
                const auto& target = recipient.controller.state().position;
                const auto dx = target.x - speaker->position.x;
                const auto dy = target.y - speaker->position.y;
                const auto dz = target.z - speaker->position.z;
                if (dx * dx + dy * dy + dz * dz > 20.0 * 20.0) continue;
            }
            if (const auto outgoing = circuits.send(
                    recipient_endpoint, payload, true, sent_at))
                static_cast<void>(send_udp(viewer_server, recipient_endpoint, *outgoing));
        }
    });
    const auto rez_task_script = [&](const homeworldz::scene::Entity& entity,
                                     const homeworldz::scene::TaskInventoryItem& item,
                                     bool enabled) {
        try {
            const auto asset = read_federated_asset(item.asset_id);
            const auto source = std::string(
                reinterpret_cast<const char*>(asset.data()), asset.size());
            return falcon.rez(
                {item.asset_id, item.item_id, entity.object_id, entity.owner_id},
                source, enabled);
        } catch (const std::exception& error) {
            return homeworldz::script::FalconRezResult{false, false, error.what()};
        }
    };
    // Re-send an entity's ObjectUpdate to every nearby viewer. A script rez,
    // recompile, enable, disable, or removal changes the SCRIPTED / HANDLE_TOUCH
    // flags, and the viewer only learns the new flags from a fresh update; without
    // this it keeps showing Touch as disabled on a freshly touch-enabled prim.
    const auto broadcast_object_update = [&](const homeworldz::scene::Entity& entity,
                                             std::chrono::steady_clock::time_point when) {
        const auto region_handle =
            (static_cast<std::uint64_t>(region_grid_x * 256) << 32) |
            static_cast<std::uint32_t>(region_grid_y * 256);
        for (const auto& [recipient_endpoint, recipient] : avatars) {
            const auto object =
                static_object_from_entity(scene, entity, recipient.user_id, falcon);
            if (!object) continue;
            if (const auto sent = circuits.send(
                    recipient_endpoint,
                    homeworldz::viewer::encode_static_object_update(region_handle, *object),
                    true, when, true))
                static_cast<void>(send_udp(viewer_server, recipient_endpoint, *sent));
        }
    };
    // Restore enabled task scripts after a Region restart. VM state is not yet
    // persisted, so each restored script starts fresh and re-runs state_entry;
    // this is enough to re-establish SCRIPTED / HANDLE_TOUCH advertising and live
    // event handling so touch works after a restart. Persisting and resuming VM
    // state across restarts is separate future work.
    {
        std::size_t restored_scripts = 0;
        for (const auto& [entity_id, entity] : scene.entities()) {
            static_cast<void>(entity_id);
            for (const auto& item : entity.task_inventory) {
                if (item.asset_type != 10) continue; // LSL script asset type
                const auto result = rez_task_script(entity, item, true);
                if (result.compiled) {
                    ++restored_scripts;
                } else {
                    std::cerr << "{\"level\":\"warning\",\"message\":\"task script restore failed\",\"objectId\":"
                              << homeworldz::api::json_string(entity.object_id)
                              << ",\"itemId\":" << homeworldz::api::json_string(item.item_id)
                              << ",\"diagnostic\":"
                              << homeworldz::api::json_string(result.diagnostic) << "}"
                              << std::endl;
                }
            }
        }
        if (restored_scripts != 0)
            std::cout << "{\"level\":\"info\",\"message\":\"task scripts restored\",\"count\":"
                      << restored_scripts << "}" << std::endl;
    }
    const auto clear_viewer_endpoint = [&](const std::string& endpoint, const std::string& session_id) {
        // Tell the remaining viewers to remove this avatar's rezzed
        // representation. Every avatar-removal path (logout, disconnect/timeout,
        // duplicate-login replacement, and teleport/crossing source-retirement
        // via departed_avatars) funnels through here, and it runs only once the
        // avatar has actually left this region — so it is safe for crossings and
        // teleports: a rolled-back crossing never reaches this point, and the
        // destination region independently rezzes the avatar for viewers there.
        if (const auto departing = avatars.find(endpoint); departing != avatars.end()) {
            const std::array<std::uint32_t, 1> kill_ids{
                static_cast<std::uint32_t>(departing->second.entity_id)};
            const auto kill = homeworldz::viewer::encode_kill_object(kill_ids);
            const auto kill_now = std::chrono::steady_clock::now();
            std::size_t kill_recipients = 0;
            for (const auto& [recipient_endpoint, recipient] : avatars) {
                static_cast<void>(recipient);
                if (recipient_endpoint == endpoint) continue;
                if (const auto outgoing = circuits.send(
                        recipient_endpoint, kill, true, kill_now, true)) {
                    static_cast<void>(send_udp(viewer_server, recipient_endpoint, *outgoing));
                    ++kill_recipients;
                }
            }
            std::cout << "{\"level\":\"info\",\"message\":\"avatar departure kill broadcast\","
                         "\"localId\":" << kill_ids[0] << ",\"recipients\":" << kill_recipients
                      << "}" << std::endl;
        }
        if (const auto live = avatars.find(endpoint); live != avatars.end() &&
            physics_world && live->second.physics_character != 0)
            physics_world->remove_character(live->second.physics_character);
        if (const auto selected = physics_edit_selections.find(endpoint);
            selected != physics_edit_selections.end()) {
            for (const auto& [selected_id, root_id] : selected->second) {
                static_cast<void>(selected_id);
                const auto suspended = physics_edit_suspended.find(root_id);
                if (suspended == physics_edit_suspended.end()) continue;
                if (--suspended->second == 0) {
                    physics_edit_suspended.erase(suspended);
                    if (const auto* entity = scene.find(root_id)) synchronize_physics_object(*entity);
                }
            }
            physics_edit_selections.erase(selected);
        }
        avatars.erase(endpoint);
        avatar_geometries.erase(endpoint);
        avatar_appearances.erase(endpoint);
        avatar_animations.erase(endpoint);
        next_animation_sequences.erase(endpoint);
        movement_animations.erase(endpoint);
        handshake_replies.erase(endpoint);
        established_events.erase(session_id);
        queued_viewer_events.erase(session_id);
        capability_arrival_gate.clear_session(session_id);
        std::erase_if(pending_agent_movement_completes,
            [&](const PendingAgentMovementComplete& pending) {
                return pending.endpoint == endpoint;
            });
        texture_packets.erase(endpoint);
        std::erase_if(active_texture_transfers, [&](const std::string& key) {
            return key.starts_with(endpoint + '|');
        });
        std::erase_if(pending_inventory_asset_uploads, [&](const auto& entry) {
            return entry.first.starts_with(endpoint + '|');
        });
        std::erase_if(pending_inventory_asset_xfers, [&](const auto& entry) {
            return entry.first.starts_with(endpoint + '|');
        });
        std::erase_if(pending_inventory_asset_updates, [&](const auto& entry) {
            return entry.second.session_id == session_id;
        });
        std::erase_if(pending_task_inventory_files, [&](const auto& entry) {
            return entry.first.starts_with(endpoint + '|');
        });
        std::erase_if(pending_task_inventory_xfers, [&](const auto& entry) {
            return entry.first.starts_with(endpoint + '|');
        });
        sent_dynamic_transforms.erase(endpoint);
        std::erase_if(pending_event_responses, [&](const PendingEventResponse& pending) {
            if (pending.session_id != session_id) return false;
            close_socket(pending.client);
            return true;
        });
    };

    const auto take_viewer_events = [&](const std::string& session_id) {
        std::vector<std::string> events;
        const auto queued = queued_viewer_events.find(session_id);
        if (queued == queued_viewer_events.end()) return events;
        events.assign(std::make_move_iterator(queued->second.begin()),
                      std::make_move_iterator(queued->second.end()));
        queued_viewer_events.erase(queued);
        return events;
    };
    const auto flush_pending_viewer_events = [&](const std::string& session_id) {
        const auto queued = queued_viewer_events.find(session_id);
        if (queued == queued_viewer_events.end() || queued->second.empty()) return;
        const auto pending = std::find_if(pending_event_responses.begin(), pending_event_responses.end(),
            [&](const PendingEventResponse& candidate) { return candidate.session_id == session_id; });
        if (pending == pending_event_responses.end()) return;
        const auto events = take_viewer_events(session_id);
        const auto response = homeworldz::http::response_for_content(
            pending->request, 200, "application/llsd+xml",
            homeworldz::viewer::event_queue_xml(++event_id, events));
        static_cast<void>(send_all(pending->client, response.content));
        finish_http_response(pending->client);
        close_socket(pending->client);
        std::cout << "{\"level\":\"info\",\"message\":\"http request\",\"requestId\":"
                  << homeworldz::api::json_string(response.request_id)
                  << ",\"method\":" << homeworldz::api::json_string(response.method)
                  << ",\"path\":" << homeworldz::api::json_string(response.path)
                  << ",\"status\":" << response.status_code << "}" << std::endl;
        pending_event_responses.erase(pending);
    };
    const auto enqueue_viewer_event = [&](const std::string& session_id, std::string event) {
        queued_viewer_events[session_id].push_back(std::move(event));
        flush_pending_viewer_events(session_id);
    };

    std::cout << "{\"level\":\"info\",\"message\":\"region service listening\",\"httpPort\":"
              << configured_port() << ",\"viewerPort\":" << region_viewer_port << "}" << std::endl;
    while (running) {
        const auto http_now = std::chrono::steady_clock::now();
        std::erase_if(pending_event_responses, [&](const PendingEventResponse& pending) {
            if (pending.deadline > http_now) return false;
            const auto response = homeworldz::http::response_for_content(
                pending.request, 200, "application/llsd+xml",
                homeworldz::viewer::event_queue_xml(++event_id));
            static_cast<void>(send_all(pending.client, response.content));
            finish_http_response(pending.client);
            close_socket(pending.client);
            std::cout << "{\"level\":\"info\",\"message\":\"http request\",\"requestId\":"
                      << homeworldz::api::json_string(response.request_id)
                      << ",\"method\":" << homeworldz::api::json_string(response.method)
                      << ",\"path\":" << homeworldz::api::json_string(response.path)
                      << ",\"status\":" << response.status_code << "}" << std::endl;
            return true;
        });
        std::erase_if(pending_agent_movement_completes,
            [&](const PendingAgentMovementComplete& pending) {
                const bool seed_served = capability_arrival_gate.consume_seed(
                    pending.session_id, pending.visit_id);
                if (!seed_served && pending.deadline > http_now) return false;
                if (const auto outgoing = circuits.send(
                        pending.endpoint, pending.payload, true, http_now))
                    static_cast<void>(send_udp(viewer_server, pending.endpoint, *outgoing));
                if (!seed_served) {
                    std::cout << "{\"level\":\"warning\",\"message\":\"agent movement capability gate timed out\","
                                 "\"sessionId\":"
                              << homeworldz::api::json_string(pending.session_id)
                              << ",\"visitId\":"
                              << homeworldz::api::json_string(pending.visit_id) << "}" << std::endl;
                }
                return true;
            });
        fd_set readable;
        FD_ZERO(&readable);
        FD_SET(server, &readable);
        FD_SET(viewer_server, &readable);
        timeval timeout{0, 10000};
        const auto highest = server > viewer_server ? server : viewer_server;
        const auto ready = select(static_cast<int>(highest) + 1, &readable, nullptr, nullptr, &timeout);
        if (ready > 0 && FD_ISSET(server, &readable)) {
            const auto client = accept(server, nullptr, nullptr);
            if (client != invalid_socket) {
                bool response_deferred = false;
                const auto received_request = receive_http_request(client);
                if (received_request) {
                    const std::string_view request(*received_request);
                    auto response = homeworldz::http::response_for(request, region_version);
                    if (response.path == "/map/terrain.raw") {
                        const auto authorization =
                            homeworldz::http::request_header_value(request, "Authorization");
                        if (response.method != "GET") {
                            response = homeworldz::http::response_for_content(
                                request, 405, "application/json",
                                homeworldz::api::to_json(homeworldz::api::Error{
                                    "method_not_allowed", "terrain map endpoint requires GET"}));
                        } else if (service_token.empty() || authorization != "Bearer " + service_token) {
                            response = homeworldz::http::response_for_content(
                                request, 401, "application/json",
                                homeworldz::api::to_json(homeworldz::api::Error{
                                    "unauthorized", "a valid grid service token is required"}));
                        } else {
                            response = homeworldz::http::response_for_content(
                                request, 200, "application/vnd.homeworldz.heightmap-f32le",
                                encode_heightmap(*terrain_heightmap));
                        }
                    }
                    constexpr std::string_view arrival_prefix = "/api/v1/transits/";
                    constexpr std::string_view arrival_suffix = "/prepare-arrival";
                    if (response.path.starts_with(arrival_prefix) &&
                        response.path.ends_with(arrival_suffix)) {
                        const auto transit_id = response.path.substr(
                            arrival_prefix.size(), response.path.size() -
                            arrival_prefix.size() - arrival_suffix.size());
                        const auto authorization =
                            homeworldz::http::request_header_value(request, "Authorization");
                        if (response.method != "POST") {
                            response = homeworldz::http::response_for_content(
                                request, 405, "application/json",
                                homeworldz::api::to_json(homeworldz::api::Error{
                                    "method_not_allowed", "arrival preparation requires POST"}));
                        } else if (service_token.empty() || authorization != "Bearer " + service_token) {
                            response = homeworldz::http::response_for_content(
                                request, 401, "application/json",
                                homeworldz::api::to_json(homeworldz::api::Error{
                                    "unauthorized", "a valid grid service token is required"}));
                        } else if (!homeworldz::viewer::parse_uuid(transit_id) ||
                                   !viewer_grid || !registration) {
                            response = homeworldz::http::response_for_content(
                                request, 404, "application/json",
                                homeworldz::api::to_json(homeworldz::api::Error{
                                    "transit_not_found", "avatar transit was not found"}));
                        } else {
                            std::optional<homeworldz::grid::AvatarTransit> transit;
                            try {
                                transit = viewer_grid->find_avatar_transit(transit_id);
                                if (transit && transit->state == "prepared" &&
                                    transit->destination_region_id == registration->region_id())
                                    transit = viewer_grid->accept_avatar_transit(
                                        transit_id, registration->region_id());
                            } catch (const std::exception& error) {
                                std::cout << "{\"level\":\"error\",\"message\":\"arrival preparation failed\",\"error\":"
                                          << homeworldz::api::json_string(error.what()) << "}" << std::endl;
                            }
                            if (!transit || !inbound_transits.stage(
                                    *transit, registration->region_id(),
                                    std::chrono::steady_clock::now())) {
                                response = homeworldz::http::response_for_content(
                                    request, 409, "application/json",
                                    homeworldz::api::to_json(homeworldz::api::Error{
                                        "transit_not_preparable", "avatar transit could not be prepared"}));
                            } else {
                                response = homeworldz::http::response_for_content(
                                    request, 200, "application/json",
                                    homeworldz::api::to_json(homeworldz::api::Status{"accepted"}));
                            }
                        }
                    }
                    if (const auto asset_request = internal_asset_request(response.path)) {
                        const auto authorization = homeworldz::http::request_header_value(request, "Authorization");
                        const auto expected_method = asset_request->replicate ? "POST" : "GET";
                        if (response.method != expected_method) {
                            response = homeworldz::http::response_for_content(
                                request, 405, "application/json",
                                homeworldz::api::to_json(homeworldz::api::Error{
                                    "method_not_allowed", "asset endpoint method is invalid"}));
                        } else if (service_token.empty() || authorization != "Bearer " + service_token) {
                            response = homeworldz::http::response_for_content(
                                request, 401, "application/json",
                                homeworldz::api::to_json(homeworldz::api::Error{
                                    "unauthorized", "a valid grid service token is required"}));
                        } else if (asset_request->replicate) {
                            try {
                                const auto asset = read_federated_asset(asset_request->asset_id);
                                response = homeworldz::http::response_for_content(
                                    request, 200, "application/json",
                                    homeworldz::api::to_json(homeworldz::api::Status{"replicated"}));
                            } catch (const std::exception&) {
                                response = homeworldz::http::response_for_content(
                                    request, 404, "application/json",
                                    homeworldz::api::to_json(homeworldz::api::Error{
                                        "asset_not_found", "no verified asset source was available"}));
                            }
                        } else {
                            try {
                                const auto asset = storage->read_asset(asset_request->asset_id);
                                response = homeworldz::http::response_for_content(
                                    request, 200, "application/octet-stream",
                                    std::string(reinterpret_cast<const char*>(asset.data()), asset.size()));
                            } catch (const std::exception&) {
                                response = homeworldz::http::response_for_content(
                                    request, 404, "application/json",
                                    homeworldz::api::to_json(homeworldz::api::Error{
                                        "asset_not_found", "asset was not found"}));
                            }
                        }
                    }
                    constexpr std::string_view start_state_prefix = "/api/v1/agents/";
                    constexpr std::string_view start_state_suffix = "/start-state";
                    if (response.path.starts_with(start_state_prefix) &&
                        response.path.ends_with(start_state_suffix)) {
                        const auto user_id = response.path.substr(
                            start_state_prefix.size(), response.path.size() -
                            start_state_prefix.size() - start_state_suffix.size());
                        const auto authorization = homeworldz::http::request_header_value(request, "Authorization");
                        if (response.method != "GET") {
                            response = homeworldz::http::response_for_content(
                                request, 405, "application/json",
                                homeworldz::api::to_json(homeworldz::api::Error{
                                    "method_not_allowed", "start-state endpoint requires GET"}));
                        } else if (service_token.empty() || authorization != "Bearer " + service_token) {
                            response = homeworldz::http::response_for_content(
                                request, 401, "application/json",
                                homeworldz::api::to_json(homeworldz::api::Error{
                                    "unauthorized", "a valid grid service token is required"}));
                        } else if (!homeworldz::viewer::parse_uuid(user_id)) {
                            response = homeworldz::http::response_for_content(
                                request, 404, "application/json",
                                homeworldz::api::to_json(homeworldz::api::Error{
                                    "agent_not_found", "agent start state was not found"}));
                        } else {
                            const homeworldz::scene::Entity* agent{};
                            for (const auto& [candidate_id, candidate] : scene.entities())
                                if (candidate.name == user_id && (!agent || candidate_id > agent->id))
                                    agent = &candidate;
                            if (!agent) {
                                response = homeworldz::http::response_for_content(
                                    request, 404, "application/json",
                                    homeworldz::api::to_json(homeworldz::api::Error{
                                        "agent_not_found", "agent start state was not found"}));
                            } else {
                                const double qx = agent->rotation.x, qy = agent->rotation.y,
                                             qz = agent->rotation.z;
                                const auto qw = std::sqrt((std::max)(
                                    0.0, 1.0 - qx * qx - qy * qy - qz * qz));
                                const auto look_x = 1.0 - 2.0 * (qy * qy + qz * qz);
                                const auto look_y = 2.0 * (qx * qy + qw * qz);
                                const auto body = std::string{"{\"position\":["} +
                                    std::to_string(agent->position.x) + ',' +
                                    std::to_string(agent->position.y) + ',' +
                                    std::to_string(agent->position.z) + "],\"lookAt\":[" +
                                    std::to_string(look_x) + ',' + std::to_string(look_y) +
                                    ",0],\"flying\":" +
                                    (agent->avatar_flying ? "true}" : "false}");
                                response = homeworldz::http::response_for_content(
                                    request, 200, "application/json", body);
                            }
                        }
                    }
                    auto session_id = capability_session(response.path, "/caps/seed/");
                    const bool seed = !session_id.empty();
                    if (!seed) session_id = capability_session(response.path, "/caps/event/");
                    const bool event_queue = !seed && !session_id.empty();
                    const auto capability_visit_id = seed ?
                        capability_visit(response.path, "/caps/seed/") :
                        capability_visit(response.path, "/caps/event/");
                    const auto texture = texture_request(response.path);
                    if (texture) session_id = texture->first;
                    const auto viewer_asset = viewer_asset_request(response.path);
                    if (viewer_asset) session_id = viewer_asset->first;
                    std::string simulator_features_session;
                    if (!seed && !event_queue && !texture && !viewer_asset)
                        simulator_features_session =
                            capability_session(response.path, "/caps/simulator-features/");
                    const bool simulator_features = !simulator_features_session.empty();
                    if (simulator_features) session_id = simulator_features_session;
                    std::string environment_session;
                    if (!seed && !event_queue && !texture && !viewer_asset && !simulator_features)
                        environment_session = capability_session(response.path, "/caps/environment/");
                    const bool environment_settings = !environment_session.empty();
                    if (environment_settings) session_id = environment_session;
                    const auto baked_upload_session =
                        capability_session(response.path, "/caps/upload-baked/");
                    const bool baked_upload = !baked_upload_session.empty();
                    if (baked_upload) session_id = baked_upload_session;
                    const auto baked_upload_data = baked_upload_data_request(response.path);
                    if (baked_upload_data) session_id = baked_upload_data->first;
                    const auto file_upload_session =
                        capability_session(response.path, "/caps/upload-file/");
                    const bool file_upload = !file_upload_session.empty();
                    if (file_upload) session_id = file_upload_session;
                    const auto file_upload_data = file_upload_data_request(response.path);
                    if (file_upload_data) session_id = file_upload_data->first;
                    const auto notecard_update_session =
                        capability_session(response.path, "/caps/update-notecard/");
                    const bool notecard_update = !notecard_update_session.empty();
                    if (notecard_update) session_id = notecard_update_session;
                    const auto script_update_session =
                        capability_session(response.path, "/caps/update-script/");
                    const bool script_update = !script_update_session.empty();
                    if (script_update) session_id = script_update_session;
                    const auto gesture_update_session =
                        capability_session(response.path, "/caps/update-gesture/");
                    const bool gesture_update = !gesture_update_session.empty();
                    if (gesture_update) session_id = gesture_update_session;
                    const auto task_notecard_update_session =
                        capability_session(response.path, "/caps/update-task-notecard/");
                    const bool task_notecard_update = !task_notecard_update_session.empty();
                    if (task_notecard_update) session_id = task_notecard_update_session;
                    const auto task_script_update_session =
                        capability_session(response.path, "/caps/update-task-script/");
                    const bool task_script_update = !task_script_update_session.empty();
                    if (task_script_update) session_id = task_script_update_session;
                    const auto inventory_asset_update_data =
                        inventory_asset_update_data_request(response.path);
                    if (inventory_asset_update_data) session_id = inventory_asset_update_data->first;
                    if (seed || event_queue || texture || viewer_asset || simulator_features || environment_settings ||
                        baked_upload || baked_upload_data || file_upload || file_upload_data ||
                        notecard_update || script_update || gesture_update ||
                        task_notecard_update || task_script_update || inventory_asset_update_data) {
                        bool authorized = false;
                        std::string authorized_agent_id;
                        std::optional<homeworldz::grid::ViewerSession> authorized_session;
                        const auto expected_method =
                            texture || viewer_asset || simulator_features || environment_settings ? "GET" : "POST";
                        if (response.method == expected_method && registration && viewer_sessions) {
                            authorized_session = viewer_sessions->validate(session_id);
                            authorized = authorized_session &&
                                         authorized_session->destination_region_id == registration->region_id();
                            if (!authorized && authorized_session) {
                                const auto* transit = inbound_transits.authorize(
                                    authorized_session->agent_id, session_id,
                                    std::chrono::steady_clock::now());
                                authorized = transit &&
                                    transit->destination_region_id == registration->region_id();
                            }
                            if (authorized) authorized_agent_id = authorized_session->agent_id;
                        }
                        if (authorized && seed) {
                            response = homeworldz::http::response_for_content(
                                request, 200, "application/llsd+xml",
                                homeworldz::viewer::seed_capability_xml(
                                    region_public_endpoint, grid_public_endpoint, session_id,
                                    capability_visit_id));
                            if (!capability_visit_id.empty())
                                static_cast<void>(capability_arrival_gate.mark_seed_served(
                                    session_id, capability_visit_id));
                        } else if (authorized && event_queue) {
                            if (established_events.insert(session_id).second) {
                                if (authorized_session) {
                                    enqueue_viewer_event(session_id,
                                        homeworldz::viewer::establish_agent_communication_event_xml({
                                            authorized_session->agent_id,
                                            simulator_endpoint(region_public_endpoint, region_viewer_port),
                                            region_public_endpoint + "/caps/seed/" + session_id +
                                                (capability_visit_id.empty() ? std::string{} :
                                                    "/" + capability_visit_id)}));
                                }
                            }
                            const auto events = take_viewer_events(session_id);
                            if (!events.empty()) {
                                response = homeworldz::http::response_for_content(
                                    request, 200, "application/llsd+xml",
                                    homeworldz::viewer::event_queue_xml(++event_id, events));
                            } else {
                                pending_event_responses.push_back(PendingEventResponse{
                                    client, std::string(request), session_id,
                                    std::chrono::steady_clock::now() + std::chrono::seconds(20)});
                                response_deferred = true;
                            }
                        } else if (authorized && texture && homeworldz::viewer::parse_uuid(texture->second)) {
                            try {
                                const auto asset = read_federated_asset(texture->second);
                                response = homeworldz::http::response_for_content(
                                    request, 200, "image/x-j2c",
                                    std::string(reinterpret_cast<const char*>(asset.data()), asset.size()));
                            } catch (const std::exception&) {
                                response = homeworldz::http::response_for_content(
                                    request, 404, "application/json",
                                    homeworldz::api::to_json(homeworldz::api::Error{
                                        "asset_not_found", "texture asset was not found"}));
                            }
                        } else if (authorized && viewer_asset &&
                                   homeworldz::viewer::parse_uuid(viewer_asset->second)) {
                            try {
                                const auto asset = read_federated_asset(viewer_asset->second);
                                response = homeworldz::http::response_for_content(
                                    request, 200, "application/octet-stream",
                                    std::string(reinterpret_cast<const char*>(asset.data()), asset.size()));
                            } catch (const std::exception&) {
                                response = homeworldz::http::response_for_content(
                                    request, 404, "application/json",
                                    homeworldz::api::to_json(homeworldz::api::Error{
                                        "asset_not_found", "viewer asset was not found"}));
                            }
                        } else if (authorized && simulator_features) {
                            response = homeworldz::http::response_for_content(
                                request, 200, "application/llsd+xml",
                                homeworldz::viewer::simulator_features_xml(
                                    "C$", grid_public_endpoint + "/map/"));
                        } else if (authorized && environment_settings && registration) {
                            response = homeworldz::http::response_for_content(
                                request, 200, "application/llsd+xml",
                                homeworldz::viewer::environment_settings_xml(registration->region_id()));
                        } else if (authorized && baked_upload) {
                            static std::atomic<std::uint64_t> upload_id{0};
                            auto base = region_public_endpoint;
                            while (!base.empty() && base.back() == '/') base.pop_back();
                            const auto uploader = base + "/caps/upload-baked-data/" + session_id + '/' +
                                                  std::to_string(++upload_id);
                            response = homeworldz::http::response_for_content(
                                request, 200, "application/llsd+xml",
                                homeworldz::viewer::baked_texture_upload_xml(uploader));
                        } else if (authorized && baked_upload_data) {
                            const auto body = http_request_body(request);
                            if (body.empty()) {
                                response = homeworldz::http::response_for_content(
                                    request, 400, "application/llsd+xml", "<llsd><undef/></llsd>");
                            } else {
                                try {
                                    const auto content = std::span(
                                        reinterpret_cast<const std::byte*>(body.data()), body.size());
                                    const auto asset_id = homeworldz::viewer::random_uuid();
                                    const auto metadata = storage->store_asset(
                                        asset_id, authorized_agent_id, content);
                                    const bool registered = !viewer_grid || viewer_grid->register_asset(
                                        metadata.viewer_id, metadata.creator_id, metadata.sha256,
                                        metadata.size, region_public_endpoint, true);
                                    response = registered
                                        ? homeworldz::http::response_for_content(
                                              request, 200, "application/llsd+xml",
                                              homeworldz::viewer::baked_texture_complete_xml(asset_id))
                                        : homeworldz::http::response_for_content(
                                              request, 500, "application/llsd+xml", "<llsd><undef/></llsd>");
                                    std::cout << "{\"level\":\"info\",\"message\":\"baked texture stored\","
                                                 "\"assetId\":" << homeworldz::api::json_string(asset_id)
                                              << ",\"bytes\":" << body.size() << "}" << std::endl;
                                } catch (const std::exception& error) {
                                    response = homeworldz::http::response_for_content(
                                        request, 500, "application/llsd+xml", "<llsd><undef/></llsd>");
                                    std::cerr << "{\"level\":\"error\",\"message\":\"baked texture upload failed\","
                                                 "\"error\":" << homeworldz::api::json_string(error.what())
                                              << "}" << std::endl;
                                }
                            }
                        } else if (authorized && file_upload) {
                            const auto upload = homeworldz::viewer::parse_new_file_inventory_upload(
                                http_request_body(request));
                            if (!upload) {
                                response = homeworldz::http::response_for_content(
                                    request, 400, "application/llsd+xml", "<llsd><undef/></llsd>");
                            } else {
                                const auto token = homeworldz::viewer::random_uuid();
                                PendingInventoryUpload pending{session_id, authorized_agent_id,
                                    homeworldz::viewer::random_uuid(), homeworldz::viewer::random_uuid(), *upload};
                                pending_inventory_uploads.insert_or_assign(token, pending);
                                auto base = region_public_endpoint;
                                while (!base.empty() && base.back() == '/') base.pop_back();
                                const auto uploader = base + "/caps/upload-file-data/" + session_id + '/' + token;
                                response = homeworldz::http::response_for_content(
                                    request, 200, "application/llsd+xml",
                                    homeworldz::viewer::new_file_inventory_upload_xml(uploader));
                            }
                        } else if (authorized && file_upload_data) {
                            const auto pending = pending_inventory_uploads.find(file_upload_data->second);
                            const auto body = http_request_body(request);
                            if (pending == pending_inventory_uploads.end() ||
                                pending->second.session_id != session_id ||
                                pending->second.agent_id != authorized_agent_id) {
                                response = homeworldz::http::response_for_content(
                                    request, 404, "application/llsd+xml", "<llsd><undef/></llsd>");
                            } else if (!homeworldz::viewer::valid_new_file_inventory_upload_content(
                                           pending->second.request, body)) {
                                response = homeworldz::http::response_for_content(
                                    request, 400, "application/llsd+xml", "<llsd><undef/></llsd>");
                            } else {
                                const auto& upload = pending->second;
                                const auto content = std::span(
                                    reinterpret_cast<const std::byte*>(body.data()), body.size());
                                const auto metadata = storage->store_asset(
                                    upload.asset_id, authorized_agent_id, content);
                                const bool asset_registered = viewer_grid && viewer_grid->register_asset(
                                    metadata.viewer_id, metadata.creator_id, metadata.sha256,
                                    metadata.size, region_public_endpoint, true);
                                const bool item_created = asset_registered &&
                                    viewer_grid->create_inventory_item(
                                        authorized_agent_id, homeworldz::grid::InventoryItem{
                                            upload.item_id, authorized_agent_id, authorized_agent_id,
                                            upload.request.folder_id, upload.asset_id,
                                            upload.request.asset_type, upload.request.inventory_type,
                                            upload.request.name, upload.request.description, 0,
                                            homeworldz::scene::permission_creator,
                                            homeworldz::scene::permission_creator,
                                            upload.request.everyone_permissions,
                                            upload.request.next_permissions, 0, 0});
                                if (!item_created) {
                                    response = homeworldz::http::response_for_content(
                                        request, 500, "application/llsd+xml", "<llsd><undef/></llsd>");
                                } else {
                                    response = homeworldz::http::response_for_content(
                                        request, 200, "application/llsd+xml",
                                        homeworldz::viewer::new_file_inventory_complete_xml(
                                            upload.item_id, upload.asset_id,
                                            upload.request.everyone_permissions, upload.request.next_permissions));
                                    std::cout << "{\"level\":\"info\",\"message\":\"inventory asset upload stored\","
                                                 "\"assetId\":" << homeworldz::api::json_string(upload.asset_id)
                                              << ",\"itemId\":" << homeworldz::api::json_string(upload.item_id)
                                              << ",\"creatorId\":" << homeworldz::api::json_string(authorized_agent_id)
                                              << ",\"assetType\":" << static_cast<int>(upload.request.asset_type)
                                              << ",\"inventoryType\":" << static_cast<int>(upload.request.inventory_type)
                                              << ",\"bytes\":" << body.size() << "}" << std::endl;
                                    pending_inventory_uploads.erase(pending);
                                }
                            }
                        } else if (authorized && (notecard_update || script_update || gesture_update ||
                                                  task_notecard_update || task_script_update)) {
                            const auto update = homeworldz::viewer::parse_inventory_asset_update(
                                http_request_body(request));
                            const bool task_update = task_notecard_update || task_script_update;
                            const bool script_asset = script_update || task_script_update;
                            const std::int8_t expected_asset_type =
                                notecard_update || task_notecard_update ? 7 : script_asset ? 10 : 21;
                            const std::int8_t expected_inventory_type =
                                notecard_update || task_notecard_update ? 7 : script_asset ? 10 : 20;
                            const auto personal_item = update && !task_update && viewer_grid
                                ? viewer_grid->find_inventory_item(authorized_agent_id, update->item_id)
                                : std::nullopt;
                            const homeworldz::scene::TaskInventoryItem* task_item = nullptr;
                            const homeworldz::scene::Entity* task_entity = nullptr;
                            if (update && task_update) {
                                for (const auto& [id, candidate] : scene.entities()) {
                                    static_cast<void>(id);
                                    if (candidate.object_id == update->task_id) {
                                        task_entity = &candidate;
                                        break;
                                    }
                                }
                                if (task_entity && task_entity->owner_id == authorized_agent_id &&
                                    (task_entity->owner_permissions & homeworldz::scene::permission_modify) != 0) {
                                    const auto found = std::find_if(
                                        task_entity->task_inventory.begin(), task_entity->task_inventory.end(),
                                        [&](const auto& candidate) { return candidate.item_id == update->item_id; });
                                    if (found != task_entity->task_inventory.end()) task_item = &*found;
                                }
                            }
                            const bool valid_target = update &&
                                (script_asset ? !update->target.empty() : update->target.empty()) &&
                                (task_update ? !update->task_id.empty() : update->task_id.empty());
                            const auto valid_item = task_update ?
                                task_item && task_item->asset_type == expected_asset_type &&
                                    task_item->inventory_type == expected_inventory_type &&
                                    (task_item->current_permissions & homeworldz::scene::permission_modify) != 0 :
                                personal_item && personal_item->asset_type == expected_asset_type &&
                                    personal_item->inventory_type == expected_inventory_type &&
                                    (personal_item->current_permissions & homeworldz::scene::permission_modify) != 0;
                            if (!update || !valid_target || !valid_item) {
                                response = homeworldz::http::response_for_content(
                                    request, 400, "application/llsd+xml", "<llsd><undef/></llsd>");
                            } else {
                                const auto token = homeworldz::viewer::random_uuid();
                                pending_inventory_asset_updates.insert_or_assign(token,
                                    PendingInventoryAssetUpdate{session_id, authorized_agent_id,
                                        update->item_id, homeworldz::viewer::random_uuid(),
                                        expected_asset_type, expected_inventory_type,
                                        update->task_id, update->script_running});
                                auto base = region_public_endpoint;
                                while (!base.empty() && base.back() == '/') base.pop_back();
                                const auto uploader = base + "/caps/update-inventory-asset-data/" +
                                    session_id + '/' + token;
                                response = homeworldz::http::response_for_content(
                                    request, 200, "application/llsd+xml",
                                    homeworldz::viewer::inventory_asset_update_upload_xml(uploader));
                            }
                        } else if (authorized && inventory_asset_update_data) {
                            const auto pending = pending_inventory_asset_updates.find(
                                inventory_asset_update_data->second);
                            const auto body = http_request_body(request);
                            if (pending == pending_inventory_asset_updates.end() ||
                                pending->second.session_id != session_id ||
                                pending->second.agent_id != authorized_agent_id) {
                                response = homeworldz::http::response_for_content(
                                    request, 404, "application/llsd+xml", "<llsd><undef/></llsd>");
                            } else if (body.empty() || body.size() > 1024 * 1024) {
                                response = homeworldz::http::response_for_content(
                                    request, 400, "application/llsd+xml", "<llsd><undef/></llsd>");
                                pending_inventory_asset_updates.erase(pending);
                            } else {
                                const auto update = pending->second;
                                bool stored = false;
                                std::optional<homeworldz::script::FalconRezResult> compiled;
                                try {
                                    const auto content = std::span(
                                        reinterpret_cast<const std::byte*>(body.data()), body.size());
                                    const auto metadata = storage->store_asset(
                                        update.asset_id, authorized_agent_id, content);
                                    const bool registered = viewer_grid && viewer_grid->register_asset(
                                        metadata.viewer_id, metadata.creator_id, metadata.sha256,
                                        metadata.size, region_public_endpoint, true);
                                    if (registered && update.task_id.empty()) {
                                        stored = viewer_grid->update_inventory_item_asset(
                                            authorized_agent_id, update.item_id, update.asset_id);
                                    } else if (registered) {
                                        homeworldz::scene::Entity* task_entity = nullptr;
                                        for (const auto& [id, candidate] : scene.entities()) {
                                            if (candidate.object_id == update.task_id) {
                                                task_entity = scene.find(id);
                                                break;
                                            }
                                        }
                                        if (task_entity && task_entity->owner_id == authorized_agent_id &&
                                            (task_entity->owner_permissions &
                                                homeworldz::scene::permission_modify) != 0) {
                                            const auto item = std::find_if(
                                                task_entity->task_inventory.begin(),
                                                task_entity->task_inventory.end(),
                                                [&](const auto& candidate) {
                                                    return candidate.item_id == update.item_id &&
                                                        candidate.asset_type == update.asset_type &&
                                                        candidate.inventory_type == update.inventory_type &&
                                                        (candidate.current_permissions &
                                                            homeworldz::scene::permission_modify) != 0;
                                                });
                                            if (item != task_entity->task_inventory.end()) {
                                                const auto previous_asset_id = item->asset_id;
                                                const auto previous_serial = task_entity->task_inventory_serial;
                                                item->asset_id = update.asset_id;
                                                task_entity->task_inventory_serial = previous_serial == 65535
                                                    ? 1
                                                    : static_cast<std::uint16_t>(previous_serial + 1);
                                                try {
                                                    storage->save_snapshot(scene);
                                                    stored = true;
                                                } catch (...) {
                                                    item->asset_id = previous_asset_id;
                                                    task_entity->task_inventory_serial = previous_serial;
                                                    throw;
                                                }
                                            }
                                        }
                                    }
                                } catch (const std::exception& error) {
                                    std::cerr << "{\"level\":\"error\",\"message\":\"inventory asset update failed\",\"error\":"
                                              << homeworldz::api::json_string(error.what()) << "}" << std::endl;
                                }
                                if (stored && update.asset_type == 10 &&
                                    !update.task_id.empty()) {
                                    homeworldz::scene::Entity* task_entity = nullptr;
                                    for (const auto& [id, candidate] : scene.entities()) {
                                        if (candidate.object_id == update.task_id) {
                                            task_entity = scene.find(id);
                                            break;
                                        }
                                    }
                                    if (task_entity) {
                                        compiled = falcon.rez(
                                            {update.asset_id, update.item_id,
                                             task_entity->object_id, task_entity->owner_id},
                                            body, update.script_running);
                                        broadcast_object_update(
                                            *task_entity, std::chrono::steady_clock::now());
                                    }
                                }
                                response = stored
                                    ? homeworldz::http::response_for_content(
                                          request, 200, "application/llsd+xml",
                                          homeworldz::viewer::inventory_asset_update_complete_xml(
                                              update.asset_id, update.asset_type == 10,
                                              compiled && compiled->compiled,
                                              compiled ? compiled->diagnostic : std::string{}))
                                    : homeworldz::http::response_for_content(
                                          request, 500, "application/llsd+xml", "<llsd><undef/></llsd>");
                                std::cout << "{\"level\":" << (stored ? "\"info\"" : "\"warn\"")
                                          << ",\"message\":\"inventory asset update "
                                          << (stored ? "stored" : "rejected") << "\",\"itemId\":"
                                          << homeworldz::api::json_string(update.item_id)
                                          << ",\"assetId\":" << homeworldz::api::json_string(update.asset_id)
                                          << ",\"assetType\":" << static_cast<int>(update.asset_type)
                                          << ",\"taskId\":" << homeworldz::api::json_string(update.task_id)
                                          << ",\"compiled\":"
                                          << (compiled ? (compiled->compiled ? "true" : "false") : "null")
                                          << ",\"running\":"
                                          << (compiled ? (compiled->running ? "true" : "false") : "null")
                                          << ",\"diagnostic\":"
                                          << homeworldz::api::json_string(
                                                 compiled ? compiled->diagnostic : std::string{})
                                          << ",\"bytes\":" << body.size() << "}" << std::endl;
                                pending_inventory_asset_updates.erase(pending);
                            }
                        } else {
                            response = homeworldz::http::response_for_content(
                                request, response.method == expected_method ? 404 : 405,
                                "application/llsd+xml", "<llsd><undef/></llsd>");
                        }
                    }
                    if (!response_deferred) {
                        static_cast<void>(send_all(client, response.content));
                        finish_http_response(client);
                        std::cout << "{\"level\":\"info\",\"message\":\"http request\",\"requestId\":"
                                  << homeworldz::api::json_string(response.request_id)
                                  << ",\"method\":" << homeworldz::api::json_string(response.method)
                                  << ",\"path\":" << homeworldz::api::json_string(response.path)
                                  << ",\"status\":" << response.status_code << "}" << std::endl;
                    }
                }
                if (!response_deferred) close_socket(client);
            }
        }
        const auto now = std::chrono::steady_clock::now();
        if (ready > 0 && FD_ISSET(viewer_server, &readable)) {
            constexpr std::size_t max_viewer_packets_per_tick = 256;
            for (std::size_t packet_index = 0; packet_index < max_viewer_packets_per_tick; ++packet_index) {
                if (packet_index > 0) {
                    fd_set immediately_readable;
                    FD_ZERO(&immediately_readable);
                    FD_SET(viewer_server, &immediately_readable);
                    timeval no_wait{0, 0};
                    if (select(static_cast<int>(viewer_server) + 1, &immediately_readable,
                               nullptr, nullptr, &no_wait) <= 0)
                        break;
                }
                std::array<std::byte, 65535> datagram{};
                sockaddr_in sender{};
                socket_length sender_size = sizeof(sender);
                const auto received = recvfrom(viewer_server, reinterpret_cast<char*>(datagram.data()),
                                               static_cast<int>(datagram.size()), 0,
                                               reinterpret_cast<sockaddr*>(&sender), &sender_size);
                const auto endpoint = udp_endpoint(sender);
                if (received > 0 && !endpoint.empty()) {
                const auto packet = circuits.receive(
                     endpoint, std::span<const std::byte>(datagram.data(), static_cast<std::size_t>(received)), now);
                for (const auto& replaced : circuits.take_replaced()) {
                    // A newer login took over this account. The old circuit is
                    // already gone from the registry, so frame a standalone
                    // KickUser datagram (unreliable, one-shot) telling the old
                    // viewer why it is being disconnected.
                    homeworldz::viewer::Packet kick_packet;
                    kick_packet.payload = homeworldz::viewer::encode_kick_user(
                        replaced.identity.agent_id, replaced.identity.session_id,
                        "You have logged in from another location.");
                    if (!kick_packet.payload.empty())
                        static_cast<void>(send_udp(viewer_server, replaced.endpoint,
                            homeworldz::viewer::encode_packet(kick_packet)));
                    clear_viewer_endpoint(replaced.endpoint,
                        homeworldz::viewer::format_uuid(replaced.identity.session_id));
                    std::cout << "{\"level\":\"info\",\"message\":\"stale viewer circuit replaced\",\"endpoint\":"
                              << homeworldz::api::json_string(replaced.endpoint) << "}" << std::endl;
                }
                if (packet) {
                    if (homeworldz::viewer::decode_complete_ping_check(packet->payload)) {
                        // Pong: the viewer answered our StartPingCheck, so the
                        // connection is alive. (Liveness is tracked by ping
                        // replies, not general activity — an idle-but-connected
                        // viewer still answers pings.)
                        if (auto live = avatars.find(endpoint); live != avatars.end())
                            live->second.last_pong = now;
                    }
                    const auto identity = circuits.identity(endpoint);
                    if (identity && homeworldz::viewer::decode_use_circuit_code(packet->payload)) {
                        handshake_replies.erase(endpoint);
                        const auto region_id = registration ?
                            homeworldz::viewer::parse_uuid(registration->region_id()) : std::nullopt;
                        if (region_id) {
                            homeworldz::viewer::RegionHandshake handshake;
                            handshake.name = region_name;
                            handshake.region_id = *region_id;
                            handshake.owner_id = identity->agent_id;
                            constexpr std::array<std::string_view, 4> terrain_texture_ids{
                                "b8d3965a-ad78-bf43-699b-bff8eca6c975",
                                "abb783e6-3e93-26c0-248a-247666855da3",
                                "179cdabd-398a-9b6b-1391-4dc333ba321f",
                                "beb169c7-11ea-fff2-efe5-0f24dc881df2"};
                            for (std::size_t index = 0; index < terrain_texture_ids.size(); ++index)
                                if (const auto texture = homeworldz::viewer::parse_uuid(terrain_texture_ids[index]))
                                    handshake.terrain_textures[index] = *texture;
                            if (const auto response = circuits.send(endpoint,
                                    homeworldz::viewer::encode_region_handshake(handshake), true, now, true)) {
                                const auto sent = send_udp(viewer_server, endpoint, *response);
                                std::cout << "{\"level\":" << (sent ? "\"info\"" : "\"error\"")
                                          << ",\"message\":\"region handshake sent\",\"endpoint\":"
                                          << homeworldz::api::json_string(endpoint)
                                          << ",\"bytes\":" << response->size()
                                          << ",\"success\":" << (sent ? "true" : "false");
#ifdef _WIN32
                                if (!sent) std::cout << ",\"socketError\":" << WSAGetLastError();
#endif
                                std::cout << "}" << std::endl;
                            } else {
                                std::cout << "{\"level\":\"error\",\"message\":\"region handshake not queued\","
                                             "\"endpoint\":"
                                          << homeworldz::api::json_string(endpoint) << "}" << std::endl;
                            }
                        }
                    } else if (identity) {
                        if (const auto ping_id = homeworldz::viewer::decode_start_ping_check(packet->payload)) {
                            if (const auto pong = circuits.send(endpoint,
                                    homeworldz::viewer::encode_complete_ping_check(*ping_id), false, now))
                                static_cast<void>(send_udp(viewer_server, endpoint, *pong));
                        }
                        if (homeworldz::viewer::is_economy_data_request(packet->payload)) {
                            if (const auto economy = circuits.send(endpoint,
                                    homeworldz::viewer::encode_economy_data(), true, now, true))
                                static_cast<void>(send_udp(viewer_server, endpoint, *economy));
                        }
                        const auto available_map_regions = [&] {
                            std::vector<homeworldz::viewer::MapBlock> regions;
                            const auto map_image_id = homeworldz::viewer::parse_uuid(
                                default_map_tile_asset_id).value_or(homeworldz::viewer::Uuid{});
                            if (region_grid_x >= 0 && region_grid_x <= 65535 &&
                                region_grid_y >= 0 && region_grid_y <= 65535) {
                                regions.push_back(homeworldz::viewer::MapBlock{
                                    static_cast<std::uint16_t>(region_grid_x),
                                    static_cast<std::uint16_t>(region_grid_y), region_name,
                                    13, 0, 20, static_cast<std::uint8_t>((std::min)(avatars.size(), std::size_t{255})),
                                    map_image_id, static_cast<std::uint16_t>(region_size_x),
                                    static_cast<std::uint16_t>(region_size_y)});
                            }
                            for (const auto& neighbor : region_neighbors) {
								if (!neighbor.online) continue;
                                if (neighbor.grid_x < 0 || neighbor.grid_x > 65535 ||
                                    neighbor.grid_y < 0 || neighbor.grid_y > 65535) continue;
                                regions.push_back(homeworldz::viewer::MapBlock{
                                    static_cast<std::uint16_t>(neighbor.grid_x),
                                    static_cast<std::uint16_t>(neighbor.grid_y), neighbor.name,
                                    13, 0, 20, 0, map_image_id,
                                    static_cast<std::uint16_t>(neighbor.size_x),
                                    static_cast<std::uint16_t>(neighbor.size_y)});
                            }
                            return regions;
                        };
                        if (const auto request =
                                homeworldz::viewer::decode_map_block_request(packet->payload);
                            request && request->agent_id == identity->agent_id &&
                            request->session_id == identity->session_id) {
                            auto regions = available_map_regions();
                            std::erase_if(regions, [&](const auto& region) {
                                return region.x < request->min_x || region.x > request->max_x ||
                                       region.y < request->min_y || region.y > request->max_y;
                            });
                            auto response = homeworldz::viewer::encode_map_block_reply(
                                identity->agent_id, request->flags, regions);
                            if (!response.empty())
                                if (const auto outgoing = circuits.send(
                                        endpoint, std::move(response), true, now))
                                    static_cast<void>(send_udp(viewer_server, endpoint, *outgoing));
                        }
                        if (const auto request =
                                homeworldz::viewer::decode_map_name_request(packet->payload);
                            request && request->agent_id == identity->agent_id &&
                            request->session_id == identity->session_id) {
                            auto lowercase = [](std::string value) {
                                std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
                                    return static_cast<char>(std::tolower(character));
                                });
                                return value;
                            };
                            const auto prefix = lowercase(request->name);
                            auto regions = available_map_regions();
                            std::erase_if(regions, [&](const auto& region) {
                                return !lowercase(region.name).starts_with(prefix);
                            });
                            auto response = homeworldz::viewer::encode_map_block_reply(
                                identity->agent_id, request->flags, regions);
                            if (!response.empty())
                                if (const auto outgoing = circuits.send(
                                        endpoint, std::move(response), true, now))
                                    static_cast<void>(send_udp(viewer_server, endpoint, *outgoing));
                        }
                        if (const auto requested_names =
                                homeworldz::viewer::decode_uuid_name_request(packet->payload)) {
                            std::vector<homeworldz::viewer::UuidName> names;
                            names.reserve(requested_names->size());
                            for (const auto& requested_id : *requested_names) {
                                const auto user_id = homeworldz::viewer::format_uuid(requested_id);
                                if (const auto found = resolved_avatar_names.find(user_id);
                                    found != resolved_avatar_names.end()) {
                                    names.push_back(found->second);
                                    continue;
                                }
                                try {
                                    const auto user = viewer_grid ? viewer_grid->find_user(user_id) : std::nullopt;
                                    if (!user) continue;
                                    auto [first, last] = legacy_avatar_name(user->username);
                                    homeworldz::viewer::UuidName name{
                                        requested_id, std::move(first), std::move(last)};
                                    resolved_avatar_names.emplace(user_id, name);
                                    names.push_back(std::move(name));
                                } catch (const std::exception& error) {
                                    std::cout << "{\"level\":\"error\",\"message\":\"avatar name lookup failed\",\"error\":"
                                              << homeworldz::api::json_string(error.what()) << "}" << std::endl;
                                }
                            }
                            auto response = homeworldz::viewer::encode_uuid_name_reply(names);
                            if (!response.empty()) {
                                if (const auto outgoing = circuits.send(
                                        endpoint, std::move(response), true, now))
                                    static_cast<void>(send_udp(viewer_server, endpoint, *outgoing));
                            }
                        }
                        const auto perform_teleport = [&](std::uint64_t destination_handle,
                                                          const std::array<float, 3>& destination_position) {
                            const auto current_position =
                                homeworldz::region::resolve_region_teleport_position(
                                    region_grid_x, region_grid_y, region_size_x, region_size_y,
                                    destination_handle, destination_position);
                            const homeworldz::grid::RegionNeighbor* target = nullptr;
                            std::optional<std::array<float, 3>> target_position;
                            for (const auto& neighbor : region_neighbors) {
                                auto resolved = homeworldz::region::resolve_region_teleport_position(
                                    neighbor.grid_x, neighbor.grid_y, neighbor.size_x, neighbor.size_y,
                                    destination_handle, destination_position);
                                if (!resolved) continue;
                                target = &neighbor;
                                target_position = resolved;
                                break;
                            }
                            const auto fail_teleport = [&](std::string reason) {
                                if (const auto failed = circuits.send(endpoint,
                                        homeworldz::viewer::encode_teleport_failed(
                                            {identity->agent_id, std::move(reason)}), true, now, true))
                                    static_cast<void>(send_udp(viewer_server, endpoint, *failed));
                            };
                            if (current_position) {
                                const auto avatar = avatars.find(endpoint);
                                const homeworldz::scene::Vector3 requested_position{
                                    (*current_position)[0], (*current_position)[1], (*current_position)[2]};
                                if (avatar == avatars.end() || requested_position.x < 0.0 ||
                                    requested_position.x > region_size_x || requested_position.y < 0.0 ||
                                    requested_position.y > region_size_y) {
                                    fail_teleport("Destination position is unavailable");
                                } else {
                                    const auto flying = avatar->second.controller.state().flying;
                                    avatar->second.controller.set_ground_height(
                                        collision_ground_height(requested_position));
                                    avatar->second.controller.teleport(requested_position, flying);
                                    if (physics_world && avatar->second.physics_character != 0) {
                                        if (auto state = physics_world->character_state(
                                                avatar->second.physics_character)) {
                                            state->position = avatar->second.controller.state().position;
                                            state->linear_velocity = {};
                                            state->grounded = avatar->second.controller.state().grounded;
                                            physics_world->set_character_state(
                                                avatar->second.physics_character, *state);
                                            physics_world->set_character_flying(
                                                avatar->second.physics_character, flying);
                                        }
                                    }
                                    const auto position = avatar->second.controller.viewer_position();
                                    const auto look_direction = avatar->second.controller.look_direction();
                                    const auto flags = homeworldz::viewer::teleport_flags_via_location |
                                        (flying ? homeworldz::viewer::teleport_flags_is_flying : 0U);
                                    if (const auto local = circuits.send(endpoint,
                                            homeworldz::viewer::encode_teleport_local({
                                                identity->agent_id, 2,
                                                {static_cast<float>(position.x),
                                                 static_cast<float>(position.y),
                                                 static_cast<float>(position.z)},
                                                look_direction, flags}), true, now, true))
                                        static_cast<void>(send_udp(viewer_server, endpoint, *local));
                                    std::cout << "{\"level\":\"info\",\"message\":\"avatar local teleport completed\","
                                                 "\"position\":[" << position.x << ',' << position.y << ','
                                              << position.z << "]}" << std::endl;
                                }
                            } else if (!target || !target_position || !target->online ||
								!viewer_grid || !registration) {
                                fail_teleport("Destination region is unavailable");
                            } else if (const auto simulator = simulator_event_endpoint(
                                           target->public_endpoint, target->viewer_port)) {
                                const auto session_id = homeworldz::viewer::format_uuid(identity->session_id);
                                const auto agent_id = homeworldz::viewer::format_uuid(identity->agent_id);
                                const auto transit_id = homeworldz::viewer::random_uuid();
                                const auto avatar = avatars.find(endpoint);
                                const bool flying = avatar != avatars.end() &&
                                    avatar->second.controller.state().flying;
                                const auto teleport_flags =
                                    homeworldz::viewer::teleport_flags_via_location |
                                    (flying ? homeworldz::viewer::teleport_flags_is_flying : 0U);
                                bool prepared = false;
                                try {
                                    if (const auto start = circuits.send(endpoint,
                                            homeworldz::viewer::encode_teleport_start({teleport_flags}),
                                            true, now, true))
                                        static_cast<void>(send_udp(viewer_server, endpoint, *start));
                                    // TeleportLocationRequest.LookAt is an absolute destination
                                    // point, not a direction. Preserve the live avatar's facing
                                    // across the handoff instead of interpreting that point as a
                                    // quaternion direction at the destination.
                                    const auto look_direction = avatar != avatars.end() ?
                                        avatar->second.controller.look_direction() :
                                        std::array<float, 3>{1.0F, 0.0F, 0.0F};
                                    const homeworldz::grid::AvatarTransitRequest transit_request{
                                        transit_id, agent_id, session_id, registration->region_id(),
                                        target->id, *target_position, look_direction, flying, 30};
                                    const auto transit = viewer_grid->prepare_avatar_transit(transit_request);
                                    prepared = transit && transit->state == "prepared";
                                    if (!prepared) throw std::runtime_error("grid rejected transit preparation");
                                    auto destination = homeworldz::grid::socket_transport(
                                        target->public_endpoint, service_token);
                                    if (!homeworldz::grid::prepare_avatar_arrival(*destination, transit_id))
                                        throw std::runtime_error("destination rejected transit preparation");
                                    const auto target_handle =
                                        (static_cast<std::uint64_t>(target->grid_x * 256) << 32) |
                                        static_cast<std::uint32_t>(target->grid_y * 256);
                                    enqueue_viewer_event(session_id,
                                        homeworldz::viewer::enable_simulator_event_xml(
                                            target_handle, *simulator,
                                            static_cast<std::uint32_t>(target->size_x),
                                            static_cast<std::uint32_t>(target->size_y)));
                                    enqueue_viewer_event(session_id,
                                        homeworldz::viewer::teleport_finish_event_xml({
                                            agent_id, target_handle, *simulator,
                                            target->public_endpoint + "/caps/seed/" + session_id +
                                                "/" + transit_id, 13, teleport_flags,
                                            static_cast<std::uint32_t>(target->size_x),
                                            static_cast<std::uint32_t>(target->size_y)}));
                                    std::cout << "{\"level\":\"info\",\"message\":\"avatar teleport signaled\",\"transitId\":"
                                              << homeworldz::api::json_string(transit_id)
                                              << ",\"destinationRegionId\":"
                                              << homeworldz::api::json_string(target->id) << "}" << std::endl;
                                } catch (const std::exception& error) {
                                    if (prepared) static_cast<void>(viewer_grid->rollback_avatar_transit(
                                        transit_id, registration->region_id(), error.what()));
                                    fail_teleport("Destination region could not prepare the arrival");
                                    std::cout << "{\"level\":\"error\",\"message\":\"avatar teleport preparation failed\",\"error\":"
                                              << homeworldz::api::json_string(error.what()) << "}" << std::endl;
                                }
                            } else {
                                fail_teleport("Destination viewer address could not be resolved");
                            }
                        };
                        if (const auto teleport =
                                homeworldz::viewer::decode_teleport_location_request(packet->payload);
                            teleport && teleport->agent_id == identity->agent_id &&
                            teleport->session_id == identity->session_id) {
                            perform_teleport(teleport->region_handle, teleport->position);
                        }
                        if (const auto landmark_tp =
                                homeworldz::viewer::decode_teleport_landmark_request(packet->payload);
                            landmark_tp && landmark_tp->agent_id == identity->agent_id &&
                            landmark_tp->session_id == identity->session_id) {
                            const auto fail_landmark = [&](std::string reason) {
                                if (const auto failed = circuits.send(endpoint,
                                        homeworldz::viewer::encode_teleport_failed(
                                            {identity->agent_id, std::move(reason)}), true, now, true))
                                    static_cast<void>(send_udp(viewer_server, endpoint, *failed));
                            };
                            if (landmark_tp->landmark_id == homeworldz::viewer::Uuid{}) {
                                // Null landmark = Teleport Home; wired in the Home step.
                                fail_landmark("Home location is not set");
                            } else {
                                // Landmark asset: "Landmark version 2\nregion_id <uuid>\nlocal_pos x y z".
                                std::optional<std::uint64_t> destination_handle;
                                std::array<float, 3> local_pos{};
                                try {
                                    const auto bytes = read_federated_asset(
                                        homeworldz::viewer::format_uuid(landmark_tp->landmark_id));
                                    const std::string text(
                                        reinterpret_cast<const char*>(bytes.data()), bytes.size());
                                    const auto region_key = text.find("region_id ");
                                    const auto pos_key = text.find("local_pos ");
                                    if (region_key != std::string::npos && pos_key != std::string::npos &&
                                        region_key + 10 + 36 <= text.size()) {
                                        const auto region_id = text.substr(region_key + 10, 36);
                                        std::istringstream coords(text.substr(pos_key + 10));
                                        coords >> local_pos[0] >> local_pos[1] >> local_pos[2];
                                        if (registration && region_id == registration->region_id()) {
                                            destination_handle =
                                                (static_cast<std::uint64_t>(region_grid_x * 256) << 32) |
                                                static_cast<std::uint32_t>(region_grid_y * 256);
                                        } else {
                                            for (const auto& neighbor : region_neighbors) {
                                                if (neighbor.id != region_id) continue;
                                                destination_handle =
                                                    (static_cast<std::uint64_t>(neighbor.grid_x * 256) << 32) |
                                                    static_cast<std::uint32_t>(neighbor.grid_y * 256);
                                                break;
                                            }
                                        }
                                    }
                                } catch (const std::exception&) {
                                }
                                if (destination_handle) perform_teleport(*destination_handle, local_pos);
                                else fail_landmark("Landmark destination is unavailable");
                            }
                        }
                        const auto logout = homeworldz::viewer::decode_logout_request(packet->payload);
                        if (logout && logout->agent_id == identity->agent_id &&
                            logout->session_id == identity->session_id) {
                            homeworldz::viewer::AgentMessage reply{identity->agent_id, identity->session_id};
                            if (const auto outgoing = circuits.send(endpoint,
                                    homeworldz::viewer::encode_logout_reply(reply), true, now, true))
                                static_cast<void>(send_udp(viewer_server, endpoint, *outgoing));
                            const auto session_id = homeworldz::viewer::format_uuid(identity->session_id);
                            const auto user_id = homeworldz::viewer::format_uuid(identity->agent_id);
                            if (viewer_grid) {
                                if (const auto live = avatars.find(endpoint);
                                    live != avatars.end() && registration) {
                                    const auto& state = live->second.controller.state();
                                    const std::array<float, 3> position{
                                        static_cast<float>(state.position.x),
                                        static_cast<float>(state.position.y),
                                        static_cast<float>(state.position.z)};
                                    try {
                                        if (!viewer_grid->update_last_location(
                                                user_id, registration->region_id(), position,
                                                live->second.controller.look_direction(), state.flying))
                                            std::cout << "{\"level\":\"warn\",\"message\":\"last location update rejected during logout\",\"userId\":"
                                                      << homeworldz::api::json_string(user_id) << "}" << std::endl;
                                    } catch (const std::exception& error) {
                                        std::cout << "{\"level\":\"warn\",\"message\":\"last location update failed during logout\",\"error\":"
                                                  << homeworldz::api::json_string(error.what()) << "}" << std::endl;
                                    }
                                }
                                static_cast<void>(viewer_grid->clear_presence(user_id));
                                static_cast<void>(viewer_grid->revoke_viewer_session(session_id));
                            }
                            if (viewer_sessions) viewer_sessions->invalidate(session_id);
                            clear_viewer_endpoint(endpoint, session_id);
                            circuits.remove(endpoint);
                            std::cout << "{\"level\":\"info\",\"message\":\"viewer logged out\",\"sessionId\":"
                                      << homeworldz::api::json_string(session_id) << "}" << std::endl;
                            continue;
                        }
                        const auto task_inventory_request =
                            homeworldz::viewer::decode_request_task_inventory(packet->payload);
                        if (task_inventory_request &&
                            task_inventory_request->agent_id == identity->agent_id &&
                            task_inventory_request->session_id == identity->session_id) {
                            const auto* entity = scene.find(task_inventory_request->local_id);
                            const auto agent_id = homeworldz::viewer::format_uuid(identity->agent_id);
                            bool sent = false;
                            if (entity && entity->owner_id == agent_id) {
                                const auto task_id = homeworldz::viewer::parse_uuid(entity->object_id);
                                if (task_id) {
                                    std::string filename;
                                    std::int16_t serial{};
                                    if (entity->task_inventory_serial != 0) {
                                        filename = "inventory_" + homeworldz::viewer::random_uuid() + ".tmp";
                                        const auto content = task_inventory_file(*entity);
                                        pending_task_inventory_files.insert_or_assign(
                                            endpoint + '|' + filename, content);
                                        serial = static_cast<std::int16_t>(entity->task_inventory_serial);
                                    }
                                    auto wire_filename = filename;
                                    if (!wire_filename.empty()) wire_filename.push_back('\0');
                                    const auto payload = homeworldz::viewer::encode_reply_task_inventory(
                                        {*task_id, serial, wire_filename});
                                    if (!payload.empty()) {
                                        if (const auto outgoing = circuits.send(
                                                endpoint, payload, true, now, true))
                                            sent = send_udp(viewer_server, endpoint, *outgoing);
                                    }
                                }
                            }
                            std::cout << "{\"level\":" << (sent ? "\"info\"" : "\"warn\"")
                                      << ",\"message\":\"task inventory reply "
                                      << (sent ? "sent" : "rejected") << "\",\"localId\":"
                                      << task_inventory_request->local_id << ",\"items\":"
                                      << (entity ? entity->task_inventory.size() : 0) << "}" << std::endl;
                        }
                        const auto rez_script =
                            homeworldz::viewer::decode_rez_script(packet->payload);
                        if (rez_script && rez_script->agent_id == identity->agent_id &&
                            rez_script->session_id == identity->session_id) {
                            auto* entity = scene.find(rez_script->local_id);
                            const auto agent_id = homeworldz::viewer::format_uuid(identity->agent_id);
                            const auto source_id = homeworldz::viewer::format_uuid(rez_script->item_id);
                            bool changed = false;
                            std::string operation{"copy"};
                            std::string task_item_id;
                            std::optional<homeworldz::script::FalconRezResult> compiled;
                            try {
                                if (entity && entity->owner_id == agent_id &&
                                    (entity->owner_permissions &
                                     homeworldz::scene::permission_modify) != 0 &&
                                    rez_script->asset_type == 10 &&
                                    rez_script->inventory_type == 10) {
                                    const auto previous_serial = entity->task_inventory_serial;
                                    const auto source = viewer_grid &&
                                            rez_script->item_id != homeworldz::viewer::Uuid{}
                                        ? viewer_grid->find_inventory_item(agent_id, source_id)
                                        : std::nullopt;
                                    if (source && source->asset_type == 10 &&
                                        source->inventory_type == 10 &&
                                        (source->current_permissions &
                                         homeworldz::scene::permission_copy) != 0) {
                                        operation = "copy";
                                        task_item_id = homeworldz::viewer::random_uuid();
                                        const auto created = static_cast<std::uint64_t>(
                                            std::chrono::duration_cast<std::chrono::seconds>(
                                                std::chrono::system_clock::now()
                                                    .time_since_epoch())
                                                .count());
                                        entity->task_inventory.push_back({
                                            task_item_id, source->asset_id, source->creator_id,
                                            agent_id, source->owner_id,
                                            "00000000-0000-0000-0000-000000000000",
                                            source->name, source->description,
                                            static_cast<std::int8_t>(source->asset_type),
                                            static_cast<std::int8_t>(source->inventory_type),
                                            source->flags, source->base_permissions,
                                            source->current_permissions, 0,
                                            source->everyone_permissions, source->next_permissions,
                                            static_cast<std::uint8_t>(source->sale_type),
                                            source->sale_price, created});
                                        entity->task_inventory_serial = previous_serial == 65535
                                            ? 1
                                            : static_cast<std::uint16_t>(previous_serial + 1);
                                        try {
                                            if (storage) storage->save_snapshot(scene);
                                            changed = true;
                                        } catch (...) {
                                            entity->task_inventory.pop_back();
                                            entity->task_inventory_serial = previous_serial;
                                            throw;
                                        }
                                    } else if (source && viewer_grid) {
                                        operation = "transfer";
                                        task_item_id = homeworldz::viewer::random_uuid();
                                        const auto transfer = viewer_grid->prepare_task_inventory_transfer({
                                            homeworldz::viewer::random_uuid(), agent_id, source_id,
                                            provisioned_region_id, entity->object_id, task_item_id});
                                        if (transfer && transfer->state == "prepared") {
                                            const auto finalized =
                                                apply_task_inventory_transfer(*transfer);
                                            changed = std::any_of(
                                                entity->task_inventory.begin(),
                                                entity->task_inventory.end(),
                                                [&](const auto& item) {
                                                    return item.item_id == task_item_id;
                                                });
                                            if (changed && !finalized)
                                                std::cerr << "{\"level\":\"warning\",\"message\":\"script transfer awaits reconciliation\",\"transferId\":"
                                                          << homeworldz::api::json_string(transfer->id)
                                                          << "}" << std::endl;
                                        }
                                    } else if (rez_script->item_id == homeworldz::viewer::Uuid{} &&
                                               rez_script->transaction_id ==
                                                   homeworldz::viewer::Uuid{} &&
                                               viewer_grid && storage) {
                                        operation = "create";
                                        const auto initial =
                                            homeworldz::inventory::default_asset_content(
                                                10, 10, provisioned_region_id, entity->position);
                                        if (initial) {
                                            auto asset_id = homeworldz::viewer::random_uuid();
                                            const auto content = std::span(
                                                reinterpret_cast<const std::byte*>(initial->data()),
                                                initial->size());
                                            const auto metadata =
                                                storage->store_asset(asset_id, agent_id, content);
                                            if (!viewer_grid->register_asset(
                                                    metadata.viewer_id, metadata.creator_id,
                                                    metadata.sha256, metadata.size,
                                                    region_public_endpoint, true))
                                                asset_id.clear();
                                            if (!asset_id.empty()) {
                                                task_item_id = homeworldz::viewer::random_uuid();
                                                const auto created = static_cast<std::uint64_t>(
                                                    std::chrono::duration_cast<std::chrono::seconds>(
                                                        std::chrono::system_clock::now()
                                                            .time_since_epoch())
                                                        .count());
                                                entity->task_inventory.push_back({
                                                    task_item_id, asset_id, agent_id, agent_id,
                                                    agent_id,
                                                    "00000000-0000-0000-0000-000000000000",
                                                    rez_script->name, rez_script->description, 10, 10,
                                                    rez_script->flags,
                                                    rez_script->base_permissions,
                                                    rez_script->owner_permissions,
                                                    rez_script->group_permissions,
                                                    rez_script->everyone_permissions,
                                                    rez_script->next_owner_permissions,
                                                    rez_script->sale_type,
                                                    rez_script->sale_price, created});
                                                entity->task_inventory_serial =
                                                    previous_serial == 65535
                                                        ? 1
                                                        : static_cast<std::uint16_t>(
                                                              previous_serial + 1);
                                                try {
                                                    storage->save_snapshot(scene);
                                                    changed = true;
                                                } catch (...) {
                                                    entity->task_inventory.pop_back();
                                                    entity->task_inventory_serial = previous_serial;
                                                    throw;
                                                }
                                            }
                                        }
                                    }
                                }
                            } catch (const std::exception& error) {
                                std::cout << "{\"level\":\"error\",\"message\":\"rez script failed\",\"error\":"
                                          << homeworldz::api::json_string(error.what()) << "}"
                                          << std::endl;
                            }
                            if (changed && entity) {
                                const auto item = std::find_if(
                                    entity->task_inventory.begin(),
                                    entity->task_inventory.end(),
                                    [&](const auto& candidate) {
                                        return candidate.item_id == task_item_id;
                                    });
                                if (item != entity->task_inventory.end())
                                    compiled = rez_task_script(
                                        *entity, *item, rez_script->enabled);
                                broadcast_object_update(
                                    *entity, std::chrono::steady_clock::now());
                            }
                            bool refresh_sent = false;
                            if (changed && entity) {
                                const auto task_id =
                                    homeworldz::viewer::parse_uuid(entity->object_id);
                                if (task_id) {
                                    const auto filename = "inventory_" +
                                        homeworldz::viewer::random_uuid() + ".tmp";
                                    pending_task_inventory_files.insert_or_assign(
                                        endpoint + '|' + filename,
                                        task_inventory_file(*entity));
                                    auto wire_filename = filename;
                                    wire_filename.push_back('\0');
                                    const auto payload =
                                        homeworldz::viewer::encode_reply_task_inventory({
                                            *task_id,
                                            static_cast<std::int16_t>(
                                                entity->task_inventory_serial),
                                            wire_filename});
                                    if (const auto outgoing = circuits.send(
                                            endpoint, payload, true, now, true))
                                        refresh_sent =
                                            send_udp(viewer_server, endpoint, *outgoing);
                                }
                            }
                            std::cout << "{\"level\":"
                                      << (changed ? "\"info\"" : "\"warn\"")
                                      << ",\"message\":\"rez script " << operation << ' '
                                      << (changed ? "completed" : "rejected")
                                      << "\",\"localId\":" << rez_script->local_id
                                      << ",\"itemId\":"
                                      << homeworldz::api::json_string(task_item_id)
                                      << ",\"enabled\":"
                                      << (rez_script->enabled ? "true" : "false")
                                      << ",\"compiled\":"
                                      << (compiled ? (compiled->compiled ? "true" : "false") : "null")
                                      << ",\"running\":"
                                      << (compiled ? (compiled->running ? "true" : "false") : "null")
                                      << ",\"diagnostic\":"
                                      << homeworldz::api::json_string(
                                             compiled ? compiled->diagnostic : std::string{})
                                      << ",\"refreshSent\":"
                                      << (refresh_sent ? "true" : "false") << "}"
                                      << std::endl;
                        }
                        const auto task_inventory_update =
                            homeworldz::viewer::decode_update_task_inventory(packet->payload);
                        if (task_inventory_update &&
                            task_inventory_update->agent_id == identity->agent_id &&
                            task_inventory_update->session_id == identity->session_id) {
                            auto* entity = scene.find(task_inventory_update->local_id);
                            const auto agent_id = homeworldz::viewer::format_uuid(identity->agent_id);
                            const auto source_id = homeworldz::viewer::format_uuid(task_inventory_update->item_id);
                            bool changed = false;
                            std::string operation{"copy"};
                            try {
                                if (entity && entity->owner_id == agent_id &&
                                    (entity->owner_permissions & homeworldz::scene::permission_modify) != 0) {
                                    const auto previous_serial = entity->task_inventory_serial;
                                    const auto existing = std::find_if(
                                        entity->task_inventory.begin(), entity->task_inventory.end(),
                                        [&](const auto& item) { return item.item_id == source_id; });
                                    if (existing != entity->task_inventory.end()) {
                                        operation = "update";
                                        const auto original = *existing;
                                        if (homeworldz::scene::apply_task_inventory_update(
                                                *existing, task_inventory_update->name,
                                                task_inventory_update->description,
                                                task_inventory_update->flags,
                                                task_inventory_update->owner_permissions,
                                                task_inventory_update->group_permissions,
                                                task_inventory_update->everyone_permissions,
                                                task_inventory_update->next_owner_permissions,
                                                task_inventory_update->sale_type,
                                                task_inventory_update->sale_price)) {
                                            entity->task_inventory_serial = previous_serial == 65535
                                                ? 1
                                                : static_cast<std::uint16_t>(previous_serial + 1);
                                            try {
                                                if (storage) storage->save_snapshot(scene);
                                                changed = true;
                                            } catch (...) {
                                                *existing = original;
                                                entity->task_inventory_serial = previous_serial;
                                                throw;
                                            }
                                        }
                                    } else {
                                        const auto source = viewer_grid
                                            ? viewer_grid->find_inventory_item(agent_id, source_id)
                                            : std::nullopt;
                                        if (source && (source->current_permissions &
                                                homeworldz::scene::permission_copy) != 0) {
                                            const auto created = static_cast<std::uint64_t>(
                                                std::chrono::duration_cast<std::chrono::seconds>(
                                                    std::chrono::system_clock::now().time_since_epoch()).count());
                                            entity->task_inventory.push_back({
                                                homeworldz::viewer::random_uuid(), source->asset_id,
                                                source->creator_id, agent_id, source->owner_id,
                                                "00000000-0000-0000-0000-000000000000",
                                                source->name, source->description,
                                                static_cast<std::int8_t>(source->asset_type),
                                                static_cast<std::int8_t>(source->inventory_type),
                                                source->flags, source->base_permissions,
                                                source->current_permissions, 0,
                                                source->everyone_permissions, source->next_permissions,
                                                static_cast<std::uint8_t>(source->sale_type),
                                                source->sale_price, created});
                                            entity->task_inventory_serial = previous_serial == 65535
                                                ? 1
                                                : static_cast<std::uint16_t>(previous_serial + 1);
                                            try {
                                                if (storage) storage->save_snapshot(scene);
                                                changed = true;
                                            } catch (...) {
                                                entity->task_inventory.pop_back();
                                                entity->task_inventory_serial = previous_serial;
                                                throw;
                                            }
                                        } else if (source && viewer_grid) {
                                            operation = "transfer";
                                            const auto task_item_id = homeworldz::viewer::random_uuid();
                                            const auto transfer = viewer_grid->prepare_task_inventory_transfer({
                                                homeworldz::viewer::random_uuid(), agent_id, source_id,
                                                provisioned_region_id, entity->object_id, task_item_id});
                                            if (transfer && transfer->state == "prepared") {
                                                const auto finalized = apply_task_inventory_transfer(*transfer);
                                                changed = std::any_of(
                                                    entity->task_inventory.begin(),
                                                    entity->task_inventory.end(),
                                                    [&](const auto& item) {
                                                        return item.item_id == transfer->task_item_id;
                                                    });
                                                if (changed && !finalized)
                                                    std::cerr << "{\"level\":\"warning\",\"message\":\"task inventory transfer awaits reconciliation\",\"transferId\":"
                                                              << homeworldz::api::json_string(transfer->id)
                                                              << "}" << std::endl;
                                            }
                                        } else if (viewer_grid && storage) {
                                            // "New Script"/"New Note" in the
                                            // Contents tab: a fresh item that is
                                            // neither already in the task nor an
                                            // existing agent-inventory item. Mint
                                            // the default asset for its type and
                                            // add it to the task.
                                            const auto initial =
                                                homeworldz::inventory::default_asset_content(
                                                    task_inventory_update->asset_type,
                                                    task_inventory_update->inventory_type,
                                                    provisioned_region_id, entity->position);
                                            if (initial && task_inventory_update->transaction_id ==
                                                               homeworldz::viewer::Uuid{}) {
                                                operation = "create";
                                                std::string new_asset_id;
                                                try {
                                                    new_asset_id = homeworldz::viewer::random_uuid();
                                                    const auto content = std::span(
                                                        reinterpret_cast<const std::byte*>(initial->data()),
                                                        initial->size());
                                                    const auto metadata = storage->store_asset(
                                                        new_asset_id, agent_id, content);
                                                    if (!viewer_grid->register_asset(
                                                            metadata.viewer_id, metadata.creator_id,
                                                            metadata.sha256, metadata.size,
                                                            region_public_endpoint, true))
                                                        new_asset_id.clear();
                                                } catch (const std::exception&) {
                                                    new_asset_id.clear();
                                                }
                                                if (!new_asset_id.empty()) {
                                                    const auto created = static_cast<std::uint64_t>(
                                                        std::chrono::duration_cast<std::chrono::seconds>(
                                                            std::chrono::system_clock::now()
                                                                .time_since_epoch())
                                                            .count());
                                                    entity->task_inventory.push_back(
                                                        {homeworldz::viewer::random_uuid(), new_asset_id,
                                                         agent_id, agent_id, agent_id,
                                                         "00000000-0000-0000-0000-000000000000",
                                                         task_inventory_update->name,
                                                         task_inventory_update->description,
                                                         task_inventory_update->asset_type,
                                                         task_inventory_update->inventory_type,
                                                         task_inventory_update->flags,
                                                         homeworldz::scene::permission_creator,
                                                         homeworldz::scene::permission_creator, 0, 0,
                                                         homeworldz::scene::permission_all,
                                                         static_cast<std::uint8_t>(
                                                             task_inventory_update->sale_type),
                                                         task_inventory_update->sale_price, created});
                                                    entity->task_inventory_serial =
                                                        previous_serial == 65535
                                                            ? 1
                                                            : static_cast<std::uint16_t>(
                                                                  previous_serial + 1);
                                                    try {
                                                        storage->save_snapshot(scene);
                                                        changed = true;
                                                    } catch (...) {
                                                        entity->task_inventory.pop_back();
                                                        entity->task_inventory_serial = previous_serial;
                                                        throw;
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            } catch (const std::exception& error) {
                                std::cout << "{\"level\":\"error\",\"message\":\"task inventory mutation failed\",\"error\":"
                                          << homeworldz::api::json_string(error.what()) << "}" << std::endl;
                            }
                            bool refresh_sent = false;
                            if (changed && entity) {
                                const auto task_id = homeworldz::viewer::parse_uuid(entity->object_id);
                                const auto content = task_inventory_file(*entity);
                                if (task_id) {
                                    const auto filename =
                                        "inventory_" + homeworldz::viewer::random_uuid() + ".tmp";
                                    pending_task_inventory_files.insert_or_assign(
                                        endpoint + '|' + filename, content);
                                    auto wire_filename = filename;
                                    wire_filename.push_back('\0');
                                    const auto payload = homeworldz::viewer::encode_reply_task_inventory({
                                        *task_id,
                                        static_cast<std::int16_t>(entity->task_inventory_serial),
                                        wire_filename});
                                    if (const auto outgoing = circuits.send(
                                            endpoint, payload, true, now, true))
                                        refresh_sent = send_udp(viewer_server, endpoint, *outgoing);
                                }
                            }
                            std::cout << "{\"level\":" << (changed ? "\"info\"" : "\"warn\"")
                                      << ",\"message\":\"task inventory item " << operation << ' '
                                      << (changed ? "completed" : "rejected") << "\",\"localId\":"
                                      << task_inventory_update->local_id << ",\"itemId\":"
                                      << homeworldz::api::json_string(source_id)
                                      << ",\"refreshSent\":" << (refresh_sent ? "true" : "false")
                                      << "}" << std::endl;
                        }
                        const auto task_inventory_remove =
                            homeworldz::viewer::decode_remove_task_inventory(packet->payload);
                        if (task_inventory_remove &&
                            task_inventory_remove->agent_id == identity->agent_id &&
                            task_inventory_remove->session_id == identity->session_id) {
                            auto* entity = scene.find(task_inventory_remove->local_id);
                            const auto agent_id = homeworldz::viewer::format_uuid(identity->agent_id);
                            const auto item_id = homeworldz::viewer::format_uuid(task_inventory_remove->item_id);
                            bool removed = false;
                            if (entity && entity->owner_id == agent_id &&
                                (entity->owner_permissions & homeworldz::scene::permission_modify) != 0) {
                                const auto item = std::find_if(
                                    entity->task_inventory.begin(), entity->task_inventory.end(),
                                    [&](const auto& candidate) { return candidate.item_id == item_id; });
                                if (item != entity->task_inventory.end()) {
                                    const auto index = static_cast<std::size_t>(
                                        item - entity->task_inventory.begin());
                                    const auto previous_serial = entity->task_inventory_serial;
                                    const auto original = *item;
                                    entity->task_inventory.erase(item);
                                    entity->task_inventory_serial = previous_serial == 65535
                                        ? 1
                                        : static_cast<std::uint16_t>(previous_serial + 1);
                                    try {
                                        if (storage) storage->save_snapshot(scene);
                                        removed = true;
                                        static_cast<void>(falcon.erase(entity->object_id, item_id));
                                    } catch (const std::exception& error) {
                                        entity->task_inventory.insert(
                                            entity->task_inventory.begin() + index, original);
                                        entity->task_inventory_serial = previous_serial;
                                        std::cout << "{\"level\":\"error\",\"message\":\"task inventory removal persistence failed\",\"error\":"
                                                  << homeworldz::api::json_string(error.what()) << "}" << std::endl;
                                    }
                                }
                            }
                            bool refresh_sent = false;
                            if (removed && entity) {
                                broadcast_object_update(
                                    *entity, std::chrono::steady_clock::now());
                                const auto task_id = homeworldz::viewer::parse_uuid(entity->object_id);
                                if (task_id) {
                                    std::string filename;
                                    if (entity->task_inventory_serial != 0) {
                                        const auto content = task_inventory_file(*entity);
                                        filename = "inventory_" +
                                            homeworldz::viewer::random_uuid() + ".tmp";
                                        pending_task_inventory_files.insert_or_assign(
                                            endpoint + '|' + filename, content);
                                    }
                                    auto wire_filename = filename;
                                    if (!wire_filename.empty()) wire_filename.push_back('\0');
                                    const auto payload = homeworldz::viewer::encode_reply_task_inventory({
                                        *task_id,
                                        static_cast<std::int16_t>(entity->task_inventory_serial),
                                        wire_filename});
                                    if (const auto outgoing = circuits.send(
                                            endpoint, payload, true, now, true))
                                        refresh_sent = send_udp(viewer_server, endpoint, *outgoing);
                                }
                            }
                            std::cout << "{\"level\":" << (removed ? "\"info\"" : "\"warn\"")
                                      << ",\"message\":\"task inventory item removal "
                                      << (removed ? "completed" : "rejected") << "\",\"localId\":"
                                      << task_inventory_remove->local_id << ",\"itemId\":"
                                      << homeworldz::api::json_string(item_id)
                                      << ",\"refreshSent\":" << (refresh_sent ? "true" : "false")
                                      << "}" << std::endl;
                        }
                        const auto task_inventory_move =
                            homeworldz::viewer::decode_move_task_inventory(packet->payload);
                        if (task_inventory_move &&
                            task_inventory_move->agent_id == identity->agent_id &&
                            task_inventory_move->session_id == identity->session_id) {
                            const auto agent_id = homeworldz::viewer::format_uuid(identity->agent_id);
                            const auto folder_id = homeworldz::viewer::format_uuid(
                                task_inventory_move->folder_id);
                            const auto task_item_id = homeworldz::viewer::format_uuid(
                                task_inventory_move->item_id);
                            const auto* entity = scene.find(task_inventory_move->local_id);
                            std::optional<homeworldz::scene::TaskInventoryItem> task_item;
                            if (entity && entity->owner_id == agent_id) {
                                const auto found = std::find_if(
                                    entity->task_inventory.begin(), entity->task_inventory.end(),
                                    [&](const auto& item) { return item.item_id == task_item_id; });
                                if (found != entity->task_inventory.end() && found->owner_id == agent_id)
                                    task_item = *found;
                            }
                            const auto personal_item_id = homeworldz::viewer::random_uuid();
                            bool created = false;
                            bool removed_from_task = false;
                            if (task_item && viewer_grid) {
                                try {
                                    const homeworldz::grid::InventoryItem personal{
                                        personal_item_id, task_item->creator_id, agent_id,
                                        folder_id, task_item->asset_id, task_item->asset_type,
                                        task_item->inventory_type, task_item->name,
                                        task_item->description, task_item->flags,
                                        task_item->base_permissions,
                                        task_item->current_permissions,
                                        task_item->everyone_permissions,
                                        task_item->next_permissions, task_item->sale_type,
                                        task_item->sale_price};
                                    if ((task_item->current_permissions &
                                            homeworldz::scene::permission_copy) != 0) {
                                        created = viewer_grid->create_inventory_item(agent_id, personal);
                                    } else {
                                        const auto prepared = viewer_grid->prepare_task_inventory_extraction({
                                            homeworldz::viewer::random_uuid(), agent_id,
                                            provisioned_region_id, entity->object_id, task_item_id,
                                            folder_id, personal_item_id, personal});
                                        const auto finalized = prepared
                                            ? apply_task_inventory_extraction(*prepared)
                                            : std::nullopt;
                                        created = finalized.has_value();
                                        removed_from_task = created;
                                    }
                                } catch (const std::exception& error) {
                                    std::cout << "{\"level\":\"error\",\"message\":\"task inventory personal move failed\",\"error\":"
                                              << homeworldz::api::json_string(error.what()) << "}"
                                              << std::endl;
                                }
                            }
                            bool sent = false;
                            if (created && task_item) {
                                const auto item_id = homeworldz::viewer::parse_uuid(personal_item_id);
                                const auto creator_id = homeworldz::viewer::parse_uuid(task_item->creator_id);
                                const auto owner_id = homeworldz::viewer::parse_uuid(agent_id);
                                const auto destination_id = homeworldz::viewer::parse_uuid(folder_id);
                                const auto asset_id = homeworldz::viewer::parse_uuid(task_item->asset_id);
                                if (item_id && creator_id && owner_id && destination_id && asset_id) {
                                    homeworldz::viewer::InventoryItem response_item;
                                    response_item.item_id = *item_id;
                                    response_item.creator_id = *creator_id;
                                    response_item.owner_id = *owner_id;
                                    response_item.folder_id = *destination_id;
                                    response_item.asset_id = *asset_id;
                                    response_item.asset_type = task_item->asset_type;
                                    response_item.inventory_type = task_item->inventory_type;
                                    response_item.name = task_item->name;
                                    response_item.description = task_item->description;
                                    response_item.flags = task_item->flags;
                                    response_item.base_permissions = task_item->base_permissions;
                                    response_item.current_permissions = task_item->current_permissions;
                                    response_item.everyone_permissions = task_item->everyone_permissions;
                                    response_item.next_permissions = task_item->next_permissions;
                                    response_item.sale_type = task_item->sale_type;
                                    response_item.sale_price = task_item->sale_price;
                                    response_item.creation_date = static_cast<std::int32_t>(
                                        std::chrono::duration_cast<std::chrono::seconds>(
                                            std::chrono::system_clock::now().time_since_epoch()).count());
                                    const homeworldz::viewer::AgentMessage reply{
                                        identity->agent_id, identity->session_id};
                                    if (const auto outgoing = circuits.send(
                                            endpoint,
                                            homeworldz::viewer::encode_update_create_inventory_item(
                                                reply, 0, response_item),
                                            true, now, true))
                                        sent = send_udp(viewer_server, endpoint, *outgoing);
                                }
                            }
                            bool task_refresh_sent = false;
                            if (removed_from_task) {
                                const auto* updated = scene.find(task_inventory_move->local_id);
                                const auto task_id = updated
                                    ? homeworldz::viewer::parse_uuid(updated->object_id)
                                    : std::nullopt;
                                if (updated && task_id) {
                                    const auto content = task_inventory_file(*updated);
                                    const auto filename = "inventory_" +
                                        homeworldz::viewer::random_uuid() + ".tmp";
                                    pending_task_inventory_files.insert_or_assign(
                                        endpoint + '|' + filename, content);
                                    auto wire_filename = filename;
                                    wire_filename.push_back('\0');
                                    const auto payload = homeworldz::viewer::encode_reply_task_inventory({
                                        *task_id, static_cast<std::int16_t>(updated->task_inventory_serial),
                                        wire_filename});
                                    if (const auto outgoing = circuits.send(
                                            endpoint, payload, true, now, true))
                                        task_refresh_sent = send_udp(viewer_server, endpoint, *outgoing);
                                }
                            }
                            std::cout << "{\"level\":" << (created ? "\"info\"" : "\"warn\"")
                                      << ",\"message\":\"task inventory personal move "
                                      << (created ? "completed" : "rejected") << "\",\"localId\":"
                                      << task_inventory_move->local_id << ",\"taskItemId\":"
                                      << homeworldz::api::json_string(task_item_id)
                                      << ",\"viewerUpdateSent\":" << (sent ? "true" : "false")
                                      << ",\"removedFromTask\":" << (removed_from_task ? "true" : "false")
                                      << ",\"taskRefreshSent\":" << (task_refresh_sent ? "true" : "false")
                                      << "}" << std::endl;
                        }
                        const auto asset_transfer =
                            homeworldz::viewer::decode_transfer_request(packet->payload);
                        if (asset_transfer) {
                            // Firestorm's script and notecard editors fetch an
                            // inventory item's body over the asset-transfer channel
                            // (TransferRequest -> TransferInfo + TransferPacket(s)).
                            const auto* transfer_identity = circuits.identity(endpoint);
                            const bool authorized =
                                asset_transfer->source_type !=
                                    homeworldz::viewer::transfer_source_sim_inv_item ||
                                (transfer_identity &&
                                 asset_transfer->agent_id == transfer_identity->agent_id &&
                                 asset_transfer->session_id == transfer_identity->session_id);
                            std::int32_t status = homeworldz::viewer::transfer_status_ok;
                            std::vector<std::byte> asset_bytes;
                            if (!authorized) {
                                status = homeworldz::viewer::transfer_status_unknown_source;
                            } else {
                                try {
                                    asset_bytes = read_federated_asset(
                                        homeworldz::viewer::format_uuid(asset_transfer->asset_id));
                                } catch (const std::exception&) {
                                    status = homeworldz::viewer::transfer_status_unknown_source;
                                }
                            }
                            const auto info = homeworldz::viewer::encode_transfer_info(
                                asset_transfer->transfer_id, asset_transfer->channel_type, status,
                                status == homeworldz::viewer::transfer_status_ok
                                    ? static_cast<std::int32_t>(asset_bytes.size())
                                    : 0,
                                asset_transfer->params);
                            if (const auto outgoing = circuits.send(endpoint, info, true, now))
                                send_udp(viewer_server, endpoint, *outgoing);
                            if (status == homeworldz::viewer::transfer_status_ok) {
                                constexpr std::size_t transfer_chunk_size = 1000;
                                std::int32_t packet_number = 0;
                                std::size_t offset = 0;
                                do {
                                    const auto chunk_size = (std::min)(
                                        transfer_chunk_size, asset_bytes.size() - offset);
                                    const bool final = offset + chunk_size >= asset_bytes.size();
                                    const auto data = std::span<const std::byte>(
                                        asset_bytes.data() + offset, chunk_size);
                                    const auto pkt = homeworldz::viewer::encode_transfer_packet(
                                        asset_transfer->transfer_id, asset_transfer->channel_type,
                                        packet_number,
                                        final ? homeworldz::viewer::transfer_status_done
                                              : homeworldz::viewer::transfer_status_ok,
                                        data);
                                    if (const auto outgoing = circuits.send(endpoint, pkt, true, now))
                                        send_udp(viewer_server, endpoint, *outgoing);
                                    offset += chunk_size;
                                    ++packet_number;
                                } while (offset < asset_bytes.size());
                            }
                            std::cout << "{\"level\":" << (status == 0 ? "\"info\"" : "\"warn\"")
                                      << ",\"message\":\"asset transfer "
                                      << (status == 0 ? "served" : "rejected") << "\",\"assetId\":"
                                      << homeworldz::api::json_string(
                                             homeworldz::viewer::format_uuid(asset_transfer->asset_id))
                                      << ",\"sourceType\":" << asset_transfer->source_type
                                      << ",\"bytes\":" << asset_bytes.size() << "}" << std::endl;
                        }
                        const auto task_inventory_xfer =
                            homeworldz::viewer::decode_request_xfer(packet->payload);
                        if (task_inventory_xfer) {
                            const auto pending = pending_task_inventory_files.find(
                                endpoint + '|' + task_inventory_xfer->filename);
                            bool sent = false;
                            if (pending != pending_task_inventory_files.end()) {
                                constexpr std::size_t xfer_chunk_size = 1000;
                                const auto chunk_size = (std::min)(
                                    xfer_chunk_size, pending->second.size());
                                std::vector<std::byte> xfer_data(4);
                                const auto size = static_cast<std::uint32_t>(pending->second.size());
                                for (unsigned index = 0; index < 4; ++index)
                                    xfer_data[index] = static_cast<std::byte>(size >> (index * 8));
                                xfer_data.insert(
                                    xfer_data.end(), pending->second.begin(),
                                    pending->second.begin() + chunk_size);
                                const bool final = chunk_size == pending->second.size();
                                const auto payload = homeworldz::viewer::encode_send_xfer_packet(
                                    task_inventory_xfer->id, final ? 0x80000000U : 0U, xfer_data);
                                if (const auto outgoing = circuits.send(
                                        endpoint, payload, true, now, true))
                                    sent = send_udp(viewer_server, endpoint, *outgoing);
                                if (sent && !final) {
                                    pending_task_inventory_xfers.insert_or_assign(
                                        endpoint + '|' + std::to_string(task_inventory_xfer->id),
                                        PendingTaskInventoryXfer{
                                            std::move(pending->second), chunk_size, 1, 0});
                                }
                                if (sent) pending_task_inventory_files.erase(pending);
                            }
                            std::cout << "{\"level\":" << (sent ? "\"info\"" : "\"warn\"")
                                      << ",\"message\":\"task inventory xfer "
                                      << (sent ? "sent" : "rejected") << "\",\"filename\":"
                                      << homeworldz::api::json_string(task_inventory_xfer->filename)
                                      << "}" << std::endl;
                        }
                        const auto task_inventory_confirmation =
                            homeworldz::viewer::decode_confirm_xfer_packet(packet->payload);
                        if (task_inventory_confirmation) {
                            const auto key = endpoint + '|' +
                                std::to_string(task_inventory_confirmation->id);
                            const auto pending = pending_task_inventory_xfers.find(key);
                            if (pending != pending_task_inventory_xfers.end()) {
                                bool sent = false;
                                bool final = false;
                                std::uint32_t packet_number{};
                                if (task_inventory_confirmation->packet ==
                                    pending->second.awaiting_confirmation &&
                                    pending->second.offset < pending->second.data.size()) {
                                    constexpr std::size_t xfer_chunk_size = 1000;
                                    const auto remaining =
                                        pending->second.data.size() - pending->second.offset;
                                    const auto chunk_size = (std::min)(xfer_chunk_size, remaining);
                                    packet_number = pending->second.next_packet;
                                    final = chunk_size == remaining;
                                    const auto packet_field = final
                                        ? packet_number | 0x80000000U
                                        : packet_number;
                                    const auto begin =
                                        pending->second.data.begin() + pending->second.offset;
                                    const auto payload = homeworldz::viewer::encode_send_xfer_packet(
                                        task_inventory_confirmation->id, packet_field,
                                        std::span<const std::byte>(&*begin, chunk_size));
                                    if (const auto outgoing = circuits.send(
                                            endpoint, payload, true, now, true))
                                        sent = send_udp(viewer_server, endpoint, *outgoing);
                                    if (sent && !final) {
                                        pending->second.offset += chunk_size;
                                        pending->second.awaiting_confirmation = packet_number;
                                        ++pending->second.next_packet;
                                    }
                                    if (sent && final) pending_task_inventory_xfers.erase(pending);
                                }
                                std::cout << "{\"level\":" << (sent ? "\"info\"" : "\"warn\"")
                                          << ",\"message\":\"task inventory xfer continuation "
                                          << (sent ? "sent" : "rejected") << "\",\"packet\":"
                                          << packet_number << ",\"final\":"
                                          << (final ? "true" : "false") << "}" << std::endl;
                            }
                        }
                        const auto create_folder =
                            homeworldz::viewer::decode_create_inventory_folder(packet->payload);
                        if (create_folder && create_folder->agent_id == identity->agent_id &&
                            create_folder->session_id == identity->session_id) {
                            const auto user_id = homeworldz::viewer::format_uuid(identity->agent_id);
                            const auto folder_id = homeworldz::viewer::format_uuid(create_folder->folder_id);
                            const auto parent_id = homeworldz::viewer::format_uuid(create_folder->parent_id);
                            bool created = false;
                            try {
                                created = viewer_grid && viewer_grid->create_inventory_folder(
                                    user_id, folder_id, parent_id, create_folder->name,
                                    static_cast<int>(create_folder->type));
                            } catch (const std::exception& error) {
                                std::cout << "{\"level\":\"error\",\"message\":\"inventory folder creation failed\",\"error\":"
                                          << homeworldz::api::json_string(error.what()) << "}" << std::endl;
                            }
                            std::cout << "{\"level\":" << (created ? "\"info\"" : "\"warn\"")
                                      << ",\"message\":\"inventory folder creation "
                                      << (created ? "completed" : "rejected") << "\",\"folderId\":"
                                      << homeworldz::api::json_string(folder_id) << "}" << std::endl;
                        }
                        const auto move_folders =
                            homeworldz::viewer::decode_move_inventory_folder(packet->payload);
                        if (move_folders && move_folders->agent_id == identity->agent_id &&
                            move_folders->session_id == identity->session_id) {
                            const auto user_id = homeworldz::viewer::format_uuid(identity->agent_id);
                            std::size_t moved = 0;
                            for (const auto& move : move_folders->folders) {
                                const auto folder_id = homeworldz::viewer::format_uuid(move.folder_id);
                                const auto parent_id = homeworldz::viewer::format_uuid(move.parent_id);
                                try {
                                    if (viewer_grid && viewer_grid->move_inventory_folder(
                                            user_id, folder_id, parent_id))
                                        ++moved;
                                } catch (const std::exception& error) {
                                    std::cout << "{\"level\":\"error\",\"message\":\"inventory folder move failed\",\"error\":"
                                              << homeworldz::api::json_string(error.what()) << "}" << std::endl;
                                }
                            }
                            std::cout << "{\"level\":"
                                      << (moved == move_folders->folders.size() ? "\"info\"" : "\"warn\"")
                                      << ",\"message\":\"inventory folder move batch processed\",\"moved\":"
                                      << moved << ",\"requested\":" << move_folders->folders.size() << "}"
                                      << std::endl;
                        }
                        const auto move_items =
                            homeworldz::viewer::decode_move_inventory_item(packet->payload);
                        if (move_items && move_items->agent_id == identity->agent_id &&
                            move_items->session_id == identity->session_id) {
                            const auto user_id = homeworldz::viewer::format_uuid(identity->agent_id);
                            std::size_t moved = 0;
                            for (const auto& move : move_items->items) {
                                const auto item_id = homeworldz::viewer::format_uuid(move.item_id);
                                const auto folder_id = homeworldz::viewer::format_uuid(move.folder_id);
                                try {
                                    if (viewer_grid && viewer_grid->move_inventory_item(
                                            user_id, item_id, folder_id, move.new_name))
                                        ++moved;
                                } catch (const std::exception& error) {
                                    std::cout << "{\"level\":\"error\",\"message\":\"inventory item move failed\",\"error\":"
                                              << homeworldz::api::json_string(error.what()) << "}" << std::endl;
                                }
                            }
                            std::cout << "{\"level\":"
                                      << (moved == move_items->items.size() ? "\"info\"" : "\"warn\"")
                                      << ",\"message\":\"inventory item move batch processed\",\"moved\":"
                                      << moved << ",\"requested\":" << move_items->items.size() << "}"
                                      << std::endl;
                        }
                        const auto copy_item =
                            homeworldz::viewer::decode_copy_inventory_item(packet->payload);
                        if (copy_item && copy_item->agent_id == identity->agent_id &&
                            copy_item->session_id == identity->session_id) {
                            const auto user_id = homeworldz::viewer::format_uuid(identity->agent_id);
                            const auto source_id = homeworldz::viewer::format_uuid(copy_item->old_item_id);
                            const auto destination_id = homeworldz::viewer::format_uuid(copy_item->new_folder_id);
                            const auto source_owner_id = homeworldz::viewer::format_uuid(copy_item->old_agent_id);
                            const bool library_copy = source_owner_id == system_creator_id;
                            std::optional<homeworldz::grid::InventoryItem> copied;
                            try {
                                if (viewer_grid && library_copy) copied = viewer_grid->copy_library_item(
                                    user_id, source_id, destination_id, copy_item->new_name);
                                else if (viewer_grid && source_owner_id == user_id)
                                    copied = viewer_grid->copy_inventory_item(
                                        user_id, source_id, destination_id, copy_item->new_name);
                            } catch (const std::exception& error) {
                                std::cout << "{\"level\":\"error\",\"message\":\"inventory item copy failed\",\"error\":"
                                          << homeworldz::api::json_string(error.what()) << "}" << std::endl;
                            }
                            bool sent = false;
                            if (copied) {
                                const auto item_id = homeworldz::viewer::parse_uuid(copied->item_id);
                                const auto creator_id = homeworldz::viewer::parse_uuid(copied->creator_id);
                                const auto owner_id = homeworldz::viewer::parse_uuid(copied->owner_id);
                                const auto folder_id = homeworldz::viewer::parse_uuid(copied->folder_id);
                                const auto asset_id = homeworldz::viewer::parse_uuid(copied->asset_id);
                                if (item_id && creator_id && owner_id && folder_id && asset_id) {
                                    homeworldz::viewer::InventoryItem item;
                                    item.item_id = *item_id;
                                    item.creator_id = *creator_id;
                                    item.owner_id = *owner_id;
                                    item.folder_id = *folder_id;
                                    item.asset_id = *asset_id;
                                    item.asset_type = static_cast<std::int8_t>(copied->asset_type);
                                    item.inventory_type = static_cast<std::int8_t>(copied->inventory_type);
                                    item.name = copied->name;
                                    item.description = copied->description;
                                    item.flags = copied->flags;
                                    item.base_permissions = copied->base_permissions;
                                    item.current_permissions = copied->current_permissions;
                                    item.everyone_permissions = copied->everyone_permissions;
                                    item.next_permissions = copied->next_permissions;
                                    item.sale_type = static_cast<std::uint8_t>(copied->sale_type);
                                    item.sale_price = copied->sale_price;
                                    item.creation_date = static_cast<std::int32_t>(
                                        std::chrono::duration_cast<std::chrono::seconds>(
                                            std::chrono::system_clock::now().time_since_epoch()).count());
                                    const homeworldz::viewer::AgentMessage reply{
                                        identity->agent_id, identity->session_id};
                                    auto payload = homeworldz::viewer::encode_update_create_inventory_item(
                                        reply, copy_item->callback_id, item);
                                    if (!payload.empty()) {
                                        if (const auto outgoing = circuits.send(
                                                endpoint, std::move(payload), true, now, true))
                                            sent = send_udp(viewer_server, endpoint, *outgoing);
                                    }
                                }
                            }
                            std::cout << "{\"level\":" << (sent ? "\"info\"" : "\"warn\"")
                                      << ",\"message\":\"" << (library_copy ? "library" : "personal")
                                      << " inventory copy "
                                      << (sent ? "completed" : "rejected") << "\",\"sourceItemId\":"
                                      << homeworldz::api::json_string(source_id) << "}" << std::endl;
                        }
                        const auto create_item =
                            homeworldz::viewer::decode_create_inventory_item(packet->payload);
                        if (create_item && create_item->agent_id == identity->agent_id &&
                            create_item->session_id == identity->session_id) {
                            const auto user_id = homeworldz::viewer::format_uuid(identity->agent_id);
                            const auto transaction_id =
                                homeworldz::viewer::format_uuid(create_item->transaction_id);
                            const auto pending =
                                pending_inventory_asset_uploads.find(endpoint + '|' + transaction_id);
                            const bool wearable = create_item->inventory_type == 18 &&
                                (create_item->asset_type == 5 || create_item->asset_type == 13);
                            bool created = false;
                            bool consumed_pending_upload = false;
                            homeworldz::grid::InventoryItem item;
                            const bool editable_asset = wearable ||
                                (create_item->asset_type == 7 && create_item->inventory_type == 7) ||
                                (create_item->asset_type == 10 && create_item->inventory_type == 10) ||
                                (create_item->asset_type == 21 && create_item->inventory_type == 20);
                            std::string asset_id;
                            if (editable_asset && pending != pending_inventory_asset_uploads.end() &&
                                pending->second.asset_type == create_item->asset_type) {
                                asset_id = pending->second.asset_id;
                                consumed_pending_upload = true;
                            } else if (pending == pending_inventory_asset_uploads.end()) {
                                const auto avatar = avatars.find(endpoint);
                                const auto position = avatar == avatars.end() ?
                                    homeworldz::scene::Vector3{128.0, 128.0, 25.0} :
                                    avatar->second.controller.state().position;
                                const auto initial_content = homeworldz::inventory::default_asset_content(
                                    create_item->asset_type, create_item->inventory_type,
                                    registration->region_id(), position);
                                if (initial_content && viewer_grid) {
                                    try {
                                        asset_id = homeworldz::viewer::random_uuid();
                                        const auto content = std::span(
                                            reinterpret_cast<const std::byte*>(initial_content->data()),
                                            initial_content->size());
                                        const auto metadata = storage->store_asset(asset_id, user_id, content);
                                        if (!viewer_grid->register_asset(
                                                metadata.viewer_id, metadata.creator_id, metadata.sha256,
                                                metadata.size, region_public_endpoint, true))
                                            asset_id.clear();
                                    } catch (const std::exception& error) {
                                        asset_id.clear();
                                        std::cout << "{\"level\":\"error\",\"message\":\"default inventory asset creation failed\",\"error\":"
                                                  << homeworldz::api::json_string(error.what()) << "}" << std::endl;
                                    }
                                }
                            }
                            if (!asset_id.empty() && viewer_grid) {
                                item.item_id = homeworldz::viewer::random_uuid();
                                item.creator_id = user_id;
                                item.owner_id = user_id;
                                item.folder_id = homeworldz::viewer::format_uuid(create_item->folder_id);
                                item.asset_id = asset_id;
                                item.asset_type = create_item->asset_type;
                                item.inventory_type = create_item->inventory_type;
                                item.name = create_item->name;
                                item.description = create_item->description;
                                item.flags = wearable ? create_item->wearable_type : 0;
                                item.base_permissions = homeworldz::scene::permission_creator;
                                item.current_permissions = homeworldz::scene::permission_creator;
                                item.everyone_permissions = 0x00000000U;
                                item.next_permissions = create_item->next_owner_permissions;
                                try {
                                    created = viewer_grid->create_inventory_item(user_id, item);
                                } catch (const std::exception& error) {
                                    std::cout << "{\"level\":\"error\",\"message\":\"inventory asset item creation failed\",\"error\":"
                                              << homeworldz::api::json_string(error.what()) << "}" << std::endl;
                                }
                            }
                            bool sent = false;
                            if (created) {
                                const auto item_id = homeworldz::viewer::parse_uuid(item.item_id);
                                const auto creator_id = homeworldz::viewer::parse_uuid(item.creator_id);
                                const auto owner_id = homeworldz::viewer::parse_uuid(item.owner_id);
                                const auto folder_id = homeworldz::viewer::parse_uuid(item.folder_id);
                                const auto parsed_asset_id = homeworldz::viewer::parse_uuid(item.asset_id);
                                if (item_id && creator_id && owner_id && folder_id && parsed_asset_id) {
                                    homeworldz::viewer::InventoryItem response_item;
                                    response_item.item_id = *item_id;
                                    response_item.creator_id = *creator_id;
                                    response_item.owner_id = *owner_id;
                                    response_item.folder_id = *folder_id;
                                    response_item.asset_id = *parsed_asset_id;
                                    response_item.asset_type = static_cast<std::int8_t>(item.asset_type);
                                    response_item.inventory_type = static_cast<std::int8_t>(item.inventory_type);
                                    response_item.name = item.name;
                                    response_item.description = item.description;
                                    response_item.flags = item.flags;
                                    response_item.base_permissions = item.base_permissions;
                                    response_item.current_permissions = item.current_permissions;
                                    response_item.everyone_permissions = item.everyone_permissions;
                                    response_item.next_permissions = item.next_permissions;
                                    response_item.creation_date = static_cast<std::int32_t>(
                                        std::chrono::duration_cast<std::chrono::seconds>(
                                            std::chrono::system_clock::now().time_since_epoch()).count());
                                    const homeworldz::viewer::AgentMessage reply{
                                        identity->agent_id, identity->session_id};
                                    if (const auto outgoing = circuits.send(endpoint,
                                            homeworldz::viewer::encode_update_create_inventory_item(
                                                reply, create_item->callback_id, response_item),
                                            true, now, true))
                                        sent = send_udp(viewer_server, endpoint, *outgoing);
                                }
                                if (consumed_pending_upload)
                                    pending_inventory_asset_uploads.erase(pending);
                            }
                            std::cout << "{\"level\":" << (created && sent ? "\"info\"" : "\"warn\"")
                                      << ",\"message\":\"inventory asset item "
                                      << (created && sent ? "created" : "rejected") << "\",\"name\":"
                                      << homeworldz::api::json_string(create_item->name)
                                      << ",\"wearableType\":"
                                      << static_cast<unsigned int>(create_item->wearable_type) << "}"
                                      << std::endl;
                        }
                        const auto handshake_reply = homeworldz::viewer::decode_region_handshake_reply(packet->payload);
                        if (handshake_reply && handshake_reply->agent_id == identity->agent_id &&
                            handshake_reply->session_id == identity->session_id) {
                            handshake_replies.insert(endpoint);
                        }
                        auto cached_texture =
                            homeworldz::viewer::decode_agent_cached_texture(packet->payload);
                        if (cached_texture && cached_texture->agent_id == identity->agent_id &&
                            cached_texture->session_id == identity->session_id) {
                            std::size_t hits = 0;
                            for (auto& query : cached_texture->queries) {
                                const auto asset_id = storage->find_baked_texture(
                                    homeworldz::viewer::format_uuid(query.cache_id), query.texture_index);
                                if (!asset_id) continue;
                                if (const auto parsed = homeworldz::viewer::parse_uuid(*asset_id)) {
                                    query.texture_id = *parsed;
                                    ++hits;
                                }
                            }
                            if (const auto outgoing = circuits.send(endpoint,
                                    homeworldz::viewer::encode_agent_cached_texture_response(*cached_texture),
                                    true, now, true)) {
                                static_cast<void>(send_udp(viewer_server, endpoint, *outgoing));
                                std::cout << "{\"level\":\"info\",\"message\":\"wearable cache response sent\","
                                             "\"hits\":" << hits << ",\"misses\":"
                                          << cached_texture->queries.size() - hits << "}"
                                          << std::endl;
                            }
                        }
                        const auto appearance =
                            homeworldz::viewer::decode_agent_set_appearance(packet->payload);
                        if (appearance && appearance->agent_id == identity->agent_id &&
                            appearance->session_id == identity->session_id) {
                            avatar_appearances.insert_or_assign(endpoint, *appearance);
                            if (const auto geometry = homeworldz::viewer::avatar_geometry(*appearance)) {
                                avatar_geometries[endpoint] = *geometry;
                                if (const auto live = avatars.find(endpoint); live != avatars.end()) {
                                    live->second.controller.set_avatar_geometry(
                                        geometry->height, geometry->hip_offset);
                                    if (physics_world) {
                                        if (live->second.physics_character != 0)
                                            physics_world->remove_character(live->second.physics_character);
                                        live->second.physics_character = physics_world->create_character({
                                            live->second.entity_id, live->second.controller.state().position,
                                            0.3, geometry->height, 0.4});
                                        physics_world->set_character_flying(
                                            live->second.physics_character,
                                            live->second.controller.state().flying);
                                    }
                                }
                                std::cout << "{\"level\":\"info\",\"message\":\"avatar geometry updated\","
                                             "\"height\":" << geometry->height << ",\"hipOffset\":"
                                          << geometry->hip_offset << ",\"visualParams\":"
                                          << appearance->visual_params.size() << "}" << std::endl;
                            }
                            std::size_t stored = 0;
                            for (const auto& entry : appearance->cache_entries) {
                                if (entry.texture_index >= appearance->texture_ids.size()) continue;
                                const auto asset_id = homeworldz::viewer::format_uuid(
                                    appearance->texture_ids[entry.texture_index]);
                                if (!storage->find_asset(asset_id)) continue;
                                storage->store_baked_texture(
                                    homeworldz::viewer::format_uuid(entry.cache_id),
                                    entry.texture_index, asset_id);
                                ++stored;
                            }
                            if (stored != 0)
                                std::cout << "{\"level\":\"info\",\"message\":\"wearable cache updated\","
                                             "\"count\":" << stored << "}" << std::endl;
                            // Relay the client's own appearance. But a headless
                            // client echoes back the server-bake UUIDs we seeded it,
                            // in a legacy (v0) message; relayed as-is, viewers would
                            // composite it locally (grey) instead of using the bakes.
                            // Detect our own server bake (system/zero creator) in the
                            // head slot and re-broadcast it as server-side (v1) with
                            // the matching default visual params. A real baker's own
                            // textures — and mid-bake placeholders — are never our
                            // system bake, so they relay unchanged as v0 (no shape
                            // oscillation) and the wearer keeps its local bake.
                            // Relay the client's own appearance untouched. Never
                            // substitute or re-mark it: a real baker's mid-bake
                            // states briefly reference zero-creator textures, and
                            // rewriting them oscillates the avatar. Headless clients
                            // are covered by the join-seed; the server-bake delivery
                            // to viewers is tracked separately (ADR 0029).
                            const auto remote_appearance = homeworldz::viewer::encode_avatar_appearance({
                                identity->agent_id, appearance->serial, appearance->texture_entry,
                                appearance->visual_params});
                            std::size_t recipients = 0;
                            if (!remote_appearance.empty()) {
                                // Echo the completed appearance to the originating viewer as well.
                                for (const auto& [recipient_endpoint, recipient] : avatars) {
                                    static_cast<void>(recipient);
                                    if (const auto outgoing = circuits.send(
                                            recipient_endpoint, remote_appearance, true, now, true)) {
                                        if (send_udp(viewer_server, recipient_endpoint, *outgoing))
                                            ++recipients;
                                    }
                                }
                            }
                            std::cout << "{\"level\":\"info\",\"message\":\"avatar appearance distributed\",\"bytes\":"
                                      << remote_appearance.size() << ",\"recipients\":" << recipients << "}"
                                      << std::endl;
                        }
                        const auto agent_animation =
                            homeworldz::viewer::decode_agent_animation(packet->payload);
                        if (agent_animation && agent_animation->agent_id == identity->agent_id &&
                            agent_animation->session_id == identity->session_id) {
                            auto& animations = avatar_animations[endpoint];
                            auto& next_sequence = next_animation_sequences[endpoint];
                            if (next_sequence < 2) next_sequence = 2;
                            for (const auto& change : agent_animation->animations) {
                                const auto existing = std::find_if(
                                    animations.begin(), animations.end(), [&](const auto& animation) {
                                        return animation.animation_id == change.animation_id;
                                    });
                                if (change.start && existing == animations.end()) {
                                    animations.push_back(
                                        {change.animation_id, next_sequence++, identity->agent_id});
                                } else if (!change.start && existing != animations.end()) {
                                    animations.erase(existing);
                                }
                            }
                            if (animations.empty()) {
                                if (const auto stand = homeworldz::viewer::parse_uuid(
                                        "2408fe9e-df1d-1d7d-f4ff-1384fa7b350f"))
                                    animations.push_back({*stand, 1, identity->agent_id});
                            }
                            const homeworldz::viewer::AvatarAnimation response{
                                identity->agent_id, animations};
                            // Broadcast to every viewer in the region (not just
                            // the emitter) so played animations — including
                            // gesture-triggered ones — are visible to others,
                            // mirroring the locomotion-animation broadcast.
                            const auto payload = homeworldz::viewer::encode_avatar_animation(response);
                            for (const auto& [recipient_endpoint, recipient] : avatars) {
                                static_cast<void>(recipient);
                                if (const auto outgoing = circuits.send(
                                        recipient_endpoint, payload, false, now, true))
                                    static_cast<void>(send_udp(viewer_server, recipient_endpoint, *outgoing));
                            }
                            std::cout << "{\"level\":\"info\",\"message\":\"avatar animation state updated\","
                                         "\"changes\":" << agent_animation->animations.size()
                                      << ",\"active\":" << animations.size()
                                      << ",\"recipients\":" << avatars.size() << "}" << std::endl;
                        }
                        const auto asset_upload =
                            homeworldz::viewer::decode_asset_upload_request(packet->payload);
                        if (asset_upload) {
                            bool success = false;
                            bool xfer_started = false;
                            homeworldz::viewer::Uuid asset_uuid{};
                            std::string asset_id;
                            const bool editable_inventory_asset =
                                asset_upload->asset_type == 5 || asset_upload->asset_type == 7 ||
                                asset_upload->asset_type == 10 || asset_upload->asset_type == 13 ||
                                asset_upload->asset_type == 21;
                            if (editable_inventory_asset &&
                                !asset_upload->temporary) {
                                try {
                                    const auto session = viewer_sessions ? viewer_sessions->validate(
                                        homeworldz::viewer::format_uuid(identity->session_id)) : std::nullopt;
                                    const auto secure_id = session ?
                                        homeworldz::viewer::parse_uuid(session->secure_session_id) : std::nullopt;
                                    if (!secure_id)
                                        throw std::runtime_error("secure viewer session was unavailable");
                                    asset_uuid = homeworldz::viewer::combine_uuids(
                                        asset_upload->transaction_id, *secure_id);
                                    asset_id = homeworldz::viewer::format_uuid(asset_uuid);
                                    const auto transaction_id =
                                        homeworldz::viewer::format_uuid(asset_upload->transaction_id);
                                    if (asset_upload->data.empty()) {
                                        const auto xfer_id = next_inventory_asset_xfer++;
                                        pending_inventory_asset_xfers.insert_or_assign(
                                            endpoint + '|' + std::to_string(xfer_id),
                                            PendingInventoryAssetXfer{transaction_id, asset_id, asset_uuid,
                                                asset_upload->asset_type, 0, 1000, {}, {}});
                                        if (const auto outgoing = circuits.send(endpoint,
                                                homeworldz::viewer::encode_request_xfer(
                                                    xfer_id, asset_uuid, asset_upload->asset_type),
                                                true, now, true)) {
                                            xfer_started = send_udp(viewer_server, endpoint, *outgoing);
                                        }
                                    } else {
                                        const auto metadata = storage->store_asset(
                                            asset_id, homeworldz::viewer::format_uuid(identity->agent_id),
                                            asset_upload->data);
                                        success = !viewer_grid || viewer_grid->register_asset(
                                            metadata.viewer_id, metadata.creator_id, metadata.sha256,
                                            metadata.size, region_public_endpoint, true);
                                        if (success) {
                                            pending_inventory_asset_uploads.insert_or_assign(
                                                endpoint + '|' + transaction_id,
                                                PendingInventoryAssetUpload{asset_id, asset_upload->asset_type});
                                        }
                                    }
                                } catch (const std::exception& error) {
                                    std::cout << "{\"level\":\"error\",\"message\":\"inventory asset upload failed\","
                                                 "\"error\":" << homeworldz::api::json_string(error.what())
                                              << "}" << std::endl;
                                }
                            }
                            if (!xfer_started) {
                                if (const auto outgoing = circuits.send(endpoint,
                                        homeworldz::viewer::encode_asset_upload_complete(
                                            asset_uuid, asset_upload->asset_type, success), true, now, true))
                                    static_cast<void>(send_udp(viewer_server, endpoint, *outgoing));
                            }
                            std::cout << "{\"level\":" << (success || xfer_started ? "\"info\"" : "\"warn\"")
                                      << ",\"message\":\"inventory asset upload "
                                      << (success ? "stored" : xfer_started ? "transfer requested" : "rejected")
                                      << "\",\"assetId\":"
                                      << homeworldz::api::json_string(asset_id) << ",\"bytes\":"
                                      << asset_upload->data.size() << "}" << std::endl;
                        }
                        const auto xfer_packet =
                            homeworldz::viewer::decode_send_xfer_packet(packet->payload);
                        if (xfer_packet) {
                            const auto key = endpoint + '|' + std::to_string(xfer_packet->id);
                            const auto pending = pending_inventory_asset_xfers.find(key);
                            if (pending != pending_inventory_asset_xfers.end()) {
                                auto& transfer = pending->second;
                                const auto packet_number = xfer_packet->packet & 0x7fffffffU;
                                const bool complete = (xfer_packet->packet & 0x80000000U) != 0;
                                bool accepted = false;
                                if (packet_number == 0 && xfer_packet->data.size() > 4) {
                                    transfer.expected_size =
                                        std::to_integer<std::size_t>(xfer_packet->data[0]) |
                                        (std::to_integer<std::size_t>(xfer_packet->data[1]) << 8) |
                                        (std::to_integer<std::size_t>(xfer_packet->data[2]) << 16) |
                                        (std::to_integer<std::size_t>(xfer_packet->data[3]) << 24);
                                    transfer.packet_size = xfer_packet->data.size() - 4;
                                    if (transfer.expected_size != 0 &&
                                        transfer.expected_size <= 4 * 1024 * 1024 &&
                                        transfer.packet_size <= transfer.expected_size) {
                                        transfer.data.assign(transfer.expected_size, std::byte{});
                                        std::copy(xfer_packet->data.begin() + 4, xfer_packet->data.end(),
                                                  transfer.data.begin());
                                        accepted = true;
                                    }
                                } else if (transfer.expected_size != 0 &&
                                           xfer_packet->data.size() <= transfer.packet_size) {
                                    const auto offset = static_cast<std::size_t>(packet_number) *
                                                        transfer.packet_size;
                                    if (offset + xfer_packet->data.size() <= transfer.data.size()) {
                                        std::copy(xfer_packet->data.begin(), xfer_packet->data.end(),
                                                  transfer.data.begin() + offset);
                                        accepted = true;
                                    }
                                }
                                if (accepted) {
                                    transfer.received_packets.insert(packet_number);
                                    if (const auto outgoing = circuits.send(endpoint,
                                            homeworldz::viewer::encode_confirm_xfer_packet(
                                                xfer_packet->id, packet_number), true, now, true))
                                        static_cast<void>(send_udp(viewer_server, endpoint, *outgoing));
                                }
                                if (accepted && complete) {
                                    bool contiguous = true;
                                    for (std::uint32_t index = 0; index <= packet_number; ++index)
                                        contiguous = contiguous && transfer.received_packets.contains(index);
                                    bool stored = false;
                                    if (contiguous) {
                                        try {
                                            const auto metadata = storage->store_asset(
                                                transfer.asset_id,
                                                homeworldz::viewer::format_uuid(identity->agent_id),
                                                transfer.data);
                                            stored = !viewer_grid || viewer_grid->register_asset(
                                                metadata.viewer_id, metadata.creator_id, metadata.sha256,
                                                metadata.size, region_public_endpoint, true);
                                            if (stored) {
                                                pending_inventory_asset_uploads.insert_or_assign(
                                                    endpoint + '|' + transfer.transaction_id,
                                                    PendingInventoryAssetUpload{
                                                        transfer.asset_id, transfer.asset_type});
                                            }
                                        } catch (const std::exception& error) {
                                            std::cout << "{\"level\":\"error\",\"message\":\"inventory asset transfer failed\","
                                                         "\"error\":"
                                                      << homeworldz::api::json_string(error.what()) << "}"
                                          << std::endl;
                                        }
                                    }
                                    if (const auto outgoing = circuits.send(endpoint,
                                            homeworldz::viewer::encode_asset_upload_complete(
                                                transfer.asset_uuid, transfer.asset_type, stored),
                                            true, now, true))
                                        static_cast<void>(send_udp(viewer_server, endpoint, *outgoing));
                                    std::cout << "{\"level\":" << (stored ? "\"info\"" : "\"warn\"")
                                              << ",\"message\":\"inventory asset transfer "
                                              << (stored ? "stored" : "rejected") << "\",\"assetId\":"
                                              << homeworldz::api::json_string(transfer.asset_id)
                                              << ",\"bytes\":" << transfer.expected_size << "}"
                                              << std::endl;
                                    pending_inventory_asset_xfers.erase(pending);
                                }
                            }
                        }
                        const auto inventory_asset =
                            homeworldz::viewer::decode_update_inventory_asset(packet->payload);
                        if (inventory_asset && inventory_asset->agent_id == identity->agent_id &&
                            inventory_asset->session_id == identity->session_id) {
                            const auto transaction_id =
                                homeworldz::viewer::format_uuid(inventory_asset->transaction_id);
                            const auto pending = pending_inventory_asset_uploads.find(endpoint + '|' + transaction_id);
                            const auto user_id = homeworldz::viewer::format_uuid(identity->agent_id);
                            const auto item_id = homeworldz::viewer::format_uuid(inventory_asset->item_id);
                            bool updated = false;
                            if (pending != pending_inventory_asset_uploads.end() && viewer_grid) {
                                updated = viewer_grid->update_inventory_item_asset(
                                    user_id, item_id, pending->second.asset_id);
                                if (updated) {
                                    if (const auto item = viewer_grid->find_inventory_item(user_id, item_id)) {
                                        homeworldz::viewer::InventoryItem response_item;
                                        const auto parsed_item = homeworldz::viewer::parse_uuid(item->item_id);
                                        const auto creator = homeworldz::viewer::parse_uuid(item->creator_id);
                                        const auto owner = homeworldz::viewer::parse_uuid(item->owner_id);
                                        const auto folder = homeworldz::viewer::parse_uuid(item->folder_id);
                                        const auto asset = homeworldz::viewer::parse_uuid(item->asset_id);
                                        if (parsed_item && creator && owner && folder && asset) {
                                            response_item.item_id = *parsed_item;
                                            response_item.creator_id = *creator;
                                            response_item.owner_id = *owner;
                                            response_item.folder_id = *folder;
                                            response_item.asset_id = *asset;
                                            response_item.asset_type = static_cast<std::int8_t>(item->asset_type);
                                            response_item.inventory_type = static_cast<std::int8_t>(item->inventory_type);
                                            response_item.name = item->name;
                                            response_item.description = item->description;
                                            response_item.flags = item->flags;
                                            response_item.base_permissions = item->base_permissions;
                                            response_item.current_permissions = item->current_permissions;
                                            response_item.everyone_permissions = item->everyone_permissions;
                                            response_item.next_permissions = item->next_permissions;
                                            response_item.sale_type = static_cast<std::uint8_t>(item->sale_type);
                                            response_item.sale_price = item->sale_price;
                                            const homeworldz::viewer::AgentMessage response{
                                                identity->agent_id, identity->session_id};
                                            if (const auto outgoing = circuits.send(endpoint,
                                                    homeworldz::viewer::encode_update_create_inventory_item(
                                                        response, 0, response_item), true, now, true))
                                                static_cast<void>(send_udp(viewer_server, endpoint, *outgoing));
                                        }
                                    }
                                    pending_inventory_asset_uploads.erase(pending);
                                }
                            }
                            std::cout << "{\"level\":" << (updated ? "\"info\"" : "\"warn\"")
                                      << ",\"message\":\"wearable inventory asset "
                                      << (updated ? "updated" : "rejected") << "\",\"itemId\":"
                                      << homeworldz::api::json_string(item_id) << "}" << std::endl;
                        }
                        const auto image_request = homeworldz::viewer::decode_request_image(packet->payload);
                        if (image_request && image_request->agent_id == identity->agent_id &&
                            image_request->session_id == identity->session_id) {
                            for (const auto& requested : image_request->requests) {
                                if (requested.download_priority <= 0.0F) continue;
                                const auto asset_id = homeworldz::viewer::format_uuid(requested.image_id);
                                const auto transfer_key = endpoint + '|' + asset_id;
                                if (active_texture_transfers.contains(transfer_key)) continue;
                                try {
                                    const auto asset = read_federated_asset(asset_id);
                                    auto payloads = homeworldz::viewer::encode_image_transfer(
                                        requested.image_id, asset, requested.packet);
                                    if (payloads.empty()) continue;
                                    active_texture_transfers.insert(transfer_key);
                                    for (std::size_t index = 0; index < payloads.size(); ++index) {
                                        texture_packets[endpoint].push_back(
                                            {asset_id, std::move(payloads[index]), index + 1 == payloads.size()});
                                    }
                                    std::cout << "{\"level\":\"info\",\"message\":\"texture transfer queued\","
                                                 "\"assetId\":" << homeworldz::api::json_string(asset_id)
                                              << ",\"packets\":" << payloads.size() << "}" << std::endl;
                                } catch (const std::exception&) {
                                }
                            }
                        }
                        const auto complete = homeworldz::viewer::decode_complete_agent_movement(packet->payload);
                        const auto provisional_arrival = inbound_transits.authorize(
                            homeworldz::viewer::format_uuid(identity->agent_id),
                            homeworldz::viewer::format_uuid(identity->session_id), now);
                        if (complete && (handshake_replies.contains(endpoint) || provisional_arrival) &&
                            complete->agent_id == identity->agent_id &&
                            complete->session_id == identity->session_id &&
                            complete->circuit_code == identity->circuit_code) {
                            const auto session_id = homeworldz::viewer::format_uuid(identity->session_id);
                            const auto agent_id = homeworldz::viewer::format_uuid(identity->agent_id);
                            std::optional<homeworldz::grid::AvatarTransit> arrival;
                            if (const auto* pending = inbound_transits.authorize(
                                    agent_id, session_id, now)) {
                                try {
                                    const auto activated = viewer_grid && registration ?
                                        viewer_grid->activate_avatar_transit(
                                            pending->id, registration->region_id()) : std::nullopt;
                                    if (!activated || activated->state != "activated")
                                        throw std::runtime_error("grid rejected transit activation");
                                    arrival = inbound_transits.consume(session_id, now);
                                    if (!arrival) throw std::runtime_error("provisional transit expired");
                                    if (viewer_sessions) viewer_sessions->invalidate(session_id);
                                    std::cout << "{\"level\":\"info\",\"message\":\"avatar transit activated\",\"transitId\":"
                                              << homeworldz::api::json_string(arrival->id) << "}" << std::endl;
                                } catch (const std::exception& error) {
                                    if (viewer_grid && registration)
                                        static_cast<void>(viewer_grid->rollback_avatar_transit(
                                            pending->id, registration->region_id(), error.what()));
                                    inbound_transits.remove(session_id);
                                    if (const auto failed = circuits.send(endpoint,
                                            homeworldz::viewer::encode_teleport_failed(
                                                {identity->agent_id, "Destination could not activate the arrival"}),
                                            true, now, true))
                                        static_cast<void>(send_udp(viewer_server, endpoint, *failed));
                                    std::cout << "{\"level\":\"error\",\"message\":\"avatar transit activation failed\",\"error\":"
                                              << homeworldz::api::json_string(error.what()) << "}" << std::endl;
                                    continue;
                                }
                            }
                            homeworldz::viewer::AgentMovementComplete response;
                            response.agent_id = identity->agent_id;
                            response.session_id = identity->session_id;
                            response.region_handle =
                                (static_cast<std::uint64_t>(region_grid_x * 256) << 32) |
                                static_cast<std::uint32_t>(region_grid_y * 256);
                            response.timestamp = static_cast<std::uint32_t>(
                                std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()));
                            response.channel_version = "HomeWorldz " + region_version;
                            if (!avatars.contains(endpoint)) {
                                const auto name = agent_id;
                                homeworldz::scene::EntityId entity{};
                                std::vector<homeworldz::scene::EntityId> duplicates;
                                for (const auto& [candidate_id, candidate] : scene.entities()) {
                                    if (candidate.name != name) continue;
                                    if (candidate_id > entity) {
                                        if (entity != 0) duplicates.push_back(entity);
                                        entity = candidate_id;
                                    } else {
                                        duplicates.push_back(candidate_id);
                                    }
                                }
                                for (const auto duplicate : duplicates) scene.remove(duplicate);
                                if (entity == 0) entity = scene.create(name, initial_spawn);
                                auto* persisted = scene.find(entity);
                                const auto arrival_position = arrival ? homeworldz::scene::Vector3{
                                    arrival->position[0], arrival->position[1], arrival->position[2]} :
                                    initial_spawn;
                                const auto spawn = arrival ? arrival_position :
                                    (persisted ? persisted->position : initial_spawn);
                                const auto known_geometry = avatar_geometries.find(endpoint);
                                const auto geometry = known_geometry == avatar_geometries.end() ?
                                    homeworldz::viewer::AvatarGeometry{} : known_geometry->second;
                                homeworldz::viewer::AvatarController controller{
                                    spawn, collision_ground_height(spawn),
                                    geometry.height, geometry.hip_offset,
                                    static_cast<double>(region_size_x),
                                    static_cast<double>(region_size_y)};
                                if (arrival) {
                                    const auto yaw = std::atan2(arrival->look_at[1], arrival->look_at[0]);
                                    const std::array<float, 3> rotation{
                                        0.0F, 0.0F, static_cast<float>(std::sin(yaw * 0.5))};
                                    controller.restore_motion({}, rotation, arrival->flying);
                                    if (persisted) {
                                        persisted->position = spawn;
                                        persisted->velocity = {};
                                        persisted->rotation = {rotation[0], rotation[1], rotation[2]};
                                        persisted->avatar_flying = arrival->flying;
                                    }
                                } else if (persisted) controller.restore_motion(
                                    persisted->velocity,
                                    {static_cast<float>(persisted->rotation.x),
                                     static_cast<float>(persisted->rotation.y),
                                     static_cast<float>(persisted->rotation.z)},
                                    persisted->avatar_flying);
                                const auto initial_position = controller.state().position;
                                const auto initial_viewer_position = controller.viewer_position();
                                response.position = {static_cast<float>(initial_position.x),
                                                     static_cast<float>(initial_position.y),
                                                     static_cast<float>(initial_position.z)};
                                const auto& rotation = controller.state().rotation;
                                const double qx = rotation[0], qy = rotation[1], qz = rotation[2];
                                const auto qw = std::sqrt((std::max)(
                                    0.0, 1.0 - qx * qx - qy * qy - qz * qz));
                                response.look_at = {
                                    static_cast<float>(1.0 - 2.0 * (qy * qy + qz * qz)),
                                    static_cast<float>(2.0 * (qx * qy + qw * qz)), 0.0F};
                                const auto [avatar_iterator, inserted] = avatars.emplace(endpoint, LiveAvatar{
                                    std::move(controller), entity, name,
                                    now + std::chrono::seconds(5), now + std::chrono::seconds(30),
                                    now + std::chrono::milliseconds(100), initial_viewer_position});
                                static_cast<void>(inserted);
                                avatar_iterator->second.last_pong = now;
                                avatar_iterator->second.restored_flying_until =
                                    avatar_iterator->second.controller.state().flying ?
                                        now + std::chrono::seconds(2) : now;
                                if (physics_world) {
                                    auto& live = avatars.at(endpoint);
                                    live.physics_character = physics_world->create_character({
                                        entity, live.controller.state().position, 0.3,
                                        live.controller.state().height, 0.4});
                                    physics_world->set_character_velocity(
                                        live.physics_character, live.controller.state().velocity);
                                    physics_world->set_character_flying(
                                        live.physics_character, live.controller.state().flying);
                                }
                                if (viewer_grid && registration)
                                    static_cast<void>(viewer_grid->update_presence(name, registration->region_id()));
                            }
                            const auto& live_avatar = avatars.at(endpoint);
                            auto& animations = avatar_animations[endpoint];
                            const auto initial_movement =
                                live_avatar.controller.movement_animation();
                            if (animations.empty()) {
                                if (const auto movement = homeworldz::viewer::parse_uuid(
                                        homeworldz::viewer::movement_animation_id(initial_movement)))
                                    animations.push_back({*movement, 1, identity->agent_id});
                                next_animation_sequences[endpoint] = 2;
                            }
                            movement_animations.insert_or_assign(
                                endpoint, initial_movement);
                            const auto initial_viewer_position = live_avatar.controller.viewer_position();
                            const std::array<float, 3> avatar_position{
                                static_cast<float>(initial_viewer_position.x),
                                static_cast<float>(initial_viewer_position.y),
                                static_cast<float>(initial_viewer_position.z)};
                            const auto new_avatar_update =
                                homeworldz::viewer::encode_avatar_object_update(
                                    response.region_handle,
                                    static_cast<std::uint32_t>(live_avatar.entity_id),
                                    identity->agent_id, avatar_position);
                            for (const auto& [recipient_endpoint, recipient] : avatars) {
                                if (const auto avatar = circuits.send(
                                        recipient_endpoint, new_avatar_update, true, now, true))
                                    static_cast<void>(send_udp(
                                        viewer_server, recipient_endpoint, *avatar));
                                if (recipient_endpoint == endpoint) continue;
                                const auto recipient_position = recipient.controller.viewer_position();
                                const auto recipient_id =
                                    homeworldz::viewer::parse_uuid(recipient.user_id);
                                if (!recipient_id) continue;
                                const auto existing_avatar_update =
                                    homeworldz::viewer::encode_avatar_object_update(
                                        response.region_handle,
                                        static_cast<std::uint32_t>(recipient.entity_id),
                                        *recipient_id,
                                        {static_cast<float>(recipient_position.x),
                                         static_cast<float>(recipient_position.y),
                                         static_cast<float>(recipient_position.z)},
                                        {static_cast<float>(recipient.controller.state().velocity.x),
                                         static_cast<float>(recipient.controller.state().velocity.y),
                                         static_cast<float>(recipient.controller.state().velocity.z)},
                                        recipient.controller.state().rotation);
                                if (const auto avatar = circuits.send(
                                        endpoint, existing_avatar_update, true, now, true))
                                    static_cast<void>(send_udp(viewer_server, endpoint, *avatar));
                            }
                            auto movement_complete =
                                homeworldz::viewer::encode_agent_movement_complete(response);
                            const bool arrival_seed_served = !arrival ||
                                capability_arrival_gate.consume_seed(session_id, arrival->id);
                            if (arrival_seed_served) {
                                if (const auto outgoing = circuits.send(
                                        endpoint, movement_complete, true, now))
                                    static_cast<void>(send_udp(viewer_server, endpoint, *outgoing));
                            } else {
                                pending_agent_movement_completes.push_back({
                                    endpoint, session_id, arrival->id, std::move(movement_complete),
                                    now + std::chrono::milliseconds(500)});
                            }
                            const homeworldz::viewer::AvatarAnimation animation_response{
                                identity->agent_id, animations};
                            const auto new_animation =
                                homeworldz::viewer::encode_avatar_animation(animation_response);
                            for (const auto& [recipient_endpoint, recipient] : avatars) {
                                static_cast<void>(recipient);
                                if (const auto outgoing = circuits.send(
                                        recipient_endpoint, new_animation, false, now))
                                    static_cast<void>(send_udp(
                                        viewer_server, recipient_endpoint, *outgoing));
                            }
                            for (const auto& [animation_endpoint, retained] : avatar_animations) {
                                if (animation_endpoint == endpoint || retained.empty()) continue;
                                const auto existing = avatars.find(animation_endpoint);
                                if (existing == avatars.end()) continue;
                                const auto sender_id =
                                    homeworldz::viewer::parse_uuid(existing->second.user_id);
                                if (!sender_id) continue;
                                const auto retained_animation =
                                    homeworldz::viewer::encode_avatar_animation(
                                        {*sender_id, retained});
                                if (const auto outgoing = circuits.send(
                                        endpoint, retained_animation, false, now))
                                    static_cast<void>(send_udp(viewer_server, endpoint, *outgoing));
                            }
                            for (const auto& [appearance_endpoint, retained] : avatar_appearances) {
                                if (appearance_endpoint == endpoint) continue;
                                const auto retained_appearance =
                                    homeworldz::viewer::encode_avatar_appearance({
                                        retained.agent_id, retained.serial, retained.texture_entry,
                                        retained.visual_params, {}, retained.appearance_version});
                                if (retained_appearance.empty()) continue;
                                if (const auto outgoing = circuits.send(
                                        endpoint, retained_appearance, true, now, true))
                                    static_cast<void>(send_udp(viewer_server, endpoint, *outgoing));
                            }
                            // If this avatar has not supplied an appearance yet,
                            // publish a server-side default-outfit bake so it
                            // rezzes immediately even if its client never bakes.
                            // A later AgentSetAppearance overrides this.
                            if (!avatar_appearances.contains(endpoint)) {
                                if (const auto* bake = ensure_default_outfit_bake()) {
                                    homeworldz::viewer::AgentSetAppearance seeded;
                                    seeded.agent_id = identity->agent_id;
                                    seeded.session_id = identity->session_id;
                                    seeded.serial = 1;
                                    seeded.texture_entry = bake->texture_entry;
                                    seeded.visual_params = default_outfit_visual_params;
                                    seeded.appearance_version = 1;
                                    avatar_appearances.insert_or_assign(endpoint, seeded);
                                    // LMV never sends AgentSetAppearance, so derive
                                    // the avatar's body geometry from the seeded
                                    // default shape too, or its physics capsule
                                    // keeps default dimensions (wrong height ->
                                    // bent-knee stance).
                                    if (const auto geometry =
                                            homeworldz::viewer::avatar_geometry(seeded)) {
                                        avatar_geometries[endpoint] = *geometry;
                                        if (const auto live = avatars.find(endpoint);
                                            live != avatars.end()) {
                                            live->second.controller.set_avatar_geometry(
                                                geometry->height, geometry->hip_offset);
                                            if (physics_world) {
                                                if (live->second.physics_character != 0)
                                                    physics_world->remove_character(
                                                        live->second.physics_character);
                                                live->second.physics_character =
                                                    physics_world->create_character(
                                                        {live->second.entity_id,
                                                         live->second.controller.state().position, 0.3,
                                                         geometry->height, 0.4});
                                                physics_world->set_character_flying(
                                                    live->second.physics_character,
                                                    live->second.controller.state().flying);
                                            }
                                        }
                                    }
                                    const auto seeded_appearance =
                                        homeworldz::viewer::encode_avatar_appearance({
                                            identity->agent_id, 1, bake->texture_entry,
                                            default_outfit_visual_params, {}, std::uint8_t{1}});
                                    // Send the default-outfit bake only to OTHER
                                    // avatars, never back to the joiner: a real
                                    // baker (e.g. Firestorm) would otherwise apply
                                    // this server-side (v1) default to itself and
                                    // lose its own local bake. Its own appearance
                                    // still overrides this entry for others when it
                                    // sends AgentSetAppearance.
                                    if (!seeded_appearance.empty())
                                        for (const auto& [recipient_endpoint, recipient] : avatars) {
                                            static_cast<void>(recipient);
                                            if (recipient_endpoint == endpoint) continue;
                                            if (const auto outgoing = circuits.send(
                                                    recipient_endpoint, seeded_appearance, true, now,
                                                    true))
                                                static_cast<void>(send_udp(
                                                    viewer_server, recipient_endpoint, *outgoing));
                                        }
                                    std::cout << "{\"level\":\"info\",\"message\":\"server default "
                                                 "appearance seeded on join\"}"
                                              << std::endl;
                                }
                            }
                            const auto terrain_patches_per_axis = terrain_width / 16;
                            constexpr std::size_t terrain_patches_per_packet = 16;
                            for (std::size_t y = 0; y < terrain_patches_per_axis; ++y)
                                for (std::size_t first_x = 0; first_x < terrain_patches_per_axis;
                                     first_x += terrain_patches_per_packet) {
                                    std::array<homeworldz::viewer::TerrainPatch,
                                        terrain_patches_per_packet> row{};
                                    const auto count = (std::min)(terrain_patches_per_packet,
                                        terrain_patches_per_axis - first_x);
                                    for (std::size_t index = 0; index < count; ++index)
                                        row[index] = {
                                            static_cast<std::uint8_t>(first_x + index),
                                            static_cast<std::uint8_t>(y)};
                                    const auto terrain_payload = homeworldz::viewer::encode_terrain(
                                        std::span<const homeworldz::viewer::TerrainPatch>(
                                            row.data(), count), *terrain_heightmap);
                                    if (const auto terrain = circuits.send(
                                            endpoint, terrain_payload, true, now))
                                        static_cast<void>(send_udp(viewer_server, endpoint, *terrain));
                                }
                            for (const auto& [entity_id, entity] : scene.entities()) {
                                static_cast<void>(entity_id);
                                const auto restored_object = static_object_from_entity(scene, entity, live_avatar.user_id, falcon);
                                if (!restored_object) continue;
                                if (const auto object = circuits.send(endpoint,
                                        homeworldz::viewer::encode_static_object_update(
                                            response.region_handle, *restored_object), true, now, true))
                                    static_cast<void>(send_udp(viewer_server, endpoint, *object));
                            }
                        }
                        const auto object_link = homeworldz::viewer::decode_object_link(packet->payload);
                        if (object_link && object_link->agent_id == identity->agent_id &&
                            object_link->session_id == identity->session_id) {
                            const auto user_id = homeworldz::viewer::format_uuid(identity->agent_id);
                            std::unordered_map<homeworldz::scene::EntityId, homeworldz::scene::Entity> originals;
                            std::vector<homeworldz::scene::EntityId> changed;
                            bool valid = object_link->local_ids.size() >= 2;
                            std::unordered_set<std::uint32_t> unique;
                            auto* root = valid ? scene.find(object_link->local_ids.front()) : nullptr;
                            valid = valid && root && root->parent_id == 0 && root->owner_id == user_id &&
                                (root->owner_permissions & homeworldz::scene::permission_modify) != 0;
                            if (valid) {
                                for (const auto local_id : object_link->local_ids) {
                                    auto* entity = scene.find(local_id);
                                    const bool has_children = std::any_of(
                                        scene.entities().begin(), scene.entities().end(),
                                        [local_id](const auto& entry) { return entry.second.parent_id == local_id; });
                                    if (!unique.insert(local_id).second || !entity || entity->parent_id != 0 ||
                                        has_children || entity->owner_id != user_id ||
                                        (entity->owner_permissions & homeworldz::scene::permission_modify) == 0) {
                                        valid = false;
                                        break;
                                    }
                                }
                            }
                            if (valid) {
                                for (std::size_t index = 1; index < object_link->local_ids.size(); ++index) {
                                    auto* child = scene.find(object_link->local_ids[index]);
                                    originals.emplace(child->id, *child);
                                    homeworldz::scene::establish_link(*child, *root);
                                    child->velocity = {};
                                    changed.push_back(child->id);
                                }
                                try {
                                    storage->save_snapshot(scene);
                                } catch (const std::exception& error) {
                                    valid = false;
                                    for (const auto& [entity_id, original] : originals)
                                        if (auto* entity = scene.find(entity_id)) *entity = original;
                                    std::cout << "{\"level\":\"error\",\"message\":\"linkset persistence failed\",\"error\":"
                                              << homeworldz::api::json_string(error.what()) << "}" << std::endl;
                                }
                            }
                            if (valid) {
                                const auto region_handle =
                                    (static_cast<std::uint64_t>(region_grid_x * 256) << 32) |
                                    static_cast<std::uint32_t>(region_grid_y * 256);
                                synchronize_physics_object(*root);
                                std::vector<homeworldz::scene::EntityId> updates{root->id};
                                updates.insert(updates.end(), changed.begin(), changed.end());
                                for (const auto& [recipient_endpoint, recipient] : avatars) {
                                    for (const auto entity_id : updates) {
                                        const auto* entity = scene.find(entity_id);
                                        const auto object = entity
                                            ? static_object_from_entity(scene, *entity, recipient.user_id, falcon) : std::nullopt;
                                        if (!object) continue;
                                        if (const auto sent = circuits.send(recipient_endpoint,
                                                homeworldz::viewer::encode_static_object_update(
                                                    region_handle, *object), true, now, true))
                                            static_cast<void>(send_udp(viewer_server, recipient_endpoint, *sent));
                                    }
                                }
                            }
                            std::cout << "{\"level\":" << (valid ? "\"info\"" : "\"warn\"")
                                      << ",\"message\":\"linkset creation "
                                      << (valid ? "completed" : "rejected") << "\",\"prims\":"
                                      << object_link->local_ids.size() << "}" << std::endl;
                        }
                        const auto object_delink = homeworldz::viewer::decode_object_delink(packet->payload);
                        if (object_delink && object_delink->agent_id == identity->agent_id &&
                            object_delink->session_id == identity->session_id) {
                            const auto user_id = homeworldz::viewer::format_uuid(identity->agent_id);
                            std::unordered_map<homeworldz::scene::EntityId, homeworldz::scene::Entity> originals;
                            std::vector<homeworldz::scene::EntityId> changed;
                            std::unordered_set<homeworldz::scene::EntityId> affected_roots;
                            for (const auto local_id : object_delink->local_ids) {
                                auto* entity = scene.find(local_id);
                                if (!entity || entity->parent_id == 0 || entity->owner_id != user_id ||
                                    (entity->owner_permissions & homeworldz::scene::permission_modify) == 0)
                                    continue;
                                originals.emplace(entity->id, *entity);
                                affected_roots.insert(entity->parent_id);
                                entity->parent_id = 0;
                                entity->local_position = {};
                                entity->local_rotation = {};
                                changed.push_back(entity->id);
                            }
                            bool persisted = false;
                            if (!changed.empty()) {
                                try {
                                    storage->save_snapshot(scene);
                                    persisted = true;
                                } catch (const std::exception& error) {
                                    for (const auto& [entity_id, original] : originals)
                                        if (auto* entity = scene.find(entity_id)) *entity = original;
                                    std::cout << "{\"level\":\"error\",\"message\":\"delink persistence failed\",\"error\":"
                                              << homeworldz::api::json_string(error.what()) << "}" << std::endl;
                                }
                            }
                            if (persisted) {
                                const auto region_handle =
                                    (static_cast<std::uint64_t>(region_grid_x * 256) << 32) |
                                    static_cast<std::uint32_t>(region_grid_y * 256);
                                for (const auto root_id : affected_roots)
                                    if (const auto* root = scene.find(root_id))
                                        synchronize_physics_object(*root);
                                for (const auto entity_id : changed) {
                                    const auto* entity = scene.find(entity_id);
                                    if (!entity) continue;
                                    synchronize_physics_object(*entity);
                                    for (const auto& [recipient_endpoint, recipient] : avatars) {
                                        const auto object = static_object_from_entity(scene, *entity, recipient.user_id, falcon);
                                        if (!object) continue;
                                        if (const auto sent = circuits.send(recipient_endpoint,
                                                homeworldz::viewer::encode_static_object_update(
                                                    region_handle, *object), true, now, true))
                                            static_cast<void>(send_udp(viewer_server, recipient_endpoint, *sent));
                                    }
                                }
                            }
                            std::cout << "{\"level\":" << (persisted ? "\"info\"" : "\"warn\"")
                                      << ",\"message\":\"linkset separation "
                                      << (persisted ? "completed" : "rejected") << "\",\"prims\":"
                                      << changed.size() << "}" << std::endl;
                        }
                        const auto object_select = homeworldz::viewer::decode_object_select(packet->payload);
                        if (object_select && object_select->agent_id == identity->agent_id &&
                            object_select->session_id == identity->session_id) {
                            std::vector<homeworldz::viewer::ObjectProperties> properties;
                            properties.reserve(object_select->local_ids.size());
                            const auto user_id = homeworldz::viewer::format_uuid(identity->agent_id);
                            for (const auto local_id : object_select->local_ids) {
                                const auto* entity = scene.find(local_id);
                                if (!entity) continue;
                                auto& selected = physics_edit_selections[endpoint];
                                if (entity->owner_id == user_id &&
                                    (entity->owner_permissions & homeworldz::scene::permission_modify) != 0 &&
                                    !selected.contains(entity->id)) {
                                    const auto* root = entity->parent_id == 0
                                        ? entity : scene.find(entity->parent_id);
                                    const auto physics_id = root && root->physical ? root->id : entity->id;
                                    selected.emplace(entity->id, physics_id);
                                    ++physics_edit_suspended[physics_id];
                                    if (const auto* physics_entity = scene.find(physics_id))
                                        synchronize_physics_object(*physics_entity);
                                }
                                if (const auto object = object_properties_from_entity(scene, *entity))
                                    properties.push_back(*object);
                            }
                            auto response = homeworldz::viewer::encode_object_properties(properties);
                            if (!response.empty()) {
                                if (const auto outgoing = circuits.send(
                                        endpoint, std::move(response), true, now, true))
                                    static_cast<void>(send_udp(viewer_server, endpoint, *outgoing));
                            }
                        }
                        const auto object_deselect =
                            homeworldz::viewer::decode_object_deselect(packet->payload);
                        if (object_deselect && object_deselect->agent_id == identity->agent_id &&
                            object_deselect->session_id == identity->session_id) {
                            for (const auto local_id : object_deselect->local_ids) {
                                const auto selected = physics_edit_selections.find(endpoint);
                                if (selected == physics_edit_selections.end())
                                    continue;
                                const auto selected_part = selected->second.find(local_id);
                                if (selected_part == selected->second.end()) continue;
                                const auto physics_id = selected_part->second;
                                selected->second.erase(selected_part);
                                const auto suspended = physics_edit_suspended.find(physics_id);
                                if (suspended != physics_edit_suspended.end() &&
                                    --suspended->second == 0) {
                                    physics_edit_suspended.erase(suspended);
                                    if (auto* entity = scene.find(physics_id)) {
                                        const auto original_position = entity->position;
                                        const auto original_velocity = entity->velocity;
                                        if (raise_physical_object_above_terrain(*entity)) {
                                            try {
                                                storage->save_snapshot(scene);
                                            } catch (const std::exception& error) {
                                                entity->position = original_position;
                                                entity->velocity = original_velocity;
                                                std::cerr << "{\"level\":\"error\",\"message\":\"terrain-safe physics reactivation persistence failed\",\"entityId\":"
                                                          << entity->id << ",\"error\":"
                                                          << homeworldz::api::json_string(error.what()) << "}" << std::endl;
                                            }
                                        }
                                        synchronize_physics_object(*entity);
                                    }
                                }
                            }
                        }
                        const auto grab =
                            homeworldz::viewer::decode_object_grab(packet->payload);
                        if (grab && grab->agent_id == identity->agent_id &&
                            grab->session_id == identity->session_id) {
                            // The initial touch (ObjectGrab), distinct from the
                            // physical drag path carried by ObjectGrabUpdate, is
                            // the only trigger for touch_start; drag motion must
                            // not synthesize duplicate touch events.
                            if (const auto* clicked = scene.find(grab->local_id)) {
                                std::size_t fired =
                                    falcon.dispatch_touch_start(clicked->object_id, 1);
                                const auto root_id = clicked->parent_id == 0
                                    ? clicked->id : clicked->parent_id;
                                if (root_id != clicked->id) {
                                    if (const auto* root = scene.find(root_id))
                                        fired += falcon.dispatch_touch_start(
                                            root->object_id, 1);
                                }
                                if (fired != 0)
                                    std::cerr << "{\"level\":\"info\",\"message\":\"Falcon touch_start dispatched\",\"localId\":"
                                              << grab->local_id << ",\"scripts\":" << fired
                                              << "}" << std::endl;
                            }
                        }
                        const auto grab_update =
                            homeworldz::viewer::decode_object_grab_update(packet->payload);
                        if (grab_update && grab_update->agent_id == identity->agent_id &&
                            grab_update->session_id == identity->session_id && physics_world &&
                            physics_scene) {
                            const auto object_id = homeworldz::viewer::format_uuid(grab_update->object_id);
                            const auto user_id = homeworldz::viewer::format_uuid(identity->agent_id);
                            for (const auto& [entity_id, clicked] : scene.entities()) {
                                if (clicked.object_id != object_id) continue;
                                const auto root_id = clicked.parent_id == 0 ? entity_id : clicked.parent_id;
                                const auto* entity = scene.find(root_id);
                                if (!entity) break;
                                const bool may_move = entity->owner_id == user_id ||
                                    (entity->everyone_permissions & homeworldz::scene::permission_move) != 0;
                                if (!may_move || !entity->physical || entity->phantom ||
                                    physics_edit_suspended.contains(root_id))
                                    break;
                                const auto body_id = physics_scene->body_id(root_id);
                                const auto state = physics_world->body_state(body_id);
                                if (!state) break;
                                constexpr double grab_response_seconds = 0.25;
                                constexpr double maximum_delta_speed = 10.0;
                                // Firestorm sends the desired world-space position of the
                                // originally clicked point, plus that point's offset from the
                                // object origin in object-local coordinates. Preserve the
                                // offset as the body rotates instead of pulling the origin to
                                // the surface point.
                                homeworldz::scene::Vector3 local_offset{
                                    grab_update->grab_offset_initial[0],
                                    grab_update->grab_offset_initial[1],
                                    grab_update->grab_offset_initial[2]};
                                if (clicked.parent_id != 0) {
                                    const auto squared = clicked.local_rotation.x * clicked.local_rotation.x +
                                        clicked.local_rotation.y * clicked.local_rotation.y +
                                        clicked.local_rotation.z * clicked.local_rotation.z;
                                    const std::array<double, 4> child_rotation{
                                        clicked.local_rotation.x, clicked.local_rotation.y,
                                        clicked.local_rotation.z,
                                        std::sqrt((std::max)(0.0, 1.0 - squared))};
                                    const auto rotated_offset = homeworldz::physics::rotate_vector(
                                        local_offset, child_rotation);
                                    local_offset = {
                                        clicked.local_position.x + rotated_offset.x,
                                        clicked.local_position.y + rotated_offset.y,
                                        clicked.local_position.z + rotated_offset.z};
                                }
                                const auto world_offset = homeworldz::physics::rotate_vector(
                                    local_offset, state->rotation);
                                const homeworldz::scene::Vector3 target_origin{
                                    grab_update->grab_position[0] - world_offset.x,
                                    grab_update->grab_position[1] - world_offset.y,
                                    grab_update->grab_position[2] - world_offset.z};
                                homeworldz::scene::Vector3 delta_velocity{
                                    (target_origin.x - state->position.x) /
                                        grab_response_seconds - state->linear_velocity.x,
                                    (target_origin.y - state->position.y) /
                                        grab_response_seconds - state->linear_velocity.y,
                                    (target_origin.z - state->position.z) /
                                        grab_response_seconds - state->linear_velocity.z};
                                const auto delta_speed = std::sqrt(
                                    delta_velocity.x * delta_velocity.x +
                                    delta_velocity.y * delta_velocity.y +
                                    delta_velocity.z * delta_velocity.z);
                                if (delta_speed > maximum_delta_speed) {
                                    const auto scale = maximum_delta_speed / delta_speed;
                                    delta_velocity.x *= scale;
                                    delta_velocity.y *= scale;
                                    delta_velocity.z *= scale;
                                }
                                const auto mass = homeworldz::physics::linkset_mass(scene, *entity);
                                physics_world->apply_impulse(body_id, {
                                    delta_velocity.x * mass, delta_velocity.y * mass,
                                    delta_velocity.z * mass});
                                break;
                            }
                        }
                        const auto family_request =
                            homeworldz::viewer::decode_request_object_properties_family(packet->payload);
                        if (family_request && family_request->agent_id == identity->agent_id &&
                            family_request->session_id == identity->session_id) {
                            const auto requested_id = homeworldz::viewer::format_uuid(family_request->object_id);
                            for (const auto& [entity_id, entity] : scene.entities()) {
                                static_cast<void>(entity_id);
                                if (entity.object_id != requested_id) continue;
                                const auto properties = object_properties_from_entity(scene, entity);
                                if (properties) {
                                    auto response = homeworldz::viewer::encode_object_properties_family(
                                        family_request->request_flags, *properties);
                                    if (const auto outgoing = circuits.send(
                                            endpoint, std::move(response), true, now, true))
                                        static_cast<void>(send_udp(viewer_server, endpoint, *outgoing));
                                }
                                break;
                            }
                        }
                        const auto transform_update =
                            homeworldz::viewer::decode_multiple_object_update(packet->payload);
                        if (transform_update && transform_update->agent_id == identity->agent_id &&
                            transform_update->session_id == identity->session_id) {
                            std::unordered_map<homeworldz::scene::EntityId, homeworldz::scene::Entity> originals;
                            std::unordered_set<homeworldz::scene::EntityId> requested_entities;
                            std::unordered_set<homeworldz::scene::EntityId> changed_roots;
                            std::unordered_set<homeworldz::scene::EntityId> changed_children;
                            std::unordered_set<homeworldz::scene::EntityId> changed_root_frames;
                            std::unordered_map<homeworldz::scene::EntityId,
                                homeworldz::scene::Vector3> linked_scale_factors;
                            const auto user_id = homeworldz::viewer::format_uuid(identity->agent_id);
                            for (const auto& update : transform_update->objects) {
                                auto* entity = scene.find(update.local_id);
                                if (!entity) continue;
                                requested_entities.insert(entity->id);
                                if (entity->owner_id != user_id ||
                                    (entity->owner_permissions & homeworldz::scene::permission_modify) == 0)
                                    continue;
                                const auto finite_vector = [](const std::array<float, 3>& value) {
                                    return std::all_of(value.begin(), value.end(),
                                        [](float component) { return std::isfinite(component); });
                                };
                                const bool valid_position = !update.position ||
                                    (finite_vector(*update.position) &&
                                     (entity->parent_id != 0
                                         ? std::all_of(update.position->begin(), update.position->end(),
                                               [](float component) {
                                                   return component >= -4096.0F && component <= 4096.0F;
                                               })
                                         : ((*update.position)[0] >= 0.0F &&
                                            (*update.position)[0] <= static_cast<float>(region_size_x) &&
                                            (*update.position)[1] >= 0.0F &&
                                            (*update.position)[1] <= static_cast<float>(region_size_y) &&
                                            (*update.position)[2] >= -64.0F && (*update.position)[2] <= 4096.0F)));
                                const bool valid_rotation = !update.rotation ||
                                    (finite_vector(*update.rotation) &&
                                     std::all_of(update.rotation->begin(), update.rotation->end(),
                                         [](float component) { return component >= -1.0F && component <= 1.0F; }) &&
                                     ((*update.rotation)[0] * (*update.rotation)[0] +
                                      (*update.rotation)[1] * (*update.rotation)[1] +
                                      (*update.rotation)[2] * (*update.rotation)[2]) <= 1.001F);
                                const bool valid_scale = !update.scale ||
                                    (finite_vector(*update.scale) &&
                                     std::all_of(update.scale->begin(), update.scale->end(),
                                         [](float component) { return component >= 0.01F && component <= 64.0F; }));
                                if (!valid_position || !valid_rotation || !valid_scale) continue;
                                if (!update.position && !update.rotation && !update.scale) continue;
                                originals.try_emplace(entity->id, *entity);
                                const bool linked_update = (update.type & 0x08) != 0;
                                auto& position = entity->parent_id == 0
                                    ? entity->position : entity->local_position;
                                auto& rotation = entity->parent_id == 0
                                    ? entity->rotation : entity->local_rotation;
                                if (update.position)
                                    position = {(*update.position)[0], (*update.position)[1],
                                                (*update.position)[2]};
                                if (update.rotation)
                                    rotation = {(*update.rotation)[0], (*update.rotation)[1],
                                                (*update.rotation)[2]};
                                if (update.scale && entity->parent_id == 0 && linked_update) {
                                    homeworldz::scene::Vector3 factors{
                                        (*update.scale)[0] / entity->scale.x,
                                        (*update.scale)[1] / entity->scale.y,
                                        (*update.scale)[2] / entity->scale.z};
                                    for (const auto& [candidate_id, candidate] : scene.entities()) {
                                        static_cast<void>(candidate_id);
                                        if (candidate.parent_id != entity->id) continue;
                                        factors.x = std::clamp(
                                            factors.x, 0.01 / candidate.scale.x, 64.0 / candidate.scale.x);
                                        factors.y = std::clamp(
                                            factors.y, 0.01 / candidate.scale.y, 64.0 / candidate.scale.y);
                                        factors.z = std::clamp(
                                            factors.z, 0.01 / candidate.scale.z, 64.0 / candidate.scale.z);
                                    }
                                    entity->scale = {
                                        entity->scale.x * factors.x,
                                        entity->scale.y * factors.y,
                                        entity->scale.z * factors.z};
                                    linked_scale_factors.emplace(entity->id, factors);
                                } else if (update.scale) {
                                    entity->scale = {
                                        (*update.scale)[0], (*update.scale)[1], (*update.scale)[2]};
                                }
                                if (entity->parent_id == 0) {
                                    if (linked_update) {
                                        if (update.position || update.rotation || update.scale)
                                            changed_roots.insert(entity->id);
                                    } else if (update.position || update.rotation) {
                                        changed_root_frames.insert(entity->id);
                                    }
                                } else {
                                    changed_children.insert(entity->id);
                                }
                            }
                            for (const auto& [entity_id, current] : scene.entities()) {
                                if (current.parent_id == 0) continue;
                                const auto* root = scene.find(current.parent_id);
                                auto* child = scene.find(entity_id);
                                if (!root || !child) continue;
                                if (changed_root_frames.contains(current.parent_id)) {
                                    originals.try_emplace(child->id, *child);
                                    homeworldz::scene::establish_link(*child, *root);
                                    requested_entities.insert(child->id);
                                }
                                const auto scaled = linked_scale_factors.find(current.parent_id);
                                if (scaled != linked_scale_factors.end()) {
                                    originals.try_emplace(child->id, *child);
                                    homeworldz::scene::scale_linked_child(*child, scaled->second);
                                    requested_entities.insert(child->id);
                                }
                            }
                            for (const auto& [entity_id, current] : scene.entities()) {
                                if (current.parent_id == 0 ||
                                    (!changed_roots.contains(current.parent_id) &&
                                     !changed_children.contains(entity_id)))
                                    continue;
                                const auto* root = scene.find(current.parent_id);
                                auto* child = scene.find(entity_id);
                                if (!root || !child) continue;
                                originals.try_emplace(child->id, *child);
                                homeworldz::scene::update_linked_world_transform(*child, *root);
                                requested_entities.insert(child->id);
                            }
                            bool persisted = false;
                            if (!originals.empty()) {
                                try {
                                    storage->save_snapshot(scene);
                                    persisted = true;
                                } catch (const std::exception& error) {
                                    for (const auto& [entity_id, original] : originals)
                                        if (auto* entity = scene.find(entity_id)) *entity = original;
                                    std::cout << "{\"level\":\"error\",\"message\":\"primitive update persistence failed\",\"error\":"
                                              << homeworldz::api::json_string(error.what()) << "}" << std::endl;
                                }
                            }
                            const auto region_handle =
                                (static_cast<std::uint64_t>(region_grid_x * 256) << 32) |
                                static_cast<std::uint32_t>(region_grid_y * 256);
                            for (const auto entity_id : requested_entities) {
                                const auto* entity = scene.find(entity_id);
                                if (!entity) continue;
                                if (persisted) synchronize_physics_object(*entity);
                                for (const auto& [recipient_endpoint, recipient] : avatars) {
                                    const auto object = static_object_from_entity(scene, *entity, recipient.user_id, falcon);
                                    if (!object) continue;
                                    if (const auto sent = circuits.send(recipient_endpoint,
                                            homeworldz::viewer::encode_static_object_update(
                                                region_handle, *object), true, now, true))
                                        static_cast<void>(send_udp(viewer_server, recipient_endpoint, *sent));
                                }
                            }
                            if (persisted) {
                                std::cout << "{\"level\":\"info\",\"message\":\"primitive transforms updated\",\"count\":"
                                          << originals.size() << "}" << std::endl;
                            }
                        }
                        const auto object_name = homeworldz::viewer::decode_object_name(packet->payload);
                        if (object_name && object_name->agent_id == identity->agent_id &&
                            object_name->session_id == identity->session_id) {
                            std::unordered_map<homeworldz::scene::EntityId, std::string> original_names;
                            std::unordered_set<homeworldz::scene::EntityId> requested_entities;
                            const auto user_id = homeworldz::viewer::format_uuid(identity->agent_id);
                            for (const auto& update : object_name->objects) {
                                auto* entity = scene.find(update.local_id);
                                if (!entity) continue;
                                requested_entities.insert(entity->id);
                                if (entity->owner_id != user_id ||
                                    (entity->owner_permissions & homeworldz::scene::permission_modify) == 0)
                                    continue;
                                original_names.try_emplace(entity->id, entity->name);
                                entity->name = update.name;
                            }
                            bool persisted = false;
                            if (!original_names.empty()) {
                                try {
                                    storage->save_snapshot(scene);
                                    persisted = true;
                                } catch (const std::exception& error) {
                                    for (const auto& [entity_id, original_name] : original_names) {
                                        if (auto* entity = scene.find(entity_id)) entity->name = original_name;
                                    }
                                    std::cout << "{\"level\":\"error\",\"message\":\"primitive name persistence failed\",\"error\":"
                                              << homeworldz::api::json_string(error.what()) << "}" << std::endl;
                                }
                            }
                            std::vector<homeworldz::viewer::ObjectProperties> properties;
                            properties.reserve(requested_entities.size());
                            for (const auto entity_id : requested_entities) {
                                if (const auto* entity = scene.find(entity_id)) {
                                    if (const auto current = object_properties_from_entity(scene, *entity))
                                        properties.push_back(*current);
                                }
                            }
                            auto response = homeworldz::viewer::encode_object_properties(properties);
                            if (!response.empty()) {
                                if (const auto outgoing = circuits.send(
                                        endpoint, std::move(response), true, now, true))
                                    static_cast<void>(send_udp(viewer_server, endpoint, *outgoing));
                            }
                            if (persisted) {
                                std::cout << "{\"level\":\"info\",\"message\":\"primitive names updated\",\"count\":"
                                          << original_names.size() << "}" << std::endl;
                            }
                        }
                        const auto object_description =
                            homeworldz::viewer::decode_object_description(packet->payload);
                        if (object_description && object_description->agent_id == identity->agent_id &&
                            object_description->session_id == identity->session_id) {
                            std::unordered_map<homeworldz::scene::EntityId, std::string> original_descriptions;
                            std::unordered_set<homeworldz::scene::EntityId> requested_entities;
                            const auto user_id = homeworldz::viewer::format_uuid(identity->agent_id);
                            for (const auto& update : object_description->objects) {
                                auto* entity = scene.find(update.local_id);
                                if (!entity) continue;
                                requested_entities.insert(entity->id);
                                if (entity->owner_id != user_id ||
                                    (entity->owner_permissions & homeworldz::scene::permission_modify) == 0)
                                    continue;
                                original_descriptions.try_emplace(entity->id, entity->description);
                                entity->description = update.description;
                            }
                            bool persisted = false;
                            if (!original_descriptions.empty()) {
                                try {
                                    storage->save_snapshot(scene);
                                    persisted = true;
                                } catch (const std::exception& error) {
                                    for (const auto& [entity_id, original_description] : original_descriptions) {
                                        if (auto* entity = scene.find(entity_id))
                                            entity->description = original_description;
                                    }
                                    std::cout << "{\"level\":\"error\",\"message\":\"primitive description persistence failed\",\"error\":"
                                              << homeworldz::api::json_string(error.what()) << "}" << std::endl;
                                }
                            }
                            std::vector<homeworldz::viewer::ObjectProperties> properties;
                            properties.reserve(requested_entities.size());
                            for (const auto entity_id : requested_entities) {
                                if (const auto* entity = scene.find(entity_id)) {
                                    if (const auto current = object_properties_from_entity(scene, *entity))
                                        properties.push_back(*current);
                                }
                            }
                            auto response = homeworldz::viewer::encode_object_properties(properties);
                            if (!response.empty()) {
                                if (const auto outgoing = circuits.send(
                                        endpoint, std::move(response), true, now, true))
                                    static_cast<void>(send_udp(viewer_server, endpoint, *outgoing));
                            }
                            if (persisted) {
                                std::cout << "{\"level\":\"info\",\"message\":\"primitive descriptions updated\",\"count\":"
                                          << original_descriptions.size() << "}" << std::endl;
                            }
                        }
                        const auto object_permissions =
                            homeworldz::viewer::decode_object_permissions(packet->payload);
                        if (object_permissions && object_permissions->agent_id == identity->agent_id &&
                            object_permissions->session_id == identity->session_id) {
                            struct PermissionState {
                                std::uint32_t owner;
                                std::uint32_t group;
                                std::uint32_t everyone;
                                std::uint32_t next_owner;
                            };
                            std::unordered_map<homeworldz::scene::EntityId, PermissionState> originals;
                            std::unordered_set<homeworldz::scene::EntityId> requested_entities;
                            const auto user_id = homeworldz::viewer::format_uuid(identity->agent_id);
                            if (!object_permissions->override_permissions) {
                                for (const auto& update : object_permissions->objects) {
                                    auto* entity = scene.find(update.local_id);
                                    if (!entity) continue;
                                    requested_entities.insert(entity->id);
                                    const PermissionState before{
                                        entity->owner_permissions, entity->group_permissions,
                                        entity->everyone_permissions, entity->next_owner_permissions};
                                    if (!homeworldz::scene::apply_permission_update(
                                            *entity, user_id, update.field, update.set, update.mask))
                                        continue;
                                    const PermissionState after{
                                        entity->owner_permissions, entity->group_permissions,
                                        entity->everyone_permissions, entity->next_owner_permissions};
                                    if (before.owner != after.owner || before.group != after.group ||
                                        before.everyone != after.everyone ||
                                        before.next_owner != after.next_owner)
                                        originals.try_emplace(entity->id, before);
                                }
                            }
                            bool persisted = false;
                            if (!originals.empty()) {
                                try {
                                    storage->save_snapshot(scene);
                                    persisted = true;
                                } catch (const std::exception& error) {
                                    for (const auto& [entity_id, original] : originals) {
                                        if (auto* entity = scene.find(entity_id)) {
                                            entity->owner_permissions = original.owner;
                                            entity->group_permissions = original.group;
                                            entity->everyone_permissions = original.everyone;
                                            entity->next_owner_permissions = original.next_owner;
                                        }
                                    }
                                    std::cout << "{\"level\":\"error\",\"message\":\"primitive permission persistence failed\",\"error\":"
                                              << homeworldz::api::json_string(error.what()) << "}" << std::endl;
                                }
                            }
                            std::vector<homeworldz::viewer::ObjectProperties> properties;
                            properties.reserve(requested_entities.size());
                            for (const auto entity_id : requested_entities) {
                                if (const auto* entity = scene.find(entity_id)) {
                                    if (const auto current = object_properties_from_entity(scene, *entity))
                                        properties.push_back(*current);
                                }
                            }
                            auto response = homeworldz::viewer::encode_object_properties(properties);
                            if (!response.empty()) {
                                if (const auto outgoing = circuits.send(
                                        endpoint, std::move(response), true, now, true))
                                    static_cast<void>(send_udp(viewer_server, endpoint, *outgoing));
                            }
                            if (persisted) {
                                const auto region_handle =
                                    (static_cast<std::uint64_t>(region_grid_x * 256) << 32) |
                                    static_cast<std::uint32_t>(region_grid_y * 256);
                                for (const auto& [entity_id, original] : originals) {
                                    static_cast<void>(original);
                                    const auto* entity = scene.find(entity_id);
                                    if (!entity) continue;
                                    for (const auto& [recipient_endpoint, recipient] : avatars) {
                                        const auto object = static_object_from_entity(scene, *entity, recipient.user_id, falcon);
                                        if (!object) continue;
                                        if (const auto sent = circuits.send(recipient_endpoint,
                                                homeworldz::viewer::encode_static_object_update(
                                                    region_handle, *object), true, now, true))
                                            static_cast<void>(send_udp(
                                                viewer_server, recipient_endpoint, *sent));
                                    }
                                }
                                std::cout << "{\"level\":\"info\",\"message\":\"primitive permissions updated\",\"count\":"
                                          << originals.size() << "}" << std::endl;
                            }
                        }
                        const auto object_duplicate =
                            homeworldz::viewer::decode_object_duplicate(packet->payload);
                        if (object_duplicate && object_duplicate->agent_id == identity->agent_id &&
                            object_duplicate->session_id == identity->session_id) {
                            constexpr std::uint32_t create_selected = 0x00000002;
                            const auto finite_offset = std::all_of(
                                object_duplicate->offset.begin(), object_duplicate->offset.end(),
                                [](float component) { return std::isfinite(component); });
                            const auto supported_flags =
                                (object_duplicate->duplicate_flags & ~create_selected) == 0;
                            const auto user_id = homeworldz::viewer::format_uuid(identity->agent_id);
                            std::vector<homeworldz::scene::EntityId> created_entities;
                            std::unordered_set<homeworldz::scene::EntityId> requested_roots;
                            if (finite_offset && supported_flags) {
                                for (const auto local_id : object_duplicate->local_ids) {
                                    const auto* selected = scene.find(local_id);
                                    if (!selected) continue;
                                    const auto root_id = selected->parent_id != 0
                                        ? selected->parent_id : selected->id;
                                    if (!requested_roots.insert(root_id).second) continue;
                                    const auto* source = scene.find(root_id);
                                    if (!source || source->owner_id != user_id)
                                        continue;
                                    std::vector<const homeworldz::scene::Entity*> source_children;
                                    bool permitted = true;
                                    for (const auto& [candidate_id, candidate] : scene.entities()) {
                                        static_cast<void>(candidate_id);
                                        if (candidate.parent_id != root_id) continue;
                                        if (candidate.owner_id != user_id) {
                                            permitted = false;
                                            break;
                                        }
                                        source_children.push_back(&candidate);
                                    }
                                    const auto folded = homeworldz::scene::effective_permissions(scene, *source);
                                    if (!permitted ||
                                        (folded.owner & homeworldz::scene::permission_copy) == 0)
                                        continue;
                                    const auto source_copy = *source;
                                    const homeworldz::scene::Vector3 position{
                                        source_copy.position.x + object_duplicate->offset[0],
                                        source_copy.position.y + object_duplicate->offset[1],
                                        source_copy.position.z + object_duplicate->offset[2]};
                                    if (position.x < 0.0 || position.x > region_size_x ||
                                        position.y < 0.0 || position.y > region_size_y ||
                                        position.z < -64.0 || position.z > 4096.0)
                                        continue;
                                    const auto duplicate_root_id = scene.create(source_copy.name, position);
                                    auto* duplicate = scene.find(duplicate_root_id);
                                    if (!duplicate) continue;
                                    *duplicate = source_copy;
                                    duplicate->id = duplicate_root_id;
                                    duplicate->parent_id = 0;
                                    duplicate->local_position = {};
                                    duplicate->local_rotation = {};
                                    duplicate->position = position;
                                    duplicate->velocity = {};
                                    duplicate->object_id = homeworldz::viewer::random_uuid();
                                    regenerate_task_inventory_item_ids(*duplicate);
                                    duplicate->creation_date = static_cast<std::uint64_t>(
                                        std::chrono::duration_cast<std::chrono::seconds>(
                                            std::chrono::system_clock::now().time_since_epoch()).count());
                                    created_entities.push_back(duplicate_root_id);
                                    for (const auto* source_child : source_children) {
                                        const auto child_id = scene.create(source_child->name);
                                        auto* child = scene.find(child_id);
                                        if (!child) continue;
                                        *child = *source_child;
                                        child->id = child_id;
                                        child->parent_id = duplicate_root_id;
                                        child->velocity = {};
                                        child->object_id = homeworldz::viewer::random_uuid();
                                        regenerate_task_inventory_item_ids(*child);
                                        child->creation_date = duplicate->creation_date;
                                        homeworldz::scene::update_linked_world_transform(*child, *duplicate);
                                        created_entities.push_back(child_id);
                                    }
                                }
                            }
                            bool persisted = false;
                            if (!created_entities.empty()) {
                                try {
                                    storage->save_snapshot(scene);
                                    persisted = true;
                                } catch (const std::exception& error) {
                                    for (const auto entity_id : created_entities) scene.remove(entity_id);
                                    std::cout << "{\"level\":\"error\",\"message\":\"primitive duplication persistence failed\",\"error\":"
                                              << homeworldz::api::json_string(error.what()) << "}" << std::endl;
                                }
                            }
                            if (persisted) {
                                const auto region_handle =
                                    (static_cast<std::uint64_t>(region_grid_x * 256) << 32) |
                                    static_cast<std::uint32_t>(region_grid_y * 256);
                                for (const auto entity_id : created_entities) {
                                    const auto* entity = scene.find(entity_id);
                                    if (!entity) continue;
                                    synchronize_physics_object(*entity);
                                    for (const auto& [recipient_endpoint, recipient] : avatars) {
                                        auto object = static_object_from_entity(scene, *entity, recipient.user_id, falcon);
                                        if (!object) continue;
                                        if (recipient_endpoint == endpoint)
                                            object->update_flags |=
                                                object_duplicate->duplicate_flags & create_selected;
                                        if (const auto sent = circuits.send(recipient_endpoint,
                                                homeworldz::viewer::encode_static_object_update(
                                                    region_handle, *object), true, now, true))
                                            static_cast<void>(send_udp(
                                                viewer_server, recipient_endpoint, *sent));
                                    }
                                }
                                std::cout << "{\"level\":\"info\",\"message\":\"primitives duplicated\",\"count\":"
                                          << created_entities.size() << "}" << std::endl;
                            }
                        }
                        const auto object_material =
                            homeworldz::viewer::decode_object_material(packet->payload);
                        if (object_material && object_material->agent_id == identity->agent_id &&
                            object_material->session_id == identity->session_id) {
                            constexpr std::uint8_t last_supported_material = 0x07;
                            struct OriginalMaterial {
                                std::uint8_t material{};
                                double friction{};
                                double restitution{};
                            };
                            std::unordered_map<homeworldz::scene::EntityId, OriginalMaterial> originals;
                            std::unordered_set<homeworldz::scene::EntityId> requested_entities;
                            const auto user_id = homeworldz::viewer::format_uuid(identity->agent_id);
                            for (const auto& update : object_material->objects) {
                                auto* entity = scene.find(update.local_id);
                                if (!entity) continue;
                                requested_entities.insert(entity->id);
                                if (update.material > last_supported_material ||
                                    entity->owner_id != user_id ||
                                    (entity->owner_permissions & homeworldz::scene::permission_modify) == 0 ||
                                    entity->material == update.material)
                                    continue;
                                originals.try_emplace(entity->id, OriginalMaterial{
                                    entity->material, entity->physics_friction,
                                    entity->physics_restitution});
                                entity->material = update.material;
                                apply_material_contact_defaults(*entity);
                            }
                            bool persisted = false;
                            if (!originals.empty()) {
                                try {
                                    storage->save_snapshot(scene);
                                    persisted = true;
                                } catch (const std::exception& error) {
                                    for (const auto& [entity_id, original] : originals) {
                                        if (auto* entity = scene.find(entity_id)) {
                                            entity->material = original.material;
                                            entity->physics_friction = original.friction;
                                            entity->physics_restitution = original.restitution;
                                        }
                                    }
                                    std::cout << "{\"level\":\"error\",\"message\":\"primitive material persistence failed\",\"error\":"
                                              << homeworldz::api::json_string(error.what()) << "}" << std::endl;
                                }
                            }
                            const auto region_handle =
                                (static_cast<std::uint64_t>(region_grid_x * 256) << 32) |
                                static_cast<std::uint32_t>(region_grid_y * 256);
                            for (const auto entity_id : requested_entities) {
                                const auto* entity = scene.find(entity_id);
                                if (!entity) continue;
                                for (const auto& [recipient_endpoint, recipient] : avatars) {
                                    const auto object = static_object_from_entity(scene, *entity, recipient.user_id, falcon);
                                    if (!object) continue;
                                    if (const auto sent = circuits.send(recipient_endpoint,
                                            homeworldz::viewer::encode_static_object_update(
                                                region_handle, *object), true, now, true))
                                        static_cast<void>(send_udp(
                                            viewer_server, recipient_endpoint, *sent));
                                }
                            }
                            if (persisted) {
                                for (const auto& [entity_id, original] : originals) {
                                    static_cast<void>(original);
                                    if (const auto* entity = scene.find(entity_id))
                                        synchronize_physics_object(*entity);
                                }
                                std::cout << "{\"level\":\"info\",\"message\":\"primitive materials updated\",\"count\":"
                                          << originals.size() << "}" << std::endl;
                            }
                        }
                        const auto object_shape =
                            homeworldz::viewer::decode_object_shape(packet->payload);
                        if (object_shape && object_shape->agent_id == identity->agent_id &&
                            object_shape->session_id == identity->session_id) {
                            std::unordered_map<homeworldz::scene::EntityId,
                                homeworldz::scene::Entity> originals;
                            std::unordered_set<homeworldz::scene::EntityId> requested_entities;
                            const auto user_id = homeworldz::viewer::format_uuid(identity->agent_id);
                            const auto apply_shape = [](homeworldz::scene::Entity& entity,
                                const homeworldz::viewer::ObjectShapeUpdate& update) {
                                entity.path_curve = update.path_curve;
                                entity.profile_curve = update.profile_curve;
                                entity.path_begin = update.path_begin;
                                entity.path_end = update.path_end;
                                entity.path_scale_x = update.path_scale_x;
                                entity.path_scale_y = update.path_scale_y;
                                entity.path_shear_x = update.path_shear_x;
                                entity.path_shear_y = update.path_shear_y;
                                entity.path_twist = update.path_twist;
                                entity.path_twist_begin = update.path_twist_begin;
                                entity.path_radius_offset = update.path_radius_offset;
                                entity.path_taper_x = update.path_taper_x;
                                entity.path_taper_y = update.path_taper_y;
                                entity.path_revolutions = update.path_revolutions;
                                entity.path_skew = update.path_skew;
                                entity.profile_begin = update.profile_begin;
                                entity.profile_end = update.profile_end;
                                entity.profile_hollow = update.profile_hollow;
                            };
                            const auto shape_changed = [](const homeworldz::scene::Entity& entity,
                                const homeworldz::viewer::ObjectShapeUpdate& update) {
                                return entity.path_curve != update.path_curve ||
                                    entity.profile_curve != update.profile_curve ||
                                    entity.path_begin != update.path_begin ||
                                    entity.path_end != update.path_end ||
                                    entity.path_scale_x != update.path_scale_x ||
                                    entity.path_scale_y != update.path_scale_y ||
                                    entity.path_shear_x != update.path_shear_x ||
                                    entity.path_shear_y != update.path_shear_y ||
                                    entity.path_twist != update.path_twist ||
                                    entity.path_twist_begin != update.path_twist_begin ||
                                    entity.path_radius_offset != update.path_radius_offset ||
                                    entity.path_taper_x != update.path_taper_x ||
                                    entity.path_taper_y != update.path_taper_y ||
                                    entity.path_revolutions != update.path_revolutions ||
                                    entity.path_skew != update.path_skew ||
                                    entity.profile_begin != update.profile_begin ||
                                    entity.profile_end != update.profile_end ||
                                    entity.profile_hollow != update.profile_hollow;
                            };
                            for (const auto& update : object_shape->objects) {
                                auto* entity = scene.find(update.local_id);
                                if (!entity) continue;
                                requested_entities.insert(entity->id);
                                // Same well-formedness gate as ObjectAdd: a recognized
                                // path curve and profile curve keep every basic shape
                                // and its edited variations while rejecting garbage.
                                const bool valid_path_curve = update.path_curve == 0x10 ||
                                    update.path_curve == 0x20 || update.path_curve == 0x30;
                                const bool valid_profile_curve = (update.profile_curve & 0x0f) <= 0x05;
                                if (!valid_path_curve || !valid_profile_curve ||
                                    entity->owner_id != user_id ||
                                    (entity->owner_permissions & homeworldz::scene::permission_modify) == 0 ||
                                    !shape_changed(*entity, update))
                                    continue;
                                originals.try_emplace(entity->id, *entity);
                                apply_shape(*entity, update);
                            }
                            bool persisted = false;
                            if (!originals.empty()) {
                                try {
                                    storage->save_snapshot(scene);
                                    persisted = true;
                                } catch (const std::exception& error) {
                                    for (const auto& [entity_id, original] : originals)
                                        if (auto* entity = scene.find(entity_id))
                                            *entity = original;
                                    std::cout << "{\"level\":\"error\",\"message\":\"primitive shape persistence failed\",\"error\":"
                                              << homeworldz::api::json_string(error.what()) << "}" << std::endl;
                                }
                            }
                            const auto region_handle =
                                (static_cast<std::uint64_t>(region_grid_x * 256) << 32) |
                                static_cast<std::uint32_t>(region_grid_y * 256);
                            for (const auto entity_id : requested_entities) {
                                const auto* entity = scene.find(entity_id);
                                if (!entity) continue;
                                for (const auto& [recipient_endpoint, recipient] : avatars) {
                                    const auto object = static_object_from_entity(scene, *entity, recipient.user_id, falcon);
                                    if (!object) continue;
                                    if (const auto sent = circuits.send(recipient_endpoint,
                                            homeworldz::viewer::encode_static_object_update(
                                                region_handle, *object), true, now, true))
                                        static_cast<void>(send_udp(
                                            viewer_server, recipient_endpoint, *sent));
                                }
                            }
                            if (persisted) {
                                for (const auto& [entity_id, original] : originals) {
                                    static_cast<void>(original);
                                    if (const auto* entity = scene.find(entity_id))
                                        synchronize_physics_object(*entity);
                                }
                                std::cout << "{\"level\":\"info\",\"message\":\"primitive shapes updated\",\"count\":"
                                          << originals.size() << "}" << std::endl;
                            }
                        }
                        const auto object_image =
                            homeworldz::viewer::decode_object_image(packet->payload);
                        if (object_image && object_image->agent_id == identity->agent_id &&
                            object_image->session_id == identity->session_id) {
                            std::unordered_map<homeworldz::scene::EntityId, std::vector<std::byte>> originals;
                            std::unordered_set<homeworldz::scene::EntityId> requested_entities;
                            const auto user_id = homeworldz::viewer::format_uuid(identity->agent_id);
                            for (const auto& update : object_image->objects) {
                                auto* entity = scene.find(update.local_id);
                                if (!entity) continue;
                                requested_entities.insert(entity->id);
                                auto texture_entry = update.texture_entry;
                                homeworldz::viewer::normalize_primitive_texture_entry(
                                    texture_entry, default_prim_texture_entry());
                                if (entity->owner_id != user_id ||
                                    (entity->owner_permissions & homeworldz::scene::permission_modify) == 0 ||
                                    entity->texture_entry == texture_entry)
                                    continue;
                                originals.try_emplace(entity->id, entity->texture_entry);
                                entity->texture_entry = std::move(texture_entry);
                            }
                            bool persisted = false;
                            if (!originals.empty()) {
                                try {
                                    storage->save_snapshot(scene);
                                    persisted = true;
                                } catch (const std::exception& error) {
                                    for (auto& [entity_id, texture_entry] : originals)
                                        if (auto* entity = scene.find(entity_id))
                                            entity->texture_entry = std::move(texture_entry);
                                    std::cout << "{\"level\":\"error\",\"message\":\"primitive texture persistence failed\",\"error\":"
                                              << homeworldz::api::json_string(error.what()) << "}" << std::endl;
                                }
                            }
                            const auto region_handle =
                                (static_cast<std::uint64_t>(region_grid_x * 256) << 32) |
                                static_cast<std::uint32_t>(region_grid_y * 256);
                            for (const auto entity_id : requested_entities) {
                                const auto* entity = scene.find(entity_id);
                                if (!entity) continue;
                                for (const auto& [recipient_endpoint, recipient] : avatars) {
                                    const auto object = static_object_from_entity(scene, *entity, recipient.user_id, falcon);
                                    if (!object) continue;
                                    if (const auto sent = circuits.send(recipient_endpoint,
                                            homeworldz::viewer::encode_static_object_update(
                                                region_handle, *object), true, now, true))
                                        static_cast<void>(send_udp(
                                            viewer_server, recipient_endpoint, *sent));
                                }
                            }
                            if (persisted)
                                std::cout << "{\"level\":\"info\",\"message\":\"primitive textures updated\",\"count\":"
                                          << originals.size() << "}" << std::endl;
                        }
                        const auto object_flags =
                            homeworldz::viewer::decode_object_flag_update(packet->payload);
                        if (object_flags && object_flags->agent_id == identity->agent_id &&
                            object_flags->session_id == identity->session_id) {
                            auto* entity = scene.find(object_flags->local_id);
                            if (entity && entity->parent_id != 0)
                                entity = scene.find(entity->parent_id);
                            const auto user_id = homeworldz::viewer::format_uuid(identity->agent_id);
                            if (entity && entity->owner_id == user_id &&
                                (entity->owner_permissions & homeworldz::scene::permission_modify) != 0) {
                                const auto original_physical = entity->physical;
                                const auto original_phantom = entity->phantom;
                                const auto original_temporary = entity->temporary;
                                const auto original_physics_shape_type = entity->physics_shape_type;
                                const auto original_physics_density = entity->physics_density;
                                const auto original_physics_friction = entity->physics_friction;
                                const auto original_physics_restitution = entity->physics_restitution;
                                const auto original_physics_gravity_multiplier =
                                    entity->physics_gravity_multiplier;
                                entity->physical = object_flags->use_physics;
                                entity->phantom = object_flags->phantom;
                                entity->temporary = object_flags->temporary;
                                if (object_flags->has_extra_physics)
                                    apply_extra_physics(*entity, *object_flags);
                                bool persisted = false;
                                try {
                                    storage->save_snapshot(scene);
                                    persisted = true;
                                } catch (const std::exception& error) {
                                    entity->physical = original_physical;
                                    entity->phantom = original_phantom;
                                    entity->temporary = original_temporary;
                                    entity->physics_shape_type = original_physics_shape_type;
                                    entity->physics_density = original_physics_density;
                                    entity->physics_friction = original_physics_friction;
                                    entity->physics_restitution = original_physics_restitution;
                                    entity->physics_gravity_multiplier =
                                        original_physics_gravity_multiplier;
                                    std::cout << "{\"level\":\"error\",\"message\":\"primitive flags persistence failed\",\"error\":"
                                              << homeworldz::api::json_string(error.what()) << "}" << std::endl;
                                }
                                if (persisted) {
                                    if (entity->temporary)
                                        temporary_expirations.insert_or_assign(
                                            entity->id, now + std::chrono::seconds(60));
                                    else
                                        temporary_expirations.erase(entity->id);
                                    synchronize_physics_object(*entity);
                                    const auto region_handle =
                                        (static_cast<std::uint64_t>(region_grid_x * 256) << 32) |
                                        static_cast<std::uint32_t>(region_grid_y * 256);
                                    for (const auto& [recipient_endpoint, recipient] : avatars) {
                                        const auto object = static_object_from_entity(scene, *entity, recipient.user_id, falcon);
                                        if (!object) continue;
                                        if (const auto sent = circuits.send(recipient_endpoint,
                                                homeworldz::viewer::encode_static_object_update(
                                                    region_handle, *object), true, now, true))
                                            static_cast<void>(send_udp(
                                                viewer_server, recipient_endpoint, *sent));
                                    }
                                    std::cout << "{\"level\":\"info\",\"message\":\"primitive flags updated\",\"entityId\":"
                                              << entity->id << ",\"physical\":"
                                              << (entity->physical ? "true" : "false")
                                              << ",\"phantom\":"
                                              << (entity->phantom ? "true" : "false")
                                              << ",\"temporary\":"
                                              << (entity->temporary ? "true" : "false")
                                              << ",\"physicsShapeType\":"
                                              << static_cast<unsigned>(entity->physics_shape_type)
                                              << ",\"physicsDensity\":" << entity->physics_density
                                              << ",\"physicsFriction\":" << entity->physics_friction
                                              << ",\"physicsRestitution\":"
                                              << entity->physics_restitution
                                              << ",\"physicsGravityMultiplier\":"
                                              << entity->physics_gravity_multiplier << "}" << std::endl;
                                }
                            }
                        }
                        const auto object_add = homeworldz::viewer::decode_object_add(packet->payload);
                        if (object_add && object_add->agent_id == identity->agent_id &&
                            object_add->session_id == identity->session_id) {
                            constexpr std::uint32_t add_use_physics = 0x00000001;
                            constexpr std::uint32_t add_create_selected = 0x00000002;
                            constexpr std::uint32_t add_temporary = 0x40000000;
                            const auto valid_scale = std::all_of(
                                object_add->scale.begin(), object_add->scale.end(),
                                [](float value) { return value >= 0.01F && value <= 64.0F; });
                            const auto rotation_norm = object_add->rotation[0] * object_add->rotation[0] +
                                                       object_add->rotation[1] * object_add->rotation[1] +
                                                       object_add->rotation[2] * object_add->rotation[2];
                            const bool valid_rotation = rotation_norm <= 1.001F;
                            // Accept any well-formed basic prim: PCODE_PRIM (9) with a
                            // recognized path curve (line 0x10 / circle 0x20 / circle2
                            // 0x30) and profile curve (circle/square/iso-tri/equal-tri/
                            // right-tri/half-circle, low nibble 0x00..0x05). The full
                            // path+profile parameters are stored on the entity and
                            // echoed in the object update, so all seven basic shapes —
                            // Box, Cylinder, Prism, Sphere, Torus, Tube, Ring — plus
                            // edited variations (cut, hollow, twist, taper, shear,
                            // revolutions) rez and render from the viewer-supplied params.
                            const bool valid_path_curve = object_add->path_curve == 0x10 ||
                                object_add->path_curve == 0x20 || object_add->path_curve == 0x30;
                            const bool valid_profile_curve = (object_add->profile_curve & 0x0f) <= 0x05;
                            const bool valid_prim_shape = object_add->pcode == 9 &&
                                valid_path_curve && valid_profile_curve;
                            std::optional<homeworldz::scene::Vector3> placement;
                            if (valid_scale && object_add->bypass_raycast) {
                                const homeworldz::scene::Vector3 ray_end{
                                    object_add->ray_end[0], object_add->ray_end[1], object_add->ray_end[2]};
                                placement = homeworldz::scene::Vector3{
                                    ray_end.x, ray_end.y,
                                    ground_height(*terrain_heightmap, ray_end) + object_add->scale[2] * 0.5};
                            } else if (valid_scale) {
                                const auto target_id = homeworldz::viewer::format_uuid(object_add->ray_target_id);
                                const homeworldz::scene::Entity* target = nullptr;
                                for (const auto& [candidate_id, candidate] : scene.entities()) {
                                    static_cast<void>(candidate_id);
                                    if (candidate.object_id == target_id) {
                                        target = &candidate;
                                        break;
                                    }
                                }
                                if (target) {
                                    const auto intersection = homeworldz::scene::intersect_box(
                                        {object_add->ray_start[0], object_add->ray_start[1], object_add->ray_start[2]},
                                        {object_add->ray_end[0], object_add->ray_end[1], object_add->ray_end[2]},
                                        target->position, target->scale);
                                    if (intersection) {
                                        placement = homeworldz::scene::Vector3{
                                            intersection->position.x + intersection->normal.x * object_add->scale[0] * 0.5,
                                            intersection->position.y + intersection->normal.y * object_add->scale[1] * 0.5,
                                            intersection->position.z + intersection->normal.z * object_add->scale[2] * 0.5};
                                    }
                                }
                            }
                            const bool valid_position = placement && placement->x >= 0.0 &&
                                placement->x <= region_size_x && placement->y >= 0.0 &&
                                placement->y <= region_size_y &&
                                placement->z >= -64.0 && placement->z <= 4096.0;
                            bool created = false;
                            std::string object_id;
                            homeworldz::scene::EntityId entity_id{};
                            if (valid_prim_shape &&
                                valid_position && valid_rotation && object_add->material <= 7) {
                                object_id = homeworldz::viewer::random_uuid();
                                const auto owner_id = homeworldz::viewer::format_uuid(identity->agent_id);
                                entity_id = scene.create("Primitive", *placement);
                                if (auto* entity = scene.find(entity_id)) {
                                    entity->object_id = object_id;
                                    entity->owner_id = owner_id;
                                    entity->creator_id = owner_id;
                                    entity->scale = {object_add->scale[0], object_add->scale[1], object_add->scale[2]};
                                    entity->rotation = {object_add->rotation[0], object_add->rotation[1],
                                                        object_add->rotation[2]};
                                    entity->material = object_add->material;
                                    entity->physical = (object_add->add_flags & add_use_physics) != 0;
                                    entity->temporary = (object_add->add_flags & add_temporary) != 0;
                                    entity->path_curve = object_add->path_curve;
                                    entity->profile_curve = object_add->profile_curve;
                                    entity->path_begin = object_add->path_begin;
                                    entity->path_end = object_add->path_end;
                                    entity->path_scale_x = object_add->path_scale_x;
                                    entity->path_scale_y = object_add->path_scale_y;
                                    entity->path_shear_x = object_add->path_shear_x;
                                    entity->path_shear_y = object_add->path_shear_y;
                                    entity->path_twist = object_add->path_twist;
                                    entity->path_twist_begin = object_add->path_twist_begin;
                                    entity->path_radius_offset = object_add->path_radius_offset;
                                    entity->path_taper_x = object_add->path_taper_x;
                                    entity->path_taper_y = object_add->path_taper_y;
                                    entity->path_revolutions = object_add->path_revolutions;
                                    entity->path_skew = object_add->path_skew;
                                    entity->profile_begin = object_add->profile_begin;
                                    entity->profile_end = object_add->profile_end;
                                    entity->profile_hollow = object_add->profile_hollow;
                                    entity->texture_entry = default_prim_texture_entry();
                                    apply_material_contact_defaults(*entity);
                                    entity->creation_date = static_cast<std::uint64_t>(
                                        std::chrono::duration_cast<std::chrono::seconds>(
                                            std::chrono::system_clock::now().time_since_epoch()).count());
                                    try {
                                        storage->save_snapshot(scene);
                                        created = true;
                                    } catch (const std::exception& error) {
                                        std::cout << "{\"level\":\"error\",\"message\":\"primitive persistence failed\",\"error\":"
                                                  << homeworldz::api::json_string(error.what()) << "}" << std::endl;
                                        scene.remove(entity_id);
                                    }
                                }
                            }
                            if (created) {
                                const auto* entity = scene.find(entity_id);
                                if (entity) {
                                    if (entity->temporary)
                                        temporary_expirations.insert_or_assign(
                                            entity->id, now + std::chrono::seconds(60));
                                    synchronize_physics_object(*entity);
                                    const auto region_handle =
                                        (static_cast<std::uint64_t>(region_grid_x * 256) << 32) |
                                        static_cast<std::uint32_t>(region_grid_y * 256);
                                    for (const auto& [recipient_endpoint, recipient] : avatars) {
                                        auto object = static_object_from_entity(scene, *entity, recipient.user_id, falcon);
                                        if (!object) continue;
                                        if (recipient.user_id == entity->owner_id &&
                                            (object_add->add_flags & add_create_selected) != 0)
                                            object->update_flags |= add_create_selected;
                                        if (const auto sent = circuits.send(recipient_endpoint,
                                                homeworldz::viewer::encode_static_object_update(
                                                    region_handle, *object), true, now, true))
                                            static_cast<void>(send_udp(viewer_server, recipient_endpoint, *sent));
                                    }
                                }
                            }
                            std::cout << "{\"level\":" << (created ? "\"info\"" : "\"warn\"")
                                      << ",\"message\":\"primitive creation "
                                      << (created ? "completed" : "rejected") << "\",\"objectId\":"
                                      << homeworldz::api::json_string(object_id)
                                      << ",\"pcode\":" << static_cast<unsigned>(object_add->pcode)
                                      << ",\"pathCurve\":" << static_cast<unsigned>(object_add->path_curve)
                                      << ",\"profileCurve\":" << static_cast<unsigned>(object_add->profile_curve)
                                      << ",\"pathScale\":[" << static_cast<unsigned>(object_add->path_scale_x)
                                      << ',' << static_cast<unsigned>(object_add->path_scale_y) << ']'
                                      << ",\"pathShear\":[" << static_cast<unsigned>(object_add->path_shear_x)
                                      << ',' << static_cast<unsigned>(object_add->path_shear_y) << ']'
                                      << ",\"material\":" << static_cast<unsigned>(object_add->material)
                                      << ",\"scale\":[" << object_add->scale[0] << ','
                                      << object_add->scale[1] << ',' << object_add->scale[2] << ']'
                                      << ",\"validScale\":" << (valid_scale ? "true" : "false")
                                      << ",\"validRotation\":" << (valid_rotation ? "true" : "false")
                                      << ",\"validPosition\":" << (valid_position ? "true" : "false")
                                      << "}" << std::endl;
                        }
                        const auto derez = homeworldz::viewer::decode_derez_object(packet->payload);
                        if (derez && derez->agent_id == identity->agent_id &&
                            derez->session_id == identity->session_id) {
                            constexpr std::uint8_t derez_take_copy = 0x01;
                            constexpr std::uint8_t derez_take_inventory = 0x04;
                            constexpr std::uint8_t derez_trash = 0x06;
                            constexpr std::uint8_t derez_return_owner = 0x09;
                            constexpr int objects_folder_type = 6;
                            const auto user_id = homeworldz::viewer::format_uuid(identity->agent_id);
                            auto destination_id = homeworldz::viewer::format_uuid(derez->destination_id);
                            const bool objects_destination =
                                derez->destination == derez_take_copy ||
                                derez->destination == derez_take_inventory ||
                                derez->destination == derez_return_owner;
                            const bool removes_from_scene = derez->destination != derez_take_copy;
                            if (objects_destination &&
                                destination_id == "00000000-0000-0000-0000-000000000000" && viewer_grid) {
                                if (const auto objects_folder = viewer_grid->find_system_inventory_folder(
                                        user_id, objects_folder_type))
                                    destination_id = *objects_folder;
                            }
                            std::vector<std::uint32_t> removed_ids;
                            std::size_t inventory_items_created = 0;
                            std::unordered_set<homeworldz::scene::EntityId> processed_roots;
                            if ((objects_destination || derez->destination == derez_trash) &&
                                homeworldz::viewer::valid_derez_batch(
                                    derez->packet_count, derez->packet_number)) {
                                for (const auto local_id : derez->local_ids) {
                                    const auto* selected = scene.find(local_id);
                                    if (!selected) continue;
                                    const auto root_id = selected->parent_id != 0
                                        ? selected->parent_id : local_id;
                                    const auto* entity = scene.find(root_id);
                                    if (!processed_roots.insert(root_id).second || !entity ||
                                        entity->object_id.empty() || entity->owner_id != user_id)
                                        continue;
                                    std::vector<const homeworldz::scene::Entity*> children;
                                    std::vector<homeworldz::scene::EntityId> part_ids{root_id};
                                    bool valid_linkset = true;
                                    for (const auto& [candidate_id, candidate] : scene.entities()) {
                                        if (candidate.parent_id != root_id) continue;
                                        if (candidate.owner_id != user_id) {
                                            valid_linkset = false;
                                            break;
                                        }
                                        children.push_back(&candidate);
                                        part_ids.push_back(candidate_id);
                                    }
                                    const auto folded = homeworldz::scene::effective_permissions(scene, *entity);
                                    if (!valid_linkset ||
                                        (derez->destination == derez_take_copy &&
                                         (folded.owner & homeworldz::scene::permission_copy) == 0))
                                        continue;
                                    const auto asset_id = homeworldz::viewer::random_uuid();
                                    const auto item_id = homeworldz::viewer::random_uuid();
                                    const auto content_text =
                                        homeworldz::asset::serialize_linkset_asset(*entity, children);
                                    const auto content = std::span(
                                        reinterpret_cast<const std::byte*>(content_text.data()), content_text.size());
                                    auto base_permissions = entity->base_permissions;
                                    auto owner_permissions = folded.owner;
                                    auto everyone_permissions = entity->everyone_permissions;
                                    auto next_owner_permissions = folded.next_owner;
                                    for (const auto* child : children) {
                                        base_permissions &= child->base_permissions;
                                        everyone_permissions &= child->everyone_permissions;
                                    }
                                    everyone_permissions &= owner_permissions;
                                    bool item_created = false;
                                    try {
                                        const auto metadata = storage->store_asset(
                                            asset_id, entity->creator_id, content);
                                        const bool asset_registered = viewer_grid && viewer_grid->register_asset(
                                            metadata.viewer_id, metadata.creator_id, metadata.sha256,
                                            metadata.size, region_public_endpoint, true);
                                        item_created = asset_registered && viewer_grid->create_object_inventory_item(
                                            user_id, homeworldz::grid::ObjectInventoryItem{
                                                item_id, entity->creator_id, destination_id, asset_id,
                                                entity->name, entity->description, base_permissions,
                                                owner_permissions, everyone_permissions,
                                                next_owner_permissions});
                                    } catch (const std::exception& error) {
                                        std::cout << "{\"level\":\"error\",\"message\":\"primitive derez inventory failed\",\"error\":"
                                                  << homeworldz::api::json_string(error.what()) << "}" << std::endl;
                                    }
                                    if (!item_created) continue;
                                    homeworldz::viewer::InventoryItem item;
                                    item.item_id = *homeworldz::viewer::parse_uuid(item_id);
                                    item.creator_id = *homeworldz::viewer::parse_uuid(entity->creator_id);
                                    item.owner_id = identity->agent_id;
                                    item.folder_id = *homeworldz::viewer::parse_uuid(destination_id);
                                    item.asset_id = *homeworldz::viewer::parse_uuid(asset_id);
                                    item.asset_type = 6;
                                    item.inventory_type = 6;
                                    item.name = entity->name;
                                    item.base_permissions = base_permissions;
                                    item.current_permissions = owner_permissions;
                                    item.everyone_permissions = everyone_permissions;
                                    item.next_permissions = next_owner_permissions;
                                    item.creation_date = static_cast<std::int32_t>(
                                        std::chrono::duration_cast<std::chrono::seconds>(
                                            std::chrono::system_clock::now().time_since_epoch()).count());
                                    const homeworldz::viewer::AgentMessage reply{
                                        identity->agent_id, identity->session_id};
                                    auto inventory_update = homeworldz::viewer::encode_update_create_inventory_item(
                                        reply, 0, item);
                                    if (const auto outgoing = circuits.send(
                                            endpoint, std::move(inventory_update), true, now, true))
                                        static_cast<void>(send_udp(viewer_server, endpoint, *outgoing));
                                    if (removes_from_scene)
                                        for (auto part = part_ids.rbegin(); part != part_ids.rend(); ++part)
                                            if (scene.remove(*part))
                                                removed_ids.push_back(static_cast<std::uint32_t>(*part));
                                    ++inventory_items_created;
                                }
                            }
                            bool persisted = removed_ids.empty();
                            if (!removed_ids.empty()) {
                                try {
                                    storage->save_snapshot(scene);
                                    persisted = true;
                                } catch (const std::exception& error) {
                                    std::cout << "{\"level\":\"error\",\"message\":\"primitive derez persistence failed\",\"error\":"
                                              << homeworldz::api::json_string(error.what()) << "}" << std::endl;
                                }
                            }
                            if (persisted && !removed_ids.empty()) {
                                for (const auto entity_id : removed_ids) remove_physics_object(entity_id);
                                const auto kill = homeworldz::viewer::encode_kill_object(removed_ids);
                                for (const auto& [recipient_endpoint, recipient] : avatars) {
                                    static_cast<void>(recipient);
                                    if (const auto outgoing = circuits.send(
                                            recipient_endpoint, kill, true, now))
                                        static_cast<void>(send_udp(viewer_server, recipient_endpoint, *outgoing));
                                }
                            }
                            std::cout << "{\"level\":"
                                      << (persisted && inventory_items_created == processed_roots.size()
                                              ? "\"info\"" : "\"warn\"")
                                      << ",\"message\":\"primitive derez batch processed\",\"removed\":"
                                      << removed_ids.size() << ",\"inventoryItemsCreated\":"
                                      << inventory_items_created << ",\"requested\":" << derez->local_ids.size()
                                      << ",\"destination\":" << static_cast<unsigned>(derez->destination) << "}"
                                      << std::endl;
                        }
                        const auto rez = homeworldz::viewer::decode_rez_object(packet->payload);
                        if (rez && rez->agent_id == identity->agent_id &&
                            rez->session_id == identity->session_id) {
                            const auto user_id = homeworldz::viewer::format_uuid(identity->agent_id);
                            const auto item_id = homeworldz::viewer::format_uuid(rez->item_id);
                            bool created = false;
                            std::string object_id;
                            std::string object_rez_id;
                            bool object_rez_prepared = false;
                            bool scene_persisted = false;
                            std::vector<homeworldz::scene::EntityId> entity_ids;
                            try {
                                auto item = viewer_grid
                                    ? viewer_grid->find_inventory_item(user_id, item_id) : std::nullopt;
                                if (item && item->asset_type == 6 && item->inventory_type == 6) {
                                    const auto content = read_federated_asset(item->asset_id);
                                    const auto linkset = homeworldz::asset::parse_linkset_asset(content);
                                    const auto* asset = linkset ? &linkset->root : nullptr;
                                    std::optional<homeworldz::scene::Vector3> placement;
                                    if (asset && rez->bypass_raycast) {
                                        const homeworldz::scene::Vector3 ray_end{
                                            rez->ray_end[0], rez->ray_end[1], rez->ray_end[2]};
                                        placement = homeworldz::scene::Vector3{
                                            ray_end.x, ray_end.y,
                                            ground_height(*terrain_heightmap, ray_end) + asset->scale.z * 0.5};
                                    } else if (asset) {
                                        const auto target_id = homeworldz::viewer::format_uuid(rez->ray_target_id);
                                        const homeworldz::scene::Entity* target = nullptr;
                                        for (const auto& [candidate_id, candidate] : scene.entities()) {
                                            static_cast<void>(candidate_id);
                                            if (candidate.object_id == target_id) {
                                                target = &candidate;
                                                break;
                                            }
                                        }
                                        if (target) {
                                            const auto intersection = homeworldz::scene::intersect_box(
                                                {rez->ray_start[0], rez->ray_start[1], rez->ray_start[2]},
                                                {rez->ray_end[0], rez->ray_end[1], rez->ray_end[2]},
                                                target->position, target->scale);
                                            if (intersection) {
                                                placement = homeworldz::scene::Vector3{
                                                    intersection->position.x + intersection->normal.x * asset->scale.x * 0.5,
                                                    intersection->position.y + intersection->normal.y * asset->scale.y * 0.5,
                                                    intersection->position.z + intersection->normal.z * asset->scale.z * 0.5};
                                            }
                                        } else {
                                            const homeworldz::scene::Vector3 ray_end{
                                                rez->ray_end[0], rez->ray_end[1], rez->ray_end[2]};
                                            placement = homeworldz::scene::Vector3{
                                                ray_end.x, ray_end.y,
                                                ground_height(*terrain_heightmap, ray_end) + asset->scale.z * 0.5};
                                        }
                                    }
                                    const bool valid_position = placement &&
                                        placement->x >= 0.0 && placement->x <= region_size_x &&
                                        placement->y >= 0.0 && placement->y <= region_size_y &&
                                        placement->z >= -64.0 && placement->z <= 4096.0;
                                    if (asset && valid_position) {
                                        object_id = homeworldz::viewer::random_uuid();
                                        const bool no_copy =
                                            (item->current_permissions & homeworldz::scene::permission_copy) == 0;
                                        if (rez->remove_item && no_copy) {
                                            object_rez_id = homeworldz::viewer::random_uuid();
                                            const auto prepared = viewer_grid
                                                ? viewer_grid->prepare_object_rez({
                                                    object_rez_id, user_id, item_id,
                                                    provisioned_region_id, object_id})
                                                : std::nullopt;
                                            if (!prepared) throw std::runtime_error("prepare no-copy object rez");
                                            item = prepared->item;
                                            object_id = prepared->object_id;
                                            object_rez_prepared = true;
                                        }
                                        const auto root_id = scene.create(item->name, *placement);
                                        entity_ids.push_back(root_id);
                                        if (auto* entity = scene.find(root_id)) {
                                            entity->object_id = object_id;
                                            entity->owner_id = user_id;
                                            entity->creator_id = item->creator_id;
                                            apply_object_asset(*entity, *asset);
                                            entity->description = item->description.empty()
                                                ? asset->description : item->description;
                                            entity->base_permissions = item->base_permissions;
                                            entity->owner_permissions = item->current_permissions;
                                            entity->everyone_permissions = item->everyone_permissions;
                                            entity->next_owner_permissions = item->next_permissions;
                                            entity->creation_date = static_cast<std::uint64_t>(
                                                std::chrono::duration_cast<std::chrono::seconds>(
                                                    std::chrono::system_clock::now().time_since_epoch()).count());
                                            for (const auto& child_asset : linkset->children) {
                                                const auto child_id = scene.create(
                                                    child_asset.name.empty() ? "Primitive" : child_asset.name);
                                                entity_ids.push_back(child_id);
                                                auto* child = scene.find(child_id);
                                                if (!child) throw std::runtime_error("create linkset child");
                                                child->object_id = homeworldz::viewer::random_uuid();
                                                child->owner_id = user_id;
                                                child->creator_id = child_asset.creator_id.empty()
                                                    ? item->creator_id : child_asset.creator_id;
                                                apply_object_asset(*child, child_asset);
                                                child->description = child_asset.description;
                                                child->base_permissions =
                                                    child_asset.base_permissions & item->base_permissions;
                                                child->owner_permissions =
                                                    child_asset.owner_permissions & item->current_permissions;
                                                child->group_permissions = child_asset.group_permissions;
                                                child->everyone_permissions =
                                                    child_asset.everyone_permissions & item->everyone_permissions;
                                                child->next_owner_permissions =
                                                    child_asset.next_owner_permissions & item->next_permissions;
                                                child->creation_date = entity->creation_date;
                                                child->parent_id = root_id;
                                                child->local_position = child_asset.local_position;
                                                child->local_rotation = child_asset.local_rotation;
                                                homeworldz::scene::update_linked_world_transform(*child, *entity);
                                            }
                                            storage->save_snapshot(scene);
                                            scene_persisted = true;
                                            created = true;
                                        }
                                    }
                                }
                            } catch (const std::exception& error) {
                                for (auto entity = entity_ids.rbegin(); entity != entity_ids.rend(); ++entity)
                                    scene.remove(*entity);
                                std::cout << "{\"level\":\"error\",\"message\":\"primitive rez failed\",\"error\":"
                                          << homeworldz::api::json_string(error.what()) << "}" << std::endl;
                            }
                            if (object_rez_prepared && !scene_persisted && viewer_grid) {
                                if (!viewer_grid->rollback_object_rez(object_rez_id, provisioned_region_id))
                                    std::cerr << "{\"level\":\"warning\",\"message\":\"object rez rollback awaits reconciliation\",\"rezId\":"
                                              << homeworldz::api::json_string(object_rez_id) << "}" << std::endl;
                            }
                            if (object_rez_prepared && scene_persisted && viewer_grid) {
                                if (!viewer_grid->finalize_object_rez(object_rez_id, provisioned_region_id))
                                    std::cerr << "{\"level\":\"warning\",\"message\":\"object rez finalization awaits reconciliation\",\"rezId\":"
                                              << homeworldz::api::json_string(object_rez_id) << "}" << std::endl;
                            }
                            if (created) {
                                for (const auto entity_id : entity_ids) {
                                    const auto* entity = scene.find(entity_id);
                                    if (!entity) continue;
                                    synchronize_physics_object(*entity);
                                    const auto region_handle =
                                        (static_cast<std::uint64_t>(region_grid_x * 256) << 32) |
                                        static_cast<std::uint32_t>(region_grid_y * 256);
                                    for (const auto& [recipient_endpoint, recipient] : avatars) {
                                        const auto object = static_object_from_entity(scene, *entity, recipient.user_id, falcon);
                                        if (!object) continue;
                                        if (const auto outgoing = circuits.send(
                                                recipient_endpoint,
                                                homeworldz::viewer::encode_static_object_update(
                                                    region_handle, *object), true, now, true))
                                            static_cast<void>(send_udp(viewer_server, recipient_endpoint, *outgoing));
                                    }
                                }
                            }
                            std::cout << "{\"level\":" << (created ? "\"info\"" : "\"warn\"")
                                      << ",\"message\":\"primitive inventory rez "
                                      << (created ? "completed" : "rejected") << "\",\"itemId\":"
                                      << homeworldz::api::json_string(item_id) << ",\"objectId\":"
                                      << homeworldz::api::json_string(object_id) << "}" << std::endl;
                        }
                        const auto update = homeworldz::viewer::decode_agent_update(packet->payload);
                        const auto avatar = avatars.find(endpoint);
                        if (update && avatar != avatars.end() && update->agent_id == identity->agent_id &&
                            update->session_id == identity->session_id &&
                            (!avatar->second.has_agent_update || sequence_is_newer(
                                packet->sequence, avatar->second.last_agent_update_sequence))) {
                            const bool first_update = !avatar->second.has_agent_update;
                            const bool was_flying = avatar->second.controller.state().flying;
                            const bool grace_active = now < avatar->second.restored_flying_until;
                            auto accepted = *update;
                            if (grace_active && was_flying)
                                accepted.control_flags |= homeworldz::viewer::control_fly;
                            avatar->second.controller.apply(accepted);
                            avatar->second.last_agent_update = now;
                            avatar->second.last_agent_update_sequence = packet->sequence;
                            avatar->second.has_agent_update = true;
                            const bool is_flying = avatar->second.controller.state().flying;
                            if (first_update || was_flying != is_flying)
                                std::cout << "{\"level\":\"info\",\"message\":\"avatar flight control updated\",\"firstUpdate\":"
                                          << (first_update ? "true" : "false")
                                          << ",\"viewerFlying\":"
                                          << ((update->control_flags & homeworldz::viewer::control_fly) != 0 ? "true" : "false")
                                          << ",\"graceActive\":" << (grace_active ? "true" : "false")
                                          << ",\"flying\":" << (is_flying ? "true" : "false") << "}" << std::endl;
                        }
                        const auto terrain_edit = homeworldz::viewer::decode_modify_land(packet->payload);
                        if (terrain_edit && terrain_edit->agent_id == identity->agent_id &&
                            terrain_edit->session_id == identity->session_id) {
                            const auto changed = homeworldz::terrain::apply(
                                *terrain_heightmap, *revert_heightmap, *terrain_edit);
                            if (!changed.empty()) {
                                const auto persisted = homeworldz::terrain::save_state(
                                    terrain_state_path, *terrain_heightmap);
                                const auto physics_synchronized = synchronize_physics_terrain();
                                constexpr std::size_t patches_per_packet = 16;
                                for (const auto& [recipient_endpoint, recipient] : avatars) {
                                    static_cast<void>(recipient);
                                    for (std::size_t offset = 0; offset < changed.size();
                                         offset += patches_per_packet) {
                                        const auto count = (std::min)(patches_per_packet, changed.size() - offset);
                                        const auto terrain_payload = homeworldz::viewer::encode_terrain(
                                            std::span<const homeworldz::viewer::TerrainPatch>(
                                                changed.data() + offset, count), *terrain_heightmap);
                                        if (const auto outgoing = circuits.send(
                                                recipient_endpoint, terrain_payload, true, now))
                                            static_cast<void>(send_udp(
                                                viewer_server, recipient_endpoint, *outgoing));
                                    }
                                }
                                std::cout << "{\"level\":" << (persisted ? "\"info\"" : "\"error\"")
                                          << ",\"message\":\"terrain edit applied\",\"action\":"
                                          << static_cast<unsigned>(terrain_edit->action)
                                           << ",\"patches\":" << changed.size()
                                           << ",\"persisted\":" << (persisted ? "true" : "false")
                                           << ",\"physicsSynchronized\":"
                                           << (physics_synchronized ? "true" : "false") << "}"
                                           << std::endl;
                            }
                        }
                        const auto chat = homeworldz::viewer::decode_chat_from_viewer(packet->payload);
                        if (chat && avatar != avatars.end() && chat->agent_id == identity->agent_id &&
                            chat->session_id == identity->session_id && chat->channel == 0 &&
                            !chat->message.empty() && chat->message.size() <= 1023) {
                            const auto& origin = avatar->second.controller.state().position;
                            const double radius = chat->type == 0 ? 10.0 : (chat->type == 2 ? 100.0 : 20.0);
                            homeworldz::viewer::ChatFromSimulator outgoing;
                            outgoing.from_name = homeworldz::viewer::format_uuid(identity->agent_id);
                            outgoing.source_id = identity->agent_id;
                            outgoing.owner_id = identity->agent_id;
                            outgoing.chat_type = chat->type;
                            outgoing.position = {static_cast<float>(origin.x), static_cast<float>(origin.y),
                                                 static_cast<float>(origin.z)};
                            outgoing.message = chat->message;
                            const auto payload = homeworldz::viewer::encode_chat_from_simulator(outgoing);
                            for (const auto& [recipient_endpoint, recipient] : avatars) {
                                const auto& target = recipient.controller.state().position;
                                const auto dx = target.x - origin.x, dy = target.y - origin.y, dz = target.z - origin.z;
                                if (dx * dx + dy * dy + dz * dz > radius * radius) continue;
                                if (const auto sent = circuits.send(recipient_endpoint, payload, true, now))
                                    static_cast<void>(send_udp(viewer_server, recipient_endpoint, *sent));
                            }
                        }
                    }
                }
                }
            }
        }
        for (const auto& outgoing : circuits.poll(now))
            static_cast<void>(send_udp(viewer_server, outgoing.endpoint, outgoing.bytes));
        for (auto iterator = texture_packets.begin(); iterator != texture_packets.end();) {
            auto& queue = iterator->second;
            while (!queue.empty()) {
                const auto outgoing = circuits.send(iterator->first, queue.front().payload, true, now);
                if (!outgoing) break;
                static_cast<void>(send_udp(viewer_server, iterator->first, *outgoing));
                const auto completed_asset = queue.front().last ? queue.front().asset_id : std::string{};
                queue.pop_front();
                if (!completed_asset.empty()) {
                    active_texture_transfers.erase(iterator->first + '|' + completed_asset);
                    std::cout << "{\"level\":\"info\",\"message\":\"texture transfer sent\","
                                 "\"assetId\":" << homeworldz::api::json_string(completed_asset) << "}"
                              << std::endl;
                }
            }
            if (queue.empty()) iterator = texture_packets.erase(iterator);
            else ++iterator;
        }
        const auto script_tick = falcon.run_tick();
        if (script_tick.trapped != 0) {
            std::cerr << "{\"level\":\"warning\",\"message\":\"Falcon script runtime trapped\","
                         "\"scriptsVisited\":"
                      << script_tick.scripts_visited << ",\"instructions\":"
                      << script_tick.instructions << ",\"trapped\":"
                      << script_tick.trapped << "}" << std::endl;
        }
        const auto elapsed = std::chrono::duration<double>(now - previous_tick).count();
        const auto fixed_steps = simulation.advance(elapsed);
        std::vector<std::pair<std::string, std::string>> departed_avatars;
        for (auto& [endpoint, avatar] : avatars) {
            if (!avatar.outbound_transit_id.empty() && now >= avatar.outbound_transit_expires) {
                if (viewer_grid && registration)
                    static_cast<void>(viewer_grid->rollback_avatar_transit(
                        avatar.outbound_transit_id, registration->region_id(),
                        "viewer did not activate border crossing"));
                std::cout << "{\"level\":\"warning\",\"message\":\"avatar border crossing expired\","
                             "\"transitId\":"
                          << homeworldz::api::json_string(avatar.outbound_transit_id) << "}" << std::endl;
                avatar.outbound_transit_id.clear();
            }
            if (physics_world && avatar.physics_character != 0)
                if (const auto state = physics_world->character_state(avatar.physics_character))
                    avatar.controller.synchronize_physics(
                        state->position, state->linear_velocity, state->grounded);
            if (avatar.has_agent_update &&
                now - avatar.last_agent_update > std::chrono::seconds(1))
                avatar.controller.expire_transient_controls();
            if (now >= avatar.next_ping) {
                const auto* circuit_identity = circuits.identity(endpoint);
                const auto session_id = circuit_identity ?
                    homeworldz::viewer::format_uuid(circuit_identity->session_id) : std::string{};
                if (now - avatar.last_pong > connection_timeout) {
                    // Connection lost: the viewer has not answered a ping within
                    // the timeout (crash, force-kill, or sustained packet loss).
                    // Retire it (the departed path broadcasts the KillObject)
                    // instead of waiting for the grid session TTL.
                    std::cout << "{\"level\":\"info\",\"message\":\"viewer connection lost (no ping reply)\","
                                 "\"sessionId\":"
                              << homeworldz::api::json_string(session_id) << ",\"secondsSincePong\":"
                              << std::chrono::duration_cast<std::chrono::seconds>(
                                     now - avatar.last_pong).count()
                              << "}" << std::endl;
                    departed_avatars.emplace_back(endpoint, session_id);
                    continue;
                }
                try {
                    const auto session = circuit_identity && viewer_sessions ?
                        viewer_sessions->validate(session_id, now) : std::nullopt;
                    if (!session || !registration ||
                        session->destination_region_id != registration->region_id()) {
                        departed_avatars.emplace_back(endpoint, session_id);
                        continue;
                    }
                } catch (const std::exception& error) {
                    std::cout << "{\"level\":\"warning\",\"message\":\"avatar authority check failed\",\"error\":"
                              << homeworldz::api::json_string(error.what()) << "}" << std::endl;
                }
                if (const auto ping = circuits.send(endpoint,
                        homeworldz::viewer::encode_start_ping_check(++avatar.ping_id), false, now))
                    static_cast<void>(send_udp(viewer_server, endpoint, *ping));
                avatar.next_ping = now + std::chrono::seconds(5);
            }
            if (now >= avatar.next_presence && viewer_grid && registration) {
                static_cast<void>(viewer_grid->update_presence(avatar.user_id, registration->region_id()));
                avatar.next_presence = now + std::chrono::seconds(30);
            }
            avatar.controller.set_ground_height(
                collision_ground_height(avatar.controller.state().position));
            const bool has_online_neighbor = std::any_of(
                region_neighbors.begin(), region_neighbors.end(),
                [](const auto& neighbor) { return neighbor.online; });
            avatar.controller.set_border_crossing_enabled(
                has_online_neighbor && avatar.outbound_transit_id.empty());
            if (!avatar.outbound_transit_id.empty())
                avatar.controller.expire_transient_controls();
            avatar.controller.step(elapsed);
            if (avatar.outbound_transit_id.empty()) {
                const auto& position = avatar.controller.state().position;
                const auto crossing = homeworldz::region::plan_avatar_border_crossing(
                    region_grid_x, region_grid_y, region_size_x, region_size_y,
                    {position.x, position.y, position.z}, region_neighbors);
                if (crossing && now >= avatar.next_crossing_attempt) {
                    const auto* identity = circuits.identity(endpoint);
                    const auto simulator = simulator_event_endpoint(
                        crossing->destination.public_endpoint,
                        crossing->destination.viewer_port);
                    bool prepared = false;
                    std::string transit_id;
                    try {
                        if (!identity || !viewer_grid || !registration || !simulator)
                            throw std::runtime_error("crossing services are unavailable");
                        const auto session_id = homeworldz::viewer::format_uuid(identity->session_id);
                        const auto agent_id = homeworldz::viewer::format_uuid(identity->agent_id);
                        transit_id = homeworldz::viewer::random_uuid();
                        const auto look_direction = avatar.controller.look_direction();
                        const bool flying = avatar.controller.state().flying;
                        const homeworldz::grid::AvatarTransitRequest request{
                            transit_id, agent_id, session_id, registration->region_id(),
                            crossing->destination.id, crossing->position, look_direction, flying, 30};
                        const auto transit = viewer_grid->prepare_avatar_transit(request);
                        prepared = transit && transit->state == "prepared";
                        if (!prepared)
                            throw std::runtime_error("grid rejected border crossing preparation");
                        auto destination = homeworldz::grid::socket_transport(
                            crossing->destination.public_endpoint, service_token);
                        if (!homeworldz::grid::prepare_avatar_arrival(*destination, transit_id))
                            throw std::runtime_error("destination rejected border crossing preparation");
                        const auto target_handle =
                            (static_cast<std::uint64_t>(crossing->destination.grid_x * 256) << 32) |
                            static_cast<std::uint32_t>(crossing->destination.grid_y * 256);
                        enqueue_viewer_event(session_id,
                            homeworldz::viewer::enable_simulator_event_xml(
                                target_handle, *simulator,
                                static_cast<std::uint32_t>(crossing->destination.size_x),
                                static_cast<std::uint32_t>(crossing->destination.size_y)));
                        enqueue_viewer_event(session_id,
                            homeworldz::viewer::crossed_region_event_xml({
                                agent_id, session_id, target_handle, *simulator,
                                crossing->destination.public_endpoint + "/caps/seed/" + session_id +
                                    "/" + transit_id,
                                crossing->position, look_direction,
                                static_cast<std::uint32_t>(crossing->destination.size_x),
                                static_cast<std::uint32_t>(crossing->destination.size_y)}));
                        avatar.outbound_transit_id = transit_id;
                        avatar.outbound_transit_expires = now + std::chrono::seconds(30);
                        std::cout << "{\"level\":\"info\",\"message\":\"avatar border crossing signaled\","
                                     "\"transitId\":"
                                  << homeworldz::api::json_string(transit_id)
                                  << ",\"destinationRegionId\":"
                                  << homeworldz::api::json_string(crossing->destination.id) << "}"
                                  << std::endl;
                    } catch (const std::exception& error) {
                        if (prepared && viewer_grid && registration)
                            static_cast<void>(viewer_grid->rollback_avatar_transit(
                                transit_id, registration->region_id(), error.what()));
                        std::cout << "{\"level\":\"error\",\"message\":\"avatar border crossing preparation failed\","
                                     "\"error\":"
                                  << homeworldz::api::json_string(error.what()) << "}" << std::endl;
                        avatar.next_crossing_attempt = now + std::chrono::seconds(1);
                    }
                    avatar.controller.contain_horizontal();
                } else if (position.x < 0.0 || position.x > region_size_x ||
                           position.y < 0.0 || position.y > region_size_y) {
                    avatar.controller.contain_horizontal();
                }
            } else {
                avatar.controller.contain_horizontal();
            }
            if (physics_world && avatar.physics_character != 0) {
                const auto& controller_state = avatar.controller.state();
                physics_world->set_character_velocity(avatar.physics_character, controller_state.velocity);
                physics_world->set_character_flying(
                    avatar.physics_character, controller_state.flying);
            }
            const auto desired_animation = avatar.controller.movement_animation();
            const auto desired_id = homeworldz::viewer::parse_uuid(
                homeworldz::viewer::movement_animation_id(desired_animation));
            const auto movement_agent_id = homeworldz::viewer::parse_uuid(avatar.user_id);
            auto& animations = avatar_animations[endpoint];
            const auto previous = movement_animations.find(endpoint);
            bool animation_changed = previous == movement_animations.end() ||
                                     previous->second != desired_animation;
            if (desired_id) {
                const auto present = std::find_if(animations.begin(), animations.end(),
                    [&](const auto& entry) { return entry.animation_id == *desired_id; });
                animation_changed = animation_changed || present == animations.end();
            }
            if (animation_changed && desired_id && movement_agent_id) {
                if (previous != movement_animations.end()) {
                    if (const auto old_id = homeworldz::viewer::parse_uuid(
                            homeworldz::viewer::movement_animation_id(previous->second)))
                        std::erase_if(animations, [&](const auto& entry) {
                            return entry.animation_id == *old_id;
                        });
                }
                auto& sequence = next_animation_sequences[endpoint];
                if (sequence < 2) sequence = 2;
                animations.push_back({*desired_id, sequence++, *movement_agent_id});
                movement_animations.insert_or_assign(endpoint, desired_animation);
                const homeworldz::viewer::AvatarAnimation update{*movement_agent_id, animations};
                const auto payload = homeworldz::viewer::encode_avatar_animation(update);
                for (const auto& [recipient_endpoint, recipient] : avatars) {
                    static_cast<void>(recipient);
                    if (const auto outgoing = circuits.send(
                            recipient_endpoint, payload, false, now, true))
                        static_cast<void>(send_udp(viewer_server, recipient_endpoint, *outgoing));
                }
            }
            if (auto* entity = scene.find(avatar.entity_id)) {
                entity->position = avatar.controller.state().position;
                entity->velocity = avatar.controller.state().velocity;
                entity->rotation = {avatar.controller.state().rotation[0],
                                    avatar.controller.state().rotation[1],
                                    avatar.controller.state().rotation[2]};
                entity->avatar_flying = avatar.controller.state().flying;
            }
            const auto& state = avatar.controller.state();
            const auto viewer_position = avatar.controller.viewer_position();
            const auto dx = viewer_position.x - avatar.last_sent_position.x;
            const auto dy = viewer_position.y - avatar.last_sent_position.y;
            const auto dz = viewer_position.z - avatar.last_sent_position.z;
            const auto dvx = state.velocity.x - avatar.last_sent_velocity.x;
            const auto dvy = state.velocity.y - avatar.last_sent_velocity.y;
            const auto dvz = state.velocity.z - avatar.last_sent_velocity.z;
            const auto drx = state.rotation[0] - avatar.last_sent_rotation[0];
            const auto dry = state.rotation[1] - avatar.last_sent_rotation[1];
            const auto drz = state.rotation[2] - avatar.last_sent_rotation[2];
            const bool transform_changed = (dx * dx + dy * dy + dz * dz) > 0.001 ||
                                           (dvx * dvx + dvy * dvy + dvz * dvz) > 0.000001 ||
                                           (drx * drx + dry * dry + drz * drz) > 0.000001F;
            if (transform_changed && now >= avatar.next_transform) {
                const auto agent_id = homeworldz::viewer::parse_uuid(avatar.user_id);
                if (agent_id) {
                    const auto region_handle =
                        (static_cast<std::uint64_t>(region_grid_x * 256) << 32) |
                        static_cast<std::uint32_t>(region_grid_y * 256);
                    const std::array<float, 3> position{
                        static_cast<float>(viewer_position.x), static_cast<float>(viewer_position.y),
                        static_cast<float>(viewer_position.z)};
                    const std::array<float, 3> velocity{
                        static_cast<float>(state.velocity.x), static_cast<float>(state.velocity.y),
                        static_cast<float>(state.velocity.z)};
                    const auto update = homeworldz::viewer::encode_avatar_object_update(
                        region_handle, static_cast<std::uint32_t>(avatar.entity_id), *agent_id,
                        position, velocity, state.rotation);
                    for (const auto& recipient_entry : avatars) {
                        const auto& recipient_endpoint = recipient_entry.first;
                        if (const auto outgoing = circuits.send(recipient_endpoint, update, false, now, true))
                            static_cast<void>(send_udp(viewer_server, recipient_endpoint, *outgoing));
                    }
                    avatar.last_sent_position = viewer_position;
                    avatar.last_sent_velocity = state.velocity;
                    avatar.last_sent_rotation = state.rotation;
                    avatar.next_transform = now + std::chrono::milliseconds(100);
                }
            }
        }
        for (const auto& [endpoint, session_id] : departed_avatars) {
            clear_viewer_endpoint(endpoint, session_id);
            circuits.remove(endpoint);
            std::cout << "{\"level\":\"info\",\"message\":\"departed avatar retired\",\"sessionId\":"
                      << homeworldz::api::json_string(session_id) << "}" << std::endl;
        }
        if (physics_world)
            for (std::size_t step = 0; step < fixed_steps; ++step)
                physics_world->step(simulation.step_seconds());
        if (physics_world && physics_scene && now >= next_dynamic_sync) {
            const auto region_handle =
                (static_cast<std::uint64_t>(region_grid_x * 256) << 32) |
                static_cast<std::uint32_t>(region_grid_y * 256);
            for (const auto& [entity_id, current] : scene.entities()) {
                if (!current.physical || current.phantom || current.object_id.empty()) continue;
                const auto body_id = physics_scene->body_id(entity_id);
                const auto current_state = physics_world->body_state(body_id);
                if (!current_state) continue;
                auto state = *current_state;
                // Phase 1 has no neighboring regions. Keep body origins within
                // this region and cancel only velocity still pointing through a
                // crossed edge. Neighbor discovery will replace this with a
                // crossing handoff when an accepting neighbor exists.
                if (homeworldz::physics::contain_body_without_neighbors(
                        state, static_cast<double>(region_size_x)))
                    physics_world->set_body_state(state);
                auto* entity = scene.find(entity_id);
                if (!entity) continue;
                entity->position = state.position;
                entity->velocity = state.linear_velocity;
                entity->rotation = {state.rotation[0], state.rotation[1], state.rotation[2]};
                std::vector<homeworldz::scene::EntityId> linked_children;
                for (const auto& [candidate_id, candidate] : scene.entities())
                    if (candidate.parent_id == entity_id) linked_children.push_back(candidate_id);
                for (const auto child_id : linked_children)
                    if (auto* child = scene.find(child_id))
                        homeworldz::scene::update_linked_world_transform(*child, *entity);
                for (const auto& [recipient_endpoint, recipient] : avatars) {
                    const auto radius = homeworldz::physics::linkset_bounding_radius(
                        scene, *entity);
                    auto& recipient_cache = sent_dynamic_transforms[recipient_endpoint];
                    if (!homeworldz::physics::within_viewer_interest(
                            recipient.controller.state().position, entity->position,
                            recipient.controller.state().draw_distance, radius)) {
                        recipient_cache.erase(entity_id);
                        continue;
                    }
                    const auto previous = recipient_cache.find(entity_id);
                    const bool heartbeat_due = previous != recipient_cache.end() &&
                        now - previous->second.sent_at >= std::chrono::seconds(1);
                    if (previous != recipient_cache.end() && !heartbeat_due &&
                        !homeworldz::physics::body_transform_changed(
                            previous->second.state, state))
                        continue;
                    const auto object = static_object_from_entity(scene, *entity, recipient.user_id, falcon);
                    if (!object) continue;
                    if (const auto sent = circuits.send(recipient_endpoint,
                            homeworldz::viewer::encode_static_object_update(
                                region_handle, *object), false, now, true)) {
                        if (send_udp(viewer_server, recipient_endpoint, *sent))
                            recipient_cache.insert_or_assign(
                                entity_id, SentDynamicTransform{state, now});
                    }
                }
            }
            for (auto& [recipient_endpoint, cache] : sent_dynamic_transforms) {
                static_cast<void>(recipient_endpoint);
                std::erase_if(cache, [&](const auto& entry) {
                    const auto* entity = scene.find(entry.first);
                    return !entity || !entity->physical || entity->phantom;
                });
            }
            next_dynamic_sync = now + std::chrono::milliseconds(100);
        }
        std::unordered_set<homeworldz::scene::EntityId> expired_temporary_roots;
        std::vector<homeworldz::scene::EntityId> stale_temporary_expirations;
        for (const auto& [entity_id, expires_at] : temporary_expirations) {
            const auto* entity = scene.find(entity_id);
            if (!entity || !entity->temporary) {
                stale_temporary_expirations.push_back(entity_id);
                continue;
            }
            if (now < expires_at) continue;
            expired_temporary_roots.insert(
                entity->parent_id == 0 ? entity_id : entity->parent_id);
        }
        for (const auto entity_id : stale_temporary_expirations)
            temporary_expirations.erase(entity_id);
        for (const auto root_id : expired_temporary_roots) {
            std::vector<homeworldz::scene::EntityId> part_ids{root_id};
            for (const auto& [candidate_id, candidate] : scene.entities())
                if (candidate.parent_id == root_id) part_ids.push_back(candidate_id);
            std::vector<std::uint32_t> local_ids;
            local_ids.reserve(part_ids.size());
            for (const auto part_id : part_ids) {
                local_ids.push_back(static_cast<std::uint32_t>(part_id));
                if (physics_scene) static_cast<void>(physics_scene->remove(part_id));
                static_cast<void>(scene.remove(part_id));
                temporary_expirations.erase(part_id);
            }
            const auto payload = homeworldz::viewer::encode_kill_object(local_ids);
            for (const auto& [recipient_endpoint, recipient] : avatars) {
                static_cast<void>(recipient);
                if (const auto outgoing = circuits.send(
                        recipient_endpoint, payload, true, now, true))
                    static_cast<void>(send_udp(viewer_server, recipient_endpoint, *outgoing));
            }
            std::cout << "{\"level\":\"info\",\"message\":\"temporary object expired\",\"rootEntityId\":"
                      << root_id << ",\"parts\":" << part_ids.size() << "}" << std::endl;
        }
        previous_tick = now;
        if (now >= next_snapshot) {
            try {
                storage->save_snapshot(scene);
                next_snapshot = now + std::chrono::seconds(30);
            } catch (const std::exception& error) {
                std::cerr << "{\"level\":\"error\",\"message\":\"save scene snapshot failed\",\"error\":"
                          << homeworldz::api::json_string(error.what()) << "}" << std::endl;
                running = false;
            }
        }
        if (registration && !registration->tick(now)) {
            std::cerr << "{\"level\":\"error\",\"message\":\"region lease renewal failed\"}" << std::endl;
            running = false;
        }
        if (viewer_grid && now >= next_neighbor_refresh)
            static_cast<void>(refresh_region_neighbors(false));
    }
    // Graceful shutdown (SIGINT/SIGTERM set running=false): tell every connected
    // viewer why it is being disconnected — a KickUser with a reason string, so
    // it shows a clear message instead of a generic timeout/crash — then give the
    // datagrams a few seconds to leave and be processed before the process exits.
    if (!avatars.empty()) {
        const auto shutdown_now = std::chrono::steady_clock::now();
        const std::string reason = "This region is restarting. Please log back in shortly.";
        std::size_t kicked = 0;
        for (const auto& [endpoint, avatar] : avatars) {
            static_cast<void>(avatar);
            const auto* id = circuits.identity(endpoint);
            if (id == nullptr) continue;
            const auto kick = homeworldz::viewer::encode_kick_user(
                id->agent_id, id->session_id, reason);
            if (kick.empty()) continue;
            if (const auto framed = circuits.send(endpoint, kick, true, shutdown_now, true)) {
                static_cast<void>(send_udp(viewer_server, endpoint, *framed));
                ++kicked;
            }
        }
        std::cout << "{\"level\":\"info\",\"message\":\"region shutdown: viewers kicked\",\"count\":"
                  << kicked << "}" << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(5));
        std::cout.flush();
        std::cerr.flush();
    }
    try {
        storage->save_snapshot(scene);
    } catch (const std::exception& error) {
        std::cerr << "{\"level\":\"error\",\"message\":\"final scene snapshot failed\",\"error\":"
                  << homeworldz::api::json_string(error.what()) << "}" << std::endl;
    }
    if (registration) registration->stop();
    for (const auto& pending : pending_event_responses) close_socket(pending.client);
    close_socket(viewer_server);
    close_socket(server);
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
