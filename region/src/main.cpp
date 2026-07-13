#include <array>
#include <algorithm>
#include <atomic>
#include <charconv>
#include <csignal>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

#include "homeworldz/api_models.h"
#include "homeworldz/avatar_controller.h"
#include "homeworldz/grid_client.h"
#include "homeworldz/http_response.h"
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

struct LiveAvatar {
    homeworldz::viewer::AvatarController controller;
    homeworldz::scene::EntityId entity_id{};
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
    return fallback;
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

std::string capability_session(std::string_view path, std::string_view prefix) {
    if (!path.starts_with(prefix)) return {};
    const auto session = path.substr(prefix.size());
    if (session.empty() || session.find('/') != std::string_view::npos) return {};
    return std::string(session);
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
} // namespace

int main() {
#ifdef _WIN32
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) return 1;
#endif
    std::signal(SIGINT, stop);
    std::signal(SIGTERM, stop);

    const auto region_name = environment_value("HOMEWORLDZ_REGION_NAME", "My Region");
    const auto region_grid_x = environment_int("HOMEWORLDZ_REGION_GRID_X", 1000, 0, 1000000);
    const auto region_grid_y = environment_int("HOMEWORLDZ_REGION_GRID_Y", 1000, 0, 1000000);
    const auto region_public_endpoint = environment_value(
        "HOMEWORLDZ_REGION_PUBLIC_ENDPOINT", "http://localhost:" + std::to_string(configured_port()));
    std::unique_ptr<homeworldz::grid::RegistrationLifecycle> registration;
    std::unique_ptr<homeworldz::grid::Client> viewer_grid;
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

    std::unique_ptr<homeworldz::storage::RegionStorage> storage;
    try {
        storage = std::make_unique<homeworldz::storage::RegionStorage>(
            environment_value("HOMEWORLDZ_REGION_DATA_PATH", "var/region"));
    } catch (const std::exception& error) {
        std::cerr << "{\"level\":\"error\",\"message\":\"open region storage failed\",\"error\":"
                  << homeworldz::api::json_string(error.what()) << "}" << std::endl;
        if (registration) registration->stop();
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }
    homeworldz::scene::Scene scene;
    homeworldz::simulation::FixedStepLoop simulation(scene);
    auto previous_tick = std::chrono::steady_clock::now();
    auto next_snapshot = previous_tick + std::chrono::seconds(30);

    const auto server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server == invalid_socket) return 1;
    int reuse = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&reuse), sizeof(reuse));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(static_cast<unsigned short>(configured_port()));
    if (bind(server, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
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
    viewer_address.sin_family = AF_INET;
    viewer_address.sin_addr.s_addr = htonl(INADDR_ANY);
    viewer_address.sin_port = htons(static_cast<unsigned short>(configured_viewer_port()));
    if (bind(viewer_server, reinterpret_cast<sockaddr*>(&viewer_address), sizeof(viewer_address)) != 0) {
        close_socket(viewer_server);
        close_socket(server);
        return 1;
    }
    homeworldz::viewer::CircuitRegistry circuits([&](const homeworldz::viewer::UseCircuitCode& request) {
        if (!registration || !viewer_grid) return false;
        const auto session = viewer_grid->validate_viewer_session(homeworldz::viewer::format_uuid(request.session_id));
        if (!session || session->circuit_code != request.circuit_code ||
            session->destination_region_id != registration->region_id()) return false;
        const auto agent = homeworldz::viewer::parse_uuid(session->agent_id);
        return agent && *agent == request.agent_id;
    });
    std::unordered_set<std::string> handshake_replies;
    std::unordered_set<std::string> established_events;
    std::unordered_map<std::string, LiveAvatar> avatars;

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
                std::array<char, 4096> buffer{};
                const auto received = recv(client, buffer.data(), static_cast<int>(buffer.size() - 1), 0);
                if (received > 0) {
                    const std::string_view request(buffer.data(), received);
                    auto response = homeworldz::http::response_for(request);
                    auto session_id = capability_session(response.path, "/caps/seed/");
                    const bool seed = !session_id.empty();
                    if (!seed) session_id = capability_session(response.path, "/caps/event/");
                    const bool event_queue = !seed && !session_id.empty();
                    if (seed || event_queue) {
                        bool authorized = false;
                        if (response.method == "POST" && registration && viewer_grid) {
                            const auto session = viewer_grid->validate_viewer_session(session_id);
                            authorized = session && session->destination_region_id == registration->region_id();
                        }
                        if (authorized && seed) {
                            response = homeworldz::http::response_for_content(
                                request, 200, "application/llsd+xml",
                                homeworldz::viewer::seed_capability_xml(region_public_endpoint, session_id));
                        } else if (authorized) {
                            static std::atomic<std::uint64_t> event_id{0};
                            std::optional<homeworldz::viewer::EstablishAgentCommunication> event;
                            if (established_events.insert(session_id).second) {
                                const auto session = viewer_grid->validate_viewer_session(session_id);
                                if (session) {
                                    event = homeworldz::viewer::EstablishAgentCommunication{
                                        session->agent_id,
                                        simulator_endpoint(region_public_endpoint, configured_viewer_port()),
                                        region_public_endpoint + "/caps/seed/" + session_id};
                                }
                            }
                            response = homeworldz::http::response_for_content(
                                request, 200, "application/llsd+xml",
                                homeworldz::viewer::event_queue_xml(++event_id, event));
                        } else {
                            response = homeworldz::http::response_for_content(
                                request, response.method == "POST" ? 404 : 405,
                                "application/llsd+xml", "<llsd><undef/></llsd>");
                        }
                    }
                    send(client, response.content.data(), static_cast<int>(response.content.size()), 0);
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
                            if (const auto response = circuits.send(endpoint,
                                    homeworldz::viewer::encode_region_handshake(handshake), true, now, true))
                                static_cast<void>(send_udp(viewer_server, endpoint, *response));
                        }
                    } else if (identity) {
                        const auto handshake_reply = homeworldz::viewer::decode_region_handshake_reply(packet->payload);
                        if (handshake_reply && handshake_reply->agent_id == identity->agent_id &&
                            handshake_reply->session_id == identity->session_id) {
                            handshake_replies.insert(endpoint);
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
                                const auto entity = scene.create(name, {128.0, 128.0, 25.0});
                                avatars.emplace(endpoint, LiveAvatar{homeworldz::viewer::AvatarController{}, entity});
                            }
                            if (const auto outgoing = circuits.send(endpoint,
                                    homeworldz::viewer::encode_agent_movement_complete(response), true, now))
                                static_cast<void>(send_udp(viewer_server, endpoint, *outgoing));
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
        const auto elapsed = std::chrono::duration<double>(now - previous_tick).count();
        simulation.advance(elapsed);
        for (auto& [endpoint, avatar] : avatars) {
            static_cast<void>(endpoint);
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
