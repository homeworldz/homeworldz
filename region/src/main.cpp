#include <array>
#include <atomic>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

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

std::string response_for(std::string_view request) {
    std::string_view status = "HTTP/1.1 404 Not Found\r\n";
    std::string_view body = R"({"code":"not_found","message":"route not found"})";
    if (request.starts_with("GET /ping ")) {
        status = "HTTP/1.1 200 OK\r\n";
        body = R"({"status":"ok"})";
    } else if (request.starts_with("GET /ready ")) {
        status = "HTTP/1.1 200 OK\r\n";
        body = R"({"status":"ready"})";
    } else if (request.starts_with("GET /version ")) {
        status = "HTTP/1.1 200 OK\r\n";
        body = R"({"service":"region","version":"dev","apiVersion":"v1"})";
    }
    return std::string(status) + "Content-Type: application/json\r\nConnection: close\r\nContent-Length: " +
           std::to_string(body.size()) + "\r\n\r\n" + std::string(body);
}

int configured_port() {
    if (const char* value = std::getenv("HOMEWORLDZ_REGION_PORT")) {
        const int port = std::atoi(value);
        if (port > 0 && port <= 65535) return port;
    }
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
            const auto response = response_for(std::string_view(buffer.data(), received));
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
