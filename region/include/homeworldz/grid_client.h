#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace homeworldz::grid {

struct HttpResponse {
    int status_code{};
    std::string body;
};

class Transport {
public:
    virtual ~Transport() = default;
    virtual HttpResponse send(std::string_view method, std::string_view path,
                              std::string_view body) = 0;
};

std::shared_ptr<Transport> socket_transport(std::string grid_url, std::string service_token);

struct RegionSettings {
    std::string name;
    int grid_x{};
    int grid_y{};
    std::string public_endpoint;
    int viewer_port{42002};
    int lease_seconds{60};
};

struct ViewerSession {
    std::string session_id;
    std::string agent_id;
    std::uint32_t circuit_code{};
    std::string destination_region_id;
};

class Client {
public:
    explicit Client(std::shared_ptr<Transport> transport) : transport_(std::move(transport)) {}
    std::string register_region(const RegionSettings& settings);
    bool renew_lease(std::string_view region_id, int lease_seconds);
    bool deregister(std::string_view region_id);
    std::optional<ViewerSession> validate_viewer_session(std::string_view session_id);
    bool revoke_viewer_session(std::string_view session_id);
    bool update_presence(std::string_view user_id, std::string_view region_id);
    bool clear_presence(std::string_view user_id);

private:
    std::shared_ptr<Transport> transport_;
};

class ViewerSessionCache {
public:
    explicit ViewerSessionCache(Client& client,
                                std::chrono::steady_clock::duration ttl = std::chrono::seconds(5))
        : client_(client), ttl_(ttl) {}
    std::optional<ViewerSession> validate(
        std::string_view session_id,
        std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now());
    void invalidate(std::string_view session_id);

private:
    struct Entry {
        ViewerSession session;
        std::chrono::steady_clock::time_point expires_at;
    };
    Client& client_;
    std::chrono::steady_clock::duration ttl_;
    std::unordered_map<std::string, Entry> entries_;
};

class RegistrationLifecycle {
public:
    RegistrationLifecycle(Client client, RegionSettings settings);
    bool start(std::chrono::steady_clock::time_point now);
    bool tick(std::chrono::steady_clock::time_point now);
    void stop();
    const std::string& region_id() const { return region_id_; }

private:
    Client client_;
    RegionSettings settings_;
    std::string region_id_;
    std::chrono::steady_clock::time_point renew_at_{};
};

} // namespace homeworldz::grid
