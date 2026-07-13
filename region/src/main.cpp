#include <array>
#include <atomic>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

#include "homeworldz/http_response.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
using socket_handle = SOCKET;
constexpr socket_handle invalid_socket = INVALID_SOCKET;
static void close_socket(socket_handle socket) { closesocket(socket); }
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using socket_handle = int;
constexpr socket_handle invalid_socket = -1;
static void close_socket(socket_handle socket) { close(socket); }
#endif

namespace {
std::atomic_bool running{true};

void stop(int) { running = false; }

int configured_port() {
#ifdef _WIN32
    char* value = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&value, &length, "HOMEWORLDZ_REGION_PORT") == 0 && value != nullptr) {
        const int port = std::atoi(value);
        std::free(value);
        if (port > 0 && port <= 65535) return port;
    }
#else
    if (const char* value = std::getenv("HOMEWORLDZ_REGION_PORT")) {
        const int port = std::atoi(value);
        if (port > 0 && port <= 65535) return port;
    }
#endif
    return 42001;
}
} // namespace

int main() {
#ifdef _WIN32
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) return 1;
#endif
    std::signal(SIGINT, stop);
    std::signal(SIGTERM, stop);

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

    std::cout << "{\"level\":\"info\",\"message\":\"region service listening\",\"port\":"
              << configured_port() << "}" << std::endl;
    while (running) {
        const auto client = accept(server, nullptr, nullptr);
        if (client == invalid_socket) continue;
        std::array<char, 4096> buffer{};
        const auto received = recv(client, buffer.data(), static_cast<int>(buffer.size() - 1), 0);
        if (received > 0) {
            const auto response = homeworldz::http::response_for(std::string_view(buffer.data(), received));
            send(client, response.data(), static_cast<int>(response.size()), 0);
        }
        close_socket(client);
    }
    close_socket(server);
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
