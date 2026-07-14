#include <array>
#include <algorithm>
#include <atomic>
#include <charconv>
#include <cctype>
#include <csignal>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <deque>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "homeworldz/api_models.h"
#include "homeworldz/avatar_controller.h"
#include "homeworldz/grid_client.h"
#include "homeworldz/http_response.h"
#include "homeworldz/region_config.h"
#include "homeworldz/region_storage.h"
#include "homeworldz/scene.h"
#include "homeworldz/simulation_loop.h"
#include "homeworldz/viewer_capabilities.h"
#include "homeworldz/viewer_protocol.h"

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
homeworldz::config::RegionSettings configured_values;

struct LiveAvatar {
    homeworldz::viewer::AvatarController controller;
    homeworldz::scene::EntityId entity_id{};
    std::string user_id;
    std::chrono::steady_clock::time_point next_ping{};
    std::chrono::steady_clock::time_point next_presence{};
    std::uint8_t ping_id{};
};

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

void stop(int) { running = false; }

std::string environment_value(const char* name, std::string fallback = {}) {
#ifdef _WIN32
    char* value = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&value, &length, name) == 0 && value != nullptr) {
        std::string result(value);
        std::free(value);
        return result;
    }
#else
    if (const char* value = std::getenv(name)) return value;
#endif
    const auto configured = configured_values.find(name);
    if (configured != configured_values.end()) return configured->second;
    return fallback;
}

std::optional<std::array<float, 256 * 256>> load_raw_heightmap(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input || input.tellg() != 256 * 256) return std::nullopt;
    input.seekg(0);
    std::array<unsigned char, 256 * 256> source{};
    input.read(reinterpret_cast<char*>(source.data()), source.size());
    if (!input) return std::nullopt;
    std::array<float, 256 * 256> result{};
    std::transform(source.begin(), source.end(), result.begin(),
                   [](unsigned char height) { return static_cast<float>(height); });
    return result;
}

homeworldz::scene::Vector3 default_spawn(
    const std::optional<std::array<float, 256 * 256>>& heightmap) {
    if (!heightmap) return {128.0, 128.0, 25.0};
    std::size_t selected_x = 128;
    std::size_t selected_y = 128;
    std::size_t selected_distance = (std::numeric_limits<std::size_t>::max)();
    for (std::size_t y = 16; y < 240; ++y) {
        for (std::size_t x = 16; x < 240; ++x) {
            const auto height = (*heightmap)[y * 256 + x];
            if (height < 24.0F) continue;
            const auto dx = static_cast<std::int64_t>(x) - 128;
            const auto dy = static_cast<std::int64_t>(y) - 128;
            const auto distance = static_cast<std::size_t>(dx * dx + dy * dy);
            if (distance < selected_distance) {
                selected_x = x;
                selected_y = y;
                selected_distance = distance;
            }
        }
    }
    const auto height = (*heightmap)[selected_y * 256 + selected_x];
    return {static_cast<double>(selected_x), static_cast<double>(selected_y), height + 1.0};
}

double ground_height(const std::optional<std::array<float, 256 * 256>>& heightmap,
                     const homeworldz::scene::Vector3& position) {
    if (!heightmap) return 25.0;
    const auto x = std::clamp(static_cast<int>(position.x), 0, 255);
    const auto y = std::clamp(static_cast<int>(position.y), 0, 255);
    return (*heightmap)[static_cast<std::size_t>(y) * 256 + x];
}

int environment_int(const char* name, int fallback, int minimum, int maximum) {
    const auto value = environment_value(name);
    if (value.empty()) return fallback;
    const int parsed = std::atoi(value.c_str());
    return parsed >= minimum && parsed <= maximum ? parsed : fallback;
}

int configured_port() {
    return environment_int("HOMEWORLDZ_REGION_PORT", 42001, 1, 65535);
}

int configured_viewer_port() {
    return environment_int("HOMEWORLDZ_VIEWER_PORT", 42002, 1, 65535);
}

bool configured_bind_address(sockaddr_in& address, const char* environment_name, int port) {
    address = {};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<unsigned short>(port));
    const auto host = environment_value(environment_name, "127.0.0.1");
    if (inet_pton(AF_INET, host.c_str(), &address.sin_addr) == 1) return true;
    std::cerr << "{\"level\":\"error\",\"message\":\"invalid IPv4 bind address\",\"setting\":"
              << homeworldz::api::json_string(environment_name) << ",\"address\":"
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
    const auto session = path.substr(prefix.size());
    if (session.empty() || session.find('/') != std::string_view::npos) return {};
    return std::string(session);
}

std::optional<std::pair<std::string, std::string>> texture_request(std::string_view path) {
    constexpr std::string_view prefix = "/caps/texture/";
    constexpr std::string_view marker = "/?texture_id=";
    if (!path.starts_with(prefix)) return std::nullopt;
    const auto separator = path.find(marker, prefix.size());
    if (separator == std::string_view::npos) return std::nullopt;
    const auto session = path.substr(prefix.size(), separator - prefix.size());
    const auto texture = path.substr(separator + marker.size());
    if (session.empty() || texture.empty() || texture.find('&') != std::string_view::npos)
        return std::nullopt;
    return std::pair{std::string(session), std::string(texture)};
}

std::optional<std::pair<std::string, std::string>> viewer_asset_request(std::string_view path) {
    constexpr std::string_view prefix = "/caps/assets/";
    if (!path.starts_with(prefix)) return std::nullopt;
    const auto separator = path.find("/?", prefix.size());
    if (separator == std::string_view::npos) return std::nullopt;
    const auto session = path.substr(prefix.size(), separator - prefix.size());
    const auto query = path.substr(separator + 2);
    const auto id_marker = query.find("_id=");
    if (session.empty() || id_marker == std::string_view::npos || id_marker == 0 ||
        query.find('&') != std::string_view::npos) return std::nullopt;
    const auto asset = query.substr(id_marker + 4);
    if (asset.empty()) return std::nullopt;
    return std::pair{std::string(session), std::string(asset)};
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

bool jpeg2000_content(std::string_view body) {
    const auto byte = [&body](std::size_t index) { return static_cast<unsigned char>(body[index]); };
    const bool codestream = body.size() >= 4 && byte(0) == 0xff && byte(1) == 0x4f &&
                            byte(2) == 0xff && byte(3) == 0x51;
    const bool jp2 = body.size() >= 12 && byte(0) == 0 && byte(1) == 0 && byte(2) == 0 && byte(3) == 12 &&
                     body.substr(4, 4) == "jP  " && byte(8) == 0x0d && byte(9) == 0x0a &&
                     byte(10) == 0x87 && byte(11) == 0x0a;
    return codestream || jp2;
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

std::optional<homeworldz::viewer::StaticObject> static_object_from_entity(
    const homeworldz::scene::Entity& entity, std::string_view recipient_id) {
    constexpr std::uint32_t object_modify = 0x00000004;
    constexpr std::uint32_t object_copy = 0x00000008;
    constexpr std::uint32_t object_any_owner = 0x00000010;
    constexpr std::uint32_t object_you_owner = 0x00000020;
    constexpr std::uint32_t object_move = 0x00000100;
    constexpr std::uint32_t object_transfer = 0x00020000;
    constexpr std::uint32_t object_owner_modify = 0x10000000;
    if (entity.object_id.empty() || entity.id > (std::numeric_limits<std::uint32_t>::max)())
        return std::nullopt;
    const auto object_id = homeworldz::viewer::parse_uuid(entity.object_id);
    const auto owner_id = homeworldz::viewer::parse_uuid(entity.owner_id);
    if (!object_id || !owner_id) return std::nullopt;
    homeworldz::viewer::StaticObject object;
    object.local_id = static_cast<std::uint32_t>(entity.id);
    object.id = *object_id;
    object.owner_id = *owner_id;
    const bool is_owner = entity.owner_id == recipient_id;
    const auto permissions = is_owner ? entity.owner_permissions : entity.everyone_permissions;
    object.update_flags = object_any_owner;
    if ((permissions & homeworldz::scene::permission_modify) != 0) object.update_flags |= object_modify;
    if ((permissions & homeworldz::scene::permission_copy) != 0) object.update_flags |= object_copy;
    if ((permissions & homeworldz::scene::permission_move) != 0) object.update_flags |= object_move;
    if ((permissions & homeworldz::scene::permission_transfer) != 0) object.update_flags |= object_transfer;
    if (is_owner) object.update_flags |= object_you_owner | object_owner_modify;
    object.material = entity.material;
    object.position = {static_cast<float>(entity.position.x), static_cast<float>(entity.position.y),
                       static_cast<float>(entity.position.z)};
    object.rotation = {static_cast<float>(entity.rotation.x), static_cast<float>(entity.rotation.y),
                       static_cast<float>(entity.rotation.z)};
    object.scale = {static_cast<float>(entity.scale.x), static_cast<float>(entity.scale.y),
                    static_cast<float>(entity.scale.z)};
    return object;
}

std::string object_asset_json(const homeworldz::scene::Entity& entity) {
    return "{\"format\":\"homeworldz-object-v1\",\"creatorId\":" +
        homeworldz::api::json_string(entity.creator_id) + ",\"name\":" +
        homeworldz::api::json_string(entity.name) + ",\"scale\":[" +
        std::to_string(entity.scale.x) + ',' + std::to_string(entity.scale.y) + ',' +
        std::to_string(entity.scale.z) + "],\"rotation\":[" +
        std::to_string(entity.rotation.x) + ',' + std::to_string(entity.rotation.y) + ',' +
        std::to_string(entity.rotation.z) + "],\"description\":" +
        homeworldz::api::json_string(entity.description) +
        ",\"material\":" + std::to_string(entity.material) +
        ",\"basePermissions\":" + std::to_string(entity.base_permissions) +
        ",\"ownerPermissions\":" + std::to_string(entity.owner_permissions) +
        ",\"groupPermissions\":" + std::to_string(entity.group_permissions) +
        ",\"everyonePermissions\":" + std::to_string(entity.everyone_permissions) +
        ",\"nextOwnerPermissions\":" + std::to_string(entity.next_owner_permissions) + '}';
}

std::optional<homeworldz::viewer::ObjectProperties> object_properties_from_entity(
    const homeworldz::scene::Entity& entity) {
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
} // namespace

int main() {
#ifdef _WIN32
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) return 1;
#endif
    std::signal(SIGINT, stop);
    std::signal(SIGTERM, stop);

    try {
        const auto config_directory = environment_value("HOMEWORLDZ_CONFIG_DIR", "config");
        const auto config_path = std::filesystem::path(config_directory) / "region.ini";
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

    const auto region_name = environment_value("HOMEWORLDZ_REGION_NAME", "My Region");
    const auto region_grid_x = environment_int("HOMEWORLDZ_REGION_GRID_X", 1000, 0, 1000000);
    const auto region_grid_y = environment_int("HOMEWORLDZ_REGION_GRID_Y", 1000, 0, 1000000);
    const auto region_public_endpoint = environment_value(
        "HOMEWORLDZ_REGION_PUBLIC_ENDPOINT", "http://localhost:" + std::to_string(configured_port()));
    const auto grid_public_endpoint = environment_value(
        "HOMEWORLDZ_GRID_PUBLIC_URL", environment_value("HOMEWORLDZ_GRID_URL", "http://localhost:42000"));
    const auto terrain_heightmap = load_raw_heightmap(environment_value(
        "HOMEWORLDZ_REGION_TERRAIN_PATH", "assets/region/terrain/plateau-square.raw"));
    if (terrain_heightmap) {
        std::cout << "{\"level\":\"info\",\"message\":\"default terrain heightmap loaded\"}" << std::endl;
    } else {
        std::cout << "{\"level\":\"warning\",\"message\":\"default terrain heightmap unavailable; using flat terrain\"}"
                  << std::endl;
    }
    const auto initial_spawn = default_spawn(terrain_heightmap);
    std::unique_ptr<homeworldz::grid::RegistrationLifecycle> registration;
    std::unique_ptr<homeworldz::grid::Client> viewer_grid;
    std::unique_ptr<homeworldz::grid::ViewerSessionCache> viewer_sessions;
    const auto service_token = environment_value("HOMEWORLDZ_GRID_SERVICE_TOKEN");
    if (!service_token.empty()) {
        try {
            homeworldz::grid::RegionSettings settings{
                region_name, region_grid_x, region_grid_y,
                region_public_endpoint,
                configured_viewer_port(),
                environment_int("HOMEWORLDZ_REGION_LEASE_SECONDS", 60, 10, 300)};
            auto transport = homeworldz::grid::socket_transport(
                environment_value("HOMEWORLDZ_GRID_URL", "http://localhost:42000"), service_token);
            homeworldz::grid::Client client(transport);
            viewer_grid = std::make_unique<homeworldz::grid::Client>(std::move(transport));
            viewer_sessions = std::make_unique<homeworldz::grid::ViewerSessionCache>(*viewer_grid);
            registration = std::make_unique<homeworldz::grid::RegistrationLifecycle>(
                std::move(client), std::move(settings));
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

    homeworldz::scene::Scene scene;
    std::unique_ptr<homeworldz::storage::RegionStorage> storage;
    try {
        storage = std::make_unique<homeworldz::storage::RegionStorage>(
            environment_value("HOMEWORLDZ_REGION_DATA_PATH", "var/region"));
        const auto imported_assets = storage->import_asset_directory(
            environment_value("HOMEWORLDZ_REGION_ASSET_PATH", "assets/region"), system_creator_id);
        if (imported_assets != 0) {
            std::cout << "{\"level\":\"info\",\"message\":\"region assets imported\",\"count\":"
                      << imported_assets << "}" << std::endl;
        }
        if (storage->load_snapshot(scene)) {
            std::cout << "{\"level\":\"info\",\"message\":\"scene snapshot restored\",\"revision\":"
                      << scene.revision() << ",\"entities\":" << scene.size() << "}" << std::endl;
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
    homeworldz::simulation::FixedStepLoop simulation(scene);
    auto previous_tick = std::chrono::steady_clock::now();
    auto next_snapshot = previous_tick + std::chrono::seconds(30);

    const auto server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server == invalid_socket) return 1;
    int reuse = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&reuse), sizeof(reuse));

    sockaddr_in address{};
    if (!configured_bind_address(address, "HOMEWORLDZ_REGION_BIND_ADDRESS", configured_port()) ||
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
    if (!configured_bind_address(viewer_address, "HOMEWORLDZ_VIEWER_BIND_ADDRESS", configured_viewer_port()) ||
        bind(viewer_server, reinterpret_cast<sockaddr*>(&viewer_address), sizeof(viewer_address)) != 0) {
        close_socket(viewer_server);
        close_socket(server);
        return 1;
    }
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
        if (session->destination_region_id != registration->region_id())
            return reject("destination_region_mismatch");
        const auto agent = homeworldz::viewer::parse_uuid(session->agent_id);
        if (!agent) return reject("invalid_session_agent");
        if (*agent != request.agent_id) return reject("agent_id_mismatch");
        std::cout << "{\"level\":\"info\",\"message\":\"viewer circuit authorized\",\"circuitCode\":"
                  << request.circuit_code << ",\"sessionId\":"
                  << homeworldz::api::json_string(homeworldz::viewer::format_uuid(request.session_id))
                  << "}" << std::endl;
        return true;
    });
    std::unordered_set<std::string> handshake_replies;
    std::unordered_set<std::string> established_events;
    std::unordered_map<std::string, LiveAvatar> avatars;
    std::unordered_map<std::string, homeworldz::viewer::UuidName> resolved_avatar_names;
    std::unordered_map<std::string, std::deque<QueuedTexturePacket>> texture_packets;
    std::unordered_set<std::string> active_texture_transfers;
    std::unordered_map<std::string, PendingInventoryUpload> pending_inventory_uploads;

    std::cout << "{\"level\":\"info\",\"message\":\"region service listening\",\"httpPort\":"
              << configured_port() << ",\"viewerPort\":" << configured_viewer_port() << "}" << std::endl;
    while (running) {
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
                const auto received_request = receive_http_request(client);
                if (received_request) {
                    const std::string_view request(*received_request);
                    auto response = homeworldz::http::response_for(request);
                    auto session_id = capability_session(response.path, "/caps/seed/");
                    const bool seed = !session_id.empty();
                    if (!seed) session_id = capability_session(response.path, "/caps/event/");
                    const bool event_queue = !seed && !session_id.empty();
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
                    if (seed || event_queue || texture || viewer_asset || simulator_features || environment_settings ||
                        baked_upload || baked_upload_data || file_upload || file_upload_data) {
                        bool authorized = false;
                        std::string authorized_agent_id;
                        std::optional<homeworldz::grid::ViewerSession> authorized_session;
                        const auto expected_method =
                            texture || viewer_asset || simulator_features || environment_settings ? "GET" : "POST";
                        if (response.method == expected_method && registration && viewer_sessions) {
                            authorized_session = viewer_sessions->validate(session_id);
                            authorized = authorized_session &&
                                         authorized_session->destination_region_id == registration->region_id();
                            if (authorized) authorized_agent_id = authorized_session->agent_id;
                        }
                        if (authorized && seed) {
                            response = homeworldz::http::response_for_content(
                                request, 200, "application/llsd+xml",
                                homeworldz::viewer::seed_capability_xml(
                                    region_public_endpoint, grid_public_endpoint, session_id));
                        } else if (authorized && event_queue) {
                            static std::atomic<std::uint64_t> event_id{0};
                            std::optional<homeworldz::viewer::EstablishAgentCommunication> event;
                            if (established_events.insert(session_id).second) {
                                if (authorized_session) {
                                    event = homeworldz::viewer::EstablishAgentCommunication{
                                        authorized_session->agent_id,
                                        simulator_endpoint(region_public_endpoint, configured_viewer_port()),
                                        region_public_endpoint + "/caps/seed/" + session_id};
                                }
                            }
                            response = homeworldz::http::response_for_content(
                                request, 200, "application/llsd+xml",
                                homeworldz::viewer::event_queue_xml(++event_id, event));
                        } else if (authorized && texture && homeworldz::viewer::parse_uuid(texture->second)) {
                            try {
                                const auto asset = storage->read_asset(texture->second);
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
                                const auto asset = storage->read_asset(viewer_asset->second);
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
                                homeworldz::viewer::simulator_features_xml());
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
                                const auto content = std::span(
                                    reinterpret_cast<const std::byte*>(body.data()), body.size());
                                const auto asset_id = homeworldz::viewer::baked_texture_asset_id(content);
                                storage->store_asset(asset_id, authorized_agent_id, content);
                                response = homeworldz::http::response_for_content(
                                    request, 200, "application/llsd+xml",
                                    homeworldz::viewer::baked_texture_complete_xml(asset_id));
                                std::cout << "{\"level\":\"info\",\"message\":\"baked texture stored\","
                                             "\"assetId\":" << homeworldz::api::json_string(asset_id)
                                          << ",\"bytes\":" << body.size() << "}" << std::endl;
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
                            } else if (!jpeg2000_content(body)) {
                                response = homeworldz::http::response_for_content(
                                    request, 400, "application/llsd+xml", "<llsd><undef/></llsd>");
                            } else {
                                const auto& upload = pending->second;
                                const auto content = std::span(
                                    reinterpret_cast<const std::byte*>(body.data()), body.size());
                                storage->store_asset(upload.asset_id, authorized_agent_id, content);
                                const bool item_created = viewer_grid && viewer_grid->create_texture_inventory_item(
                                    authorized_agent_id, homeworldz::grid::TextureInventoryItem{
                                        upload.item_id, authorized_agent_id, upload.request.folder_id,
                                        upload.asset_id, upload.request.name, upload.request.description,
                                        upload.request.everyone_permissions, upload.request.next_permissions});
                                if (!item_created) {
                                    response = homeworldz::http::response_for_content(
                                        request, 500, "application/llsd+xml", "<llsd><undef/></llsd>");
                                } else {
                                    response = homeworldz::http::response_for_content(
                                        request, 200, "application/llsd+xml",
                                        homeworldz::viewer::new_file_inventory_complete_xml(
                                            upload.item_id, upload.asset_id,
                                            upload.request.everyone_permissions, upload.request.next_permissions));
                                    std::cout << "{\"level\":\"info\",\"message\":\"texture upload stored\","
                                                 "\"assetId\":" << homeworldz::api::json_string(upload.asset_id)
                                              << ",\"itemId\":" << homeworldz::api::json_string(upload.item_id)
                                              << ",\"creatorId\":" << homeworldz::api::json_string(authorized_agent_id)
                                              << ",\"bytes\":" << body.size() << "}" << std::endl;
                                    pending_inventory_uploads.erase(pending);
                                }
                            }
                        } else {
                            response = homeworldz::http::response_for_content(
                                request, response.method == expected_method ? 404 : 405,
                                "application/llsd+xml", "<llsd><undef/></llsd>");
                        }
                    }
                    static_cast<void>(send_all(client, response.content));
                    finish_http_response(client);
                    std::cout << "{\"level\":\"info\",\"message\":\"http request\",\"requestId\":"
                              << homeworldz::api::json_string(response.request_id)
                              << ",\"method\":" << homeworldz::api::json_string(response.method)
                              << ",\"path\":" << homeworldz::api::json_string(response.path)
                              << ",\"status\":" << response.status_code << "}" << std::endl;
                }
                close_socket(client);
            }
        }
        const auto now = std::chrono::steady_clock::now();
        if (ready > 0 && FD_ISSET(viewer_server, &readable)) {
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
                if (packet) {
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
                                static_cast<void>(viewer_grid->clear_presence(user_id));
                                static_cast<void>(viewer_grid->revoke_viewer_session(session_id));
                            }
                            if (viewer_sessions) viewer_sessions->invalidate(session_id);
                            avatars.erase(endpoint);
                            handshake_replies.erase(endpoint);
                            established_events.erase(session_id);
                            texture_packets.erase(endpoint);
                            std::erase_if(active_texture_transfers, [&](const std::string& key) {
                                return key.starts_with(endpoint + '|');
                            });
                            circuits.remove(endpoint);
                            std::cout << "{\"level\":\"info\",\"message\":\"viewer logged out\",\"sessionId\":"
                                      << homeworldz::api::json_string(session_id) << "}" << std::endl;
                            continue;
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
                            copy_item->session_id == identity->session_id &&
                            homeworldz::viewer::format_uuid(copy_item->old_agent_id) == system_creator_id) {
                            const auto user_id = homeworldz::viewer::format_uuid(identity->agent_id);
                            const auto source_id = homeworldz::viewer::format_uuid(copy_item->old_item_id);
                            const auto destination_id = homeworldz::viewer::format_uuid(copy_item->new_folder_id);
                            std::optional<homeworldz::grid::InventoryItem> copied;
                            try {
                                if (viewer_grid) copied = viewer_grid->copy_library_item(
                                    user_id, source_id, destination_id, copy_item->new_name);
                            } catch (const std::exception& error) {
                                std::cout << "{\"level\":\"error\",\"message\":\"library inventory copy failed\",\"error\":"
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
                                      << ",\"message\":\"library inventory copy "
                                      << (sent ? "completed" : "rejected") << "\",\"sourceItemId\":"
                                      << homeworldz::api::json_string(source_id) << "}" << std::endl;
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
                                    const auto asset = storage->read_asset(asset_id);
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
                        if (complete && handshake_replies.contains(endpoint) &&
                            complete->agent_id == identity->agent_id &&
                            complete->session_id == identity->session_id &&
                            complete->circuit_code == identity->circuit_code) {
                            homeworldz::viewer::AgentMovementComplete response;
                            response.agent_id = identity->agent_id;
                            response.session_id = identity->session_id;
                            response.region_handle =
                                (static_cast<std::uint64_t>(region_grid_x * 256) << 32) |
                                static_cast<std::uint32_t>(region_grid_y * 256);
                            response.timestamp = static_cast<std::uint32_t>(
                                std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()));
                            if (!avatars.contains(endpoint)) {
                                const auto name = homeworldz::viewer::format_uuid(identity->agent_id);
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
                                const auto* persisted = scene.find(entity);
                                const auto spawn = persisted ? persisted->position : initial_spawn;
                                response.position = {static_cast<float>(spawn.x), static_cast<float>(spawn.y),
                                                     static_cast<float>(spawn.z)};
                                avatars.emplace(endpoint, LiveAvatar{
                                    homeworldz::viewer::AvatarController{spawn, ground_height(terrain_heightmap, spawn)}, entity, name,
                                    now + std::chrono::seconds(5), now + std::chrono::seconds(30), 0});
                                if (viewer_grid && registration)
                                    static_cast<void>(viewer_grid->update_presence(name, registration->region_id()));
                            }
                            const auto& live_avatar = avatars.at(endpoint);
                            if (const auto* entity = scene.find(live_avatar.entity_id)) {
                                const std::array<float, 3> position{
                                    static_cast<float>(entity->position.x), static_cast<float>(entity->position.y),
                                    static_cast<float>(entity->position.z)};
                                if (const auto avatar = circuits.send(endpoint,
                                        homeworldz::viewer::encode_avatar_object_update(
                                            response.region_handle, static_cast<std::uint32_t>(live_avatar.entity_id),
                                            identity->agent_id, position), true, now, true))
                                    static_cast<void>(send_udp(viewer_server, endpoint, *avatar));
                            }
                            if (const auto outgoing = circuits.send(endpoint,
                                    homeworldz::viewer::encode_agent_movement_complete(response), true, now))
                                static_cast<void>(send_udp(viewer_server, endpoint, *outgoing));
                            for (std::uint8_t y = 0; y < 16; ++y) {
                                std::array<homeworldz::viewer::TerrainPatch, 16> row{};
                                for (std::uint8_t x = 0; x < 16; ++x) row[x] = {x, y};
                                const auto terrain_payload = terrain_heightmap ?
                                    homeworldz::viewer::encode_terrain(row, *terrain_heightmap) :
                                    homeworldz::viewer::encode_flat_terrain(row, 25.0F);
                                if (const auto terrain = circuits.send(endpoint, terrain_payload, true, now))
                                    static_cast<void>(send_udp(viewer_server, endpoint, *terrain));
                            }
                            homeworldz::viewer::StaticObject welcome_prim;
                            if (const auto id = homeworldz::viewer::parse_uuid(
                                    "00000000-0000-4000-8000-000000000001"))
                                welcome_prim.id = *id;
                            welcome_prim.position = {static_cast<float>(initial_spawn.x + 4.0),
                                                     static_cast<float>(initial_spawn.y),
                                                     static_cast<float>(initial_spawn.z + 1.0)};
                            if (const auto object = circuits.send(endpoint,
                                    homeworldz::viewer::encode_static_object_update(
                                        response.region_handle, welcome_prim), true, now, true))
                                static_cast<void>(send_udp(viewer_server, endpoint, *object));
                            for (const auto& [entity_id, entity] : scene.entities()) {
                                static_cast<void>(entity_id);
                                const auto restored_object = static_object_from_entity(entity, live_avatar.user_id);
                                if (!restored_object) continue;
                                if (const auto object = circuits.send(endpoint,
                                        homeworldz::viewer::encode_static_object_update(
                                            response.region_handle, *restored_object), true, now, true))
                                    static_cast<void>(send_udp(viewer_server, endpoint, *object));
                            }
                        }
                        const auto object_select = homeworldz::viewer::decode_object_select(packet->payload);
                        if (object_select && object_select->agent_id == identity->agent_id &&
                            object_select->session_id == identity->session_id) {
                            std::vector<homeworldz::viewer::ObjectProperties> properties;
                            properties.reserve(object_select->local_ids.size());
                            for (const auto local_id : object_select->local_ids) {
                                const auto* entity = scene.find(local_id);
                                if (!entity) continue;
                                if (const auto object = object_properties_from_entity(*entity))
                                    properties.push_back(*object);
                            }
                            auto response = homeworldz::viewer::encode_object_properties(properties);
                            if (!response.empty()) {
                                if (const auto outgoing = circuits.send(
                                        endpoint, std::move(response), true, now, true))
                                    static_cast<void>(send_udp(viewer_server, endpoint, *outgoing));
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
                                const auto properties = object_properties_from_entity(entity);
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
                            struct OriginalTransform {
                                homeworldz::scene::Vector3 position;
                                homeworldz::scene::Vector3 rotation;
                                homeworldz::scene::Vector3 scale;
                            };
                            std::unordered_map<homeworldz::scene::EntityId, OriginalTransform> originals;
                            std::unordered_set<homeworldz::scene::EntityId> requested_entities;
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
                                    (finite_vector(*update.position) && (*update.position)[0] >= 0.0F &&
                                     (*update.position)[0] <= 256.0F && (*update.position)[1] >= 0.0F &&
                                     (*update.position)[1] <= 256.0F && (*update.position)[2] >= -64.0F &&
                                     (*update.position)[2] <= 4096.0F);
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
                                originals.try_emplace(entity->id, OriginalTransform{
                                    entity->position, entity->rotation, entity->scale});
                                if (update.position)
                                    entity->position = {(*update.position)[0], (*update.position)[1],
                                                        (*update.position)[2]};
                                if (update.rotation)
                                    entity->rotation = {(*update.rotation)[0], (*update.rotation)[1],
                                                        (*update.rotation)[2]};
                                if (update.scale)
                                    entity->scale = {(*update.scale)[0], (*update.scale)[1], (*update.scale)[2]};
                            }
                            bool persisted = false;
                            if (!originals.empty()) {
                                try {
                                    storage->save_snapshot(scene);
                                    persisted = true;
                                } catch (const std::exception& error) {
                                    for (const auto& [entity_id, original] : originals) {
                                        if (auto* entity = scene.find(entity_id)) {
                                            entity->position = original.position;
                                            entity->rotation = original.rotation;
                                            entity->scale = original.scale;
                                        }
                                    }
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
                                for (const auto& [recipient_endpoint, recipient] : avatars) {
                                    const auto object = static_object_from_entity(*entity, recipient.user_id);
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
                                    if (const auto current = object_properties_from_entity(*entity))
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
                                    if (const auto current = object_properties_from_entity(*entity))
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
                                    if (const auto current = object_properties_from_entity(*entity))
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
                                        const auto object = static_object_from_entity(*entity, recipient.user_id);
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
                            std::unordered_set<std::uint32_t> requested_ids;
                            if (finite_offset && supported_flags) {
                                for (const auto local_id : object_duplicate->local_ids) {
                                    if (!requested_ids.insert(local_id).second) continue;
                                    const auto* source = scene.find(local_id);
                                    if (!source || source->owner_id != user_id ||
                                        (source->owner_permissions & homeworldz::scene::permission_copy) == 0)
                                        continue;
                                    const auto source_copy = *source;
                                    const homeworldz::scene::Vector3 position{
                                        source_copy.position.x + object_duplicate->offset[0],
                                        source_copy.position.y + object_duplicate->offset[1],
                                        source_copy.position.z + object_duplicate->offset[2]};
                                    if (position.x < 0.0 || position.x > 256.0 ||
                                        position.y < 0.0 || position.y > 256.0 ||
                                        position.z < -64.0 || position.z > 4096.0)
                                        continue;
                                    const auto entity_id = scene.create(source_copy.name, position);
                                    auto* duplicate = scene.find(entity_id);
                                    if (!duplicate) continue;
                                    *duplicate = source_copy;
                                    duplicate->id = entity_id;
                                    duplicate->position = position;
                                    duplicate->velocity = {};
                                    duplicate->object_id = homeworldz::viewer::random_uuid();
                                    duplicate->creation_date = static_cast<std::uint64_t>(
                                        std::chrono::duration_cast<std::chrono::seconds>(
                                            std::chrono::system_clock::now().time_since_epoch()).count());
                                    created_entities.push_back(entity_id);
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
                                    for (const auto& [recipient_endpoint, recipient] : avatars) {
                                        auto object = static_object_from_entity(*entity, recipient.user_id);
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
                        const auto object_add = homeworldz::viewer::decode_object_add(packet->payload);
                        if (object_add && object_add->agent_id == identity->agent_id &&
                            object_add->session_id == identity->session_id) {
                            const auto valid_scale = std::all_of(
                                object_add->scale.begin(), object_add->scale.end(),
                                [](float value) { return value >= 0.01F && value <= 64.0F; });
                            const bool supported_box = object_add->pcode == 9 &&
                                object_add->path_curve == 16 && (object_add->profile_curve & 0x0f) == 1;
                            std::optional<homeworldz::scene::Vector3> placement;
                            if (valid_scale && object_add->bypass_raycast) {
                                const homeworldz::scene::Vector3 ray_end{
                                    object_add->ray_end[0], object_add->ray_end[1], object_add->ray_end[2]};
                                placement = homeworldz::scene::Vector3{
                                    ray_end.x, ray_end.y,
                                    ground_height(terrain_heightmap, ray_end) + object_add->scale[2] * 0.5};
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
                            const bool valid_position = placement && placement->x >= 0.0 && placement->x <= 256.0 &&
                                placement->y >= 0.0 && placement->y <= 256.0 &&
                                placement->z >= -64.0 && placement->z <= 4096.0;
                            bool created = false;
                            std::string object_id;
                            homeworldz::scene::EntityId entity_id{};
                            if (supported_box && valid_position && object_add->material <= 7) {
                                object_id = homeworldz::viewer::random_uuid();
                                const auto owner_id = homeworldz::viewer::format_uuid(identity->agent_id);
                                entity_id = scene.create("Primitive", *placement);
                                if (auto* entity = scene.find(entity_id)) {
                                    entity->object_id = object_id;
                                    entity->owner_id = owner_id;
                                    entity->creator_id = owner_id;
                                    entity->scale = {object_add->scale[0], object_add->scale[1], object_add->scale[2]};
                                    entity->material = object_add->material;
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
                                    const auto region_handle =
                                        (static_cast<std::uint64_t>(region_grid_x * 256) << 32) |
                                        static_cast<std::uint32_t>(region_grid_y * 256);
                                    for (const auto& [recipient_endpoint, recipient] : avatars) {
                                        const auto object = static_object_from_entity(*entity, recipient.user_id);
                                        if (!object) continue;
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
                                      << homeworldz::api::json_string(object_id) << "}" << std::endl;
                        }
                        const auto derez = homeworldz::viewer::decode_derez_object(packet->payload);
                        if (derez && derez->agent_id == identity->agent_id &&
                            derez->session_id == identity->session_id) {
                            const auto user_id = homeworldz::viewer::format_uuid(identity->agent_id);
                            const auto destination_id = homeworldz::viewer::format_uuid(derez->destination_id);
                            std::vector<std::uint32_t> removed_ids;
                            std::size_t inventory_items_created = 0;
                            if (derez->destination == 6 && derez->packet_count > 0 &&
                                derez->packet_number > 0 && derez->packet_number <= derez->packet_count) {
                                for (const auto local_id : derez->local_ids) {
                                    const auto* entity = scene.find(local_id);
                                    if (!entity || entity->object_id.empty() || entity->owner_id != user_id)
                                        continue;
                                    const auto asset_id = homeworldz::viewer::random_uuid();
                                    const auto item_id = homeworldz::viewer::random_uuid();
                                    const auto content_text = object_asset_json(*entity);
                                    const auto content = std::span(
                                        reinterpret_cast<const std::byte*>(content_text.data()), content_text.size());
                                    bool item_created = false;
                                    try {
                                        storage->store_asset(asset_id, entity->creator_id, content);
                                        item_created = viewer_grid && viewer_grid->create_object_inventory_item(
                                            user_id, homeworldz::grid::ObjectInventoryItem{
                                                item_id, entity->creator_id, destination_id, asset_id,
                                                entity->name, "", entity->base_permissions,
                                                entity->owner_permissions, entity->everyone_permissions,
                                                entity->next_owner_permissions});
                                    } catch (const std::exception& error) {
                                        std::cout << "{\"level\":\"error\",\"message\":\"primitive derez inventory failed\",\"error\":"
                                                  << homeworldz::api::json_string(error.what()) << "}" << std::endl;
                                    }
                                    if (!item_created) continue;
                                    homeworldz::viewer::InventoryItem item;
                                    item.item_id = *homeworldz::viewer::parse_uuid(item_id);
                                    item.creator_id = *homeworldz::viewer::parse_uuid(entity->creator_id);
                                    item.owner_id = identity->agent_id;
                                    item.folder_id = derez->destination_id;
                                    item.asset_id = *homeworldz::viewer::parse_uuid(asset_id);
                                    item.asset_type = 6;
                                    item.inventory_type = 6;
                                    item.name = entity->name;
                                    item.base_permissions = entity->base_permissions;
                                    item.current_permissions = entity->owner_permissions;
                                    item.everyone_permissions = entity->everyone_permissions;
                                    item.next_permissions = entity->next_owner_permissions;
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
                                    if (scene.remove(local_id)) {
                                        removed_ids.push_back(local_id);
                                        ++inventory_items_created;
                                    }
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
                                const auto kill = homeworldz::viewer::encode_kill_object(removed_ids);
                                for (const auto& [recipient_endpoint, recipient] : avatars) {
                                    static_cast<void>(recipient);
                                    if (const auto outgoing = circuits.send(
                                            recipient_endpoint, kill, true, now))
                                        static_cast<void>(send_udp(viewer_server, recipient_endpoint, *outgoing));
                                }
                            }
                            std::cout << "{\"level\":"
                                      << (persisted && removed_ids.size() == derez->local_ids.size() ? "\"info\"" : "\"warn\"")
                                      << ",\"message\":\"primitive derez batch processed\",\"removed\":"
                                      << removed_ids.size() << ",\"inventoryItemsCreated\":"
                                      << inventory_items_created << ",\"requested\":" << derez->local_ids.size()
                                      << ",\"destination\":" << static_cast<unsigned>(derez->destination) << "}"
                                      << std::endl;
                        }
                        const auto update = homeworldz::viewer::decode_agent_update(packet->payload);
                        const auto avatar = avatars.find(endpoint);
                        if (update && avatar != avatars.end() && update->agent_id == identity->agent_id &&
                            update->session_id == identity->session_id)
                            avatar->second.controller.apply(*update);
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
        const auto elapsed = std::chrono::duration<double>(now - previous_tick).count();
        simulation.advance(elapsed);
        for (auto& [endpoint, avatar] : avatars) {
            if (now >= avatar.next_ping) {
                if (const auto ping = circuits.send(endpoint,
                        homeworldz::viewer::encode_start_ping_check(++avatar.ping_id), false, now))
                    static_cast<void>(send_udp(viewer_server, endpoint, *ping));
                avatar.next_ping = now + std::chrono::seconds(5);
            }
            if (now >= avatar.next_presence && viewer_grid && registration) {
                static_cast<void>(viewer_grid->update_presence(avatar.user_id, registration->region_id()));
                avatar.next_presence = now + std::chrono::seconds(30);
            }
            avatar.controller.step(elapsed);
            if (auto* entity = scene.find(avatar.entity_id)) {
                entity->position = avatar.controller.state().position;
                entity->velocity = avatar.controller.state().velocity;
            }
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
    }
    try {
        storage->save_snapshot(scene);
    } catch (const std::exception& error) {
        std::cerr << "{\"level\":\"error\",\"message\":\"final scene snapshot failed\",\"error\":"
                  << homeworldz::api::json_string(error.what()) << "}" << std::endl;
    }
    if (registration) registration->stop();
    close_socket(viewer_server);
    close_socket(server);
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
