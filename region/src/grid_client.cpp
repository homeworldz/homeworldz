#include "homeworldz/grid_client.h"

#include "homeworldz/api_models.h"

#include <atomic>
#include <charconv>
#include <stdexcept>
#include <utility>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
using grid_socket = SOCKET;
constexpr grid_socket invalid_grid_socket = INVALID_SOCKET;
static void close_grid_socket(grid_socket socket) { closesocket(socket); }
#else
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
using grid_socket = int;
constexpr grid_socket invalid_grid_socket = -1;
static void close_grid_socket(grid_socket socket) { close(socket); }
#endif

namespace homeworldz::grid {
namespace {

class SocketTransport final : public Transport {
public:
    SocketTransport(std::string grid_url, std::string service_token)
        : service_token_(std::move(service_token)) {
        constexpr std::string_view prefix = "http://";
        if (!grid_url.starts_with(prefix)) throw std::invalid_argument("grid URL must use http:// in development");
        auto authority = std::string_view(grid_url).substr(prefix.size());
        const auto slash = authority.find('/');
        if (slash != std::string_view::npos) {
            base_path_ = authority.substr(slash);
            authority = authority.substr(0, slash);
        }
        const auto colon = authority.rfind(':');
        host_ = authority.substr(0, colon);
        port_ = colon == std::string_view::npos ? "80" : std::string(authority.substr(colon + 1));
        if (host_.empty()) throw std::invalid_argument("grid URL host is required");
    }

    HttpResponse send(std::string_view method, std::string_view path,
                      std::string_view body) override {
        addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        addrinfo* addresses = nullptr;
        if (getaddrinfo(host_.c_str(), port_.c_str(), &hints, &addresses) != 0) {
            throw std::runtime_error("resolve grid host failed");
        }
        grid_socket connection = invalid_grid_socket;
        for (auto* address = addresses; address != nullptr; address = address->ai_next) {
            connection = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
            if (connection != invalid_grid_socket &&
                connect(connection, address->ai_addr, static_cast<int>(address->ai_addrlen)) == 0) break;
            if (connection != invalid_grid_socket) close_grid_socket(connection);
            connection = invalid_grid_socket;
        }
        freeaddrinfo(addresses);
        if (connection == invalid_grid_socket) throw std::runtime_error("connect to grid failed");

        static std::atomic<unsigned long long> request_sequence{0};
        const auto request_id = "region-" + std::to_string(++request_sequence);
        const auto target = base_path_ + std::string(path);
        auto request = std::string(method) + " " + target + " HTTP/1.1\r\nHost: " + host_ +
                       "\r\nAuthorization: Bearer " + service_token_ +
                       "\r\nX-Request-ID: " + request_id +
                       "\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: " +
                       std::to_string(body.size()) + "\r\n\r\n" + std::string(body);
        std::size_t sent = 0;
        while (sent < request.size()) {
            const auto count = ::send(connection, request.data() + sent,
                                      static_cast<int>(request.size() - sent), 0);
            if (count <= 0) {
                close_grid_socket(connection);
                throw std::runtime_error("send grid request failed");
            }
            sent += static_cast<std::size_t>(count);
        }
        std::string response;
        char buffer[4096];
        for (;;) {
            const auto count = recv(connection, buffer, sizeof(buffer), 0);
            if (count <= 0) break;
            response.append(buffer, static_cast<std::size_t>(count));
        }
        close_grid_socket(connection);
        const auto first_space = response.find(' ');
        int status = 0;
        if (first_space == std::string::npos ||
            std::from_chars(response.data() + first_space + 1, response.data() + first_space + 4, status).ec != std::errc{}) {
            throw std::runtime_error("invalid grid response");
        }
        const auto body_start = response.find("\r\n\r\n");
        return {status, body_start == std::string::npos ? std::string{} : response.substr(body_start + 4)};
    }

private:
    std::string host_;
    std::string port_;
    std::string base_path_;
    std::string service_token_;
};

std::string json_field(std::string_view body, std::string_view name) {
    const auto marker = "\"" + std::string(name) + "\":\"";
    const auto start = body.find(marker);
    if (start == std::string_view::npos) return {};
    const auto value_start = start + marker.size();
    const auto end = body.find('"', value_start);
    return end == std::string_view::npos ? std::string{} : std::string(body.substr(value_start, end - value_start));
}

std::optional<std::uint32_t> json_u32(std::string_view body, std::string_view name) {
    const auto marker = "\"" + std::string(name) + "\":";
    const auto start = body.find(marker);
    if (start == std::string_view::npos) return std::nullopt;
    const auto value_start = start + marker.size();
    std::uint32_t value{};
    const auto result = std::from_chars(body.data() + value_start, body.data() + body.size(), value);
    if (result.ec != std::errc{} || result.ptr == body.data() + value_start) return std::nullopt;
    return value;
}

std::optional<std::uint64_t> json_u64(std::string_view body, std::string_view name) {
    const auto marker = "\"" + std::string(name) + "\":";
    const auto start = body.find(marker);
    if (start == std::string_view::npos) return std::nullopt;
    const auto value_start = start + marker.size();
    std::uint64_t value{};
    const auto result = std::from_chars(body.data() + value_start, body.data() + body.size(), value);
    if (result.ec != std::errc{} || result.ptr == body.data() + value_start) return std::nullopt;
    return value;
}

std::vector<AssetLocation> asset_locations_from_json(std::string_view body) {
    std::vector<AssetLocation> locations;
    constexpr std::string_view marker = "\"endpoint\":\"";
    std::size_t position = 0;
    while ((position = body.find(marker, position)) != std::string_view::npos) {
        const auto value_start = position + marker.size();
        const auto value_end = body.find('"', value_start);
        if (value_end == std::string_view::npos) break;
        const auto object_end = body.find('}', value_end);
        if (object_end == std::string_view::npos) break;
        const auto origin = body.find("\"origin\":true", value_end);
        locations.push_back(AssetLocation{
            std::string(body.substr(value_start, value_end - value_start)),
            origin != std::string_view::npos && origin < object_end});
        position = object_end + 1;
    }
    return locations;
}

std::optional<int> json_int(std::string_view body, std::string_view name) {
    const auto marker = "\"" + std::string(name) + "\":";
    const auto start = body.find(marker);
    if (start == std::string_view::npos) return std::nullopt;
    const auto value_start = start + marker.size();
    int value{};
    const auto result = std::from_chars(body.data() + value_start, body.data() + body.size(), value);
    if (result.ec != std::errc{} || result.ptr == body.data() + value_start) return std::nullopt;
    return value;
}

std::optional<InventoryItem> inventory_item_from_json(std::string_view body,
                                                       std::string_view user_id) {
    InventoryItem item;
    item.item_id = json_field(body, "id");
    item.creator_id = json_field(body, "creatorUserId");
    item.owner_id = json_field(body, "ownerUserId");
    item.folder_id = json_field(body, "folderId");
    item.asset_id = json_field(body, "assetId");
    item.name = json_field(body, "name");
    item.description = json_field(body, "description");
    const auto asset_type = json_int(body, "assetType");
    const auto inventory_type = json_int(body, "inventoryType");
    const auto flags = json_u32(body, "flags");
    const auto base_permissions = json_u32(body, "basePermissions");
    const auto current_permissions = json_u32(body, "currentPermissions");
    const auto everyone_permissions = json_u32(body, "everyonePermissions");
    const auto next_permissions = json_u32(body, "nextPermissions");
    const auto sale_type = json_int(body, "saleType");
    const auto sale_price = json_int(body, "salePrice");
    if (item.item_id.empty() || item.creator_id.empty() || item.owner_id != user_id ||
        item.folder_id.empty() || item.asset_id.empty() || item.name.empty() || !asset_type ||
        !inventory_type || !flags || !base_permissions || !current_permissions ||
        !everyone_permissions || !next_permissions || !sale_type || !sale_price)
        return std::nullopt;
    item.asset_type = *asset_type;
    item.inventory_type = *inventory_type;
    item.flags = *flags;
    item.base_permissions = *base_permissions;
    item.current_permissions = *current_permissions;
    item.everyone_permissions = *everyone_permissions;
    item.next_permissions = *next_permissions;
    item.sale_type = *sale_type;
    item.sale_price = *sale_price;
    return item;
}

} // namespace

std::shared_ptr<Transport> socket_transport(std::string grid_url, std::string service_token) {
    return std::make_shared<SocketTransport>(std::move(grid_url), std::move(service_token));
}

std::string Client::register_region(const RegionSettings& settings) {
    const auto body = "{\"name\":" + api::json_string(settings.name) +
                      ",\"gridX\":" + std::to_string(settings.grid_x) +
                      ",\"gridY\":" + std::to_string(settings.grid_y) +
                      ",\"publicEndpoint\":" + api::json_string(settings.public_endpoint) +
                      ",\"viewerPort\":" + std::to_string(settings.viewer_port) +
                      ",\"leaseSeconds\":" + std::to_string(settings.lease_seconds) + '}';
    const auto response = transport_->send("POST", "/api/v1/regions", body);
    if (response.status_code != 201) return {};
    return json_field(response.body, "id");
}

std::optional<RegisteredRegion> Client::register_provisioned_region(
    std::string_view region_id, const RegionSettings& settings) {
    const auto body = "{\"publicEndpoint\":" + api::json_string(settings.public_endpoint) +
                      ",\"viewerPort\":" + std::to_string(settings.viewer_port) +
                      ",\"leaseSeconds\":" + std::to_string(settings.lease_seconds) + '}';
    const auto response = transport_->send(
        "POST", "/api/v1/region-runtime/" + std::string(region_id), body);
    if (response.status_code != 200) return std::nullopt;
    RegisteredRegion region{json_field(response.body, "id"), json_field(response.body, "name")};
    const auto grid_x = json_int(response.body, "gridX");
    const auto grid_y = json_int(response.body, "gridY");
    if (region.id != region_id || region.name.empty() || !grid_x || !grid_y) return std::nullopt;
    region.grid_x = *grid_x;
    region.grid_y = *grid_y;
    return region;
}

bool Client::renew_lease(std::string_view region_id, int lease_seconds) {
    const auto body = "{\"leaseSeconds\":" + std::to_string(lease_seconds) + '}';
    return transport_->send("PUT", "/api/v1/regions/" + std::string(region_id) + "/lease", body).status_code == 200;
}

bool Client::deregister(std::string_view region_id) {
    return transport_->send("DELETE", "/api/v1/regions/" + std::string(region_id), {}).status_code == 204;
}

bool Client::renew_provisioned_lease(std::string_view region_id, int lease_seconds) {
    const auto body = "{\"leaseSeconds\":" + std::to_string(lease_seconds) + '}';
    return transport_->send("PUT", "/api/v1/region-runtime/" + std::string(region_id) + "/lease", body)
               .status_code == 200;
}

bool Client::deregister_provisioned(std::string_view region_id) {
    return transport_->send("DELETE", "/api/v1/region-runtime/" + std::string(region_id), {})
               .status_code == 204;
}

std::optional<ViewerSession> Client::validate_viewer_session(std::string_view session_id) {
    const auto response = transport_->send("GET", "/api/v1/sessions/" + std::string(session_id), {});
    if (response.status_code != 200) return std::nullopt;
    ViewerSession session;
    session.session_id = json_field(response.body, "id");
    session.secure_session_id = json_field(response.body, "secureSessionId");
    session.agent_id = json_field(response.body, "userId");
    session.destination_region_id = json_field(response.body, "destinationRegionId");
    const auto circuit = json_u32(response.body, "viewerCircuitCode");
    if (session.session_id != session_id || session.secure_session_id.empty() || session.agent_id.empty() ||
        session.destination_region_id.empty() ||
        !circuit || *circuit == 0)
        return std::nullopt;
    session.circuit_code = *circuit;
    return session;
}

std::optional<User> Client::find_user(std::string_view user_id) {
    const auto response = transport_->send("GET", "/api/v1/users/" + std::string(user_id), {});
    if (response.status_code != 200) return std::nullopt;
    User user{json_field(response.body, "id"), json_field(response.body, "username")};
    if (user.id != user_id || user.username.empty()) return std::nullopt;
    return user;
}

bool Client::revoke_viewer_session(std::string_view session_id) {
    const auto status = transport_->send("DELETE", "/api/v1/sessions/" + std::string(session_id), {}).status_code;
    return status == 204 || status == 404;
}

bool Client::create_inventory_folder(std::string_view user_id, std::string_view folder_id,
                                     std::string_view parent_id, std::string_view name,
                                     int type_default) {
    const auto body = "{\"id\":" + api::json_string(folder_id) +
                      ",\"parentId\":" + api::json_string(parent_id) +
                      ",\"name\":" + api::json_string(name) +
                      ",\"typeDefault\":" + std::to_string(type_default) + '}';
    return transport_->send("POST", "/api/v1/inventory/" + std::string(user_id) + "/folders", body)
               .status_code == 201;
}

bool Client::move_inventory_folder(std::string_view user_id, std::string_view folder_id,
                                   std::string_view parent_id) {
    const auto body = "{\"parentId\":" + api::json_string(parent_id) + '}';
    return transport_->send("PUT", "/api/v1/inventory/" + std::string(user_id) +
        "/folders/" + std::string(folder_id), body).status_code == 200;
}

bool Client::move_inventory_item(std::string_view user_id, std::string_view item_id,
                                 std::string_view folder_id, std::string_view new_name) {
    const auto body = "{\"folderId\":" + api::json_string(folder_id) +
                      ",\"name\":" + api::json_string(new_name) + '}';
    return transport_->send("PUT", "/api/v1/inventory/" + std::string(user_id) +
        "/items/" + std::string(item_id), body).status_code == 200;
}

bool Client::update_inventory_item_asset(std::string_view user_id, std::string_view item_id,
                                         std::string_view asset_id) {
    const auto body = "{\"assetId\":" + api::json_string(asset_id) + '}';
    return transport_->send("PUT", "/api/v1/inventory/" + std::string(user_id) +
        "/items/" + std::string(item_id) + "/asset", body).status_code == 200;
}

std::optional<InventoryItem> Client::find_inventory_item(std::string_view user_id,
                                                          std::string_view item_id) {
    const auto response = transport_->send(
        "GET", "/api/v1/inventory/" + std::string(user_id) +
                   "/items/" + std::string(item_id), {});
    if (response.status_code != 200) return std::nullopt;
    return inventory_item_from_json(response.body, user_id);
}

std::optional<std::string> Client::find_system_inventory_folder(std::string_view user_id,
                                                                 int folder_type) {
    const auto response = transport_->send(
        "GET", "/api/v1/inventory/" + std::string(user_id) +
                   "/system-folders/" + std::to_string(folder_type), {});
    if (response.status_code != 200) return std::nullopt;
    const auto folder_id = json_field(response.body, "id");
    if (folder_id.empty()) return std::nullopt;
    return folder_id;
}

bool Client::create_texture_inventory_item(std::string_view user_id, const TextureInventoryItem& item) {
    const auto body = "{\"id\":" + api::json_string(item.item_id) +
                      ",\"creatorUserId\":" + api::json_string(item.creator_id) +
                      ",\"folderId\":" + api::json_string(item.folder_id) +
                      ",\"assetId\":" + api::json_string(item.asset_id) +
                      ",\"assetType\":0,\"inventoryType\":0,\"name\":" + api::json_string(item.name) +
                      ",\"description\":" + api::json_string(item.description) +
                      ",\"basePermissions\":" + std::to_string(0x0009e000) +
                      ",\"currentPermissions\":" + std::to_string(0x0009e000) +
                      ",\"everyonePermissions\":" + std::to_string(item.everyone_permissions) +
                      ",\"nextPermissions\":" + std::to_string(item.next_permissions) + '}';
    return transport_->send("POST", "/api/v1/inventory/" + std::string(user_id) + "/items", body)
               .status_code == 201;
}

bool Client::create_object_inventory_item(std::string_view user_id, const ObjectInventoryItem& item) {
    const auto body = "{\"id\":" + api::json_string(item.item_id) +
                      ",\"creatorUserId\":" + api::json_string(item.creator_id) +
                      ",\"folderId\":" + api::json_string(item.folder_id) +
                      ",\"assetId\":" + api::json_string(item.asset_id) +
                      ",\"assetType\":6,\"inventoryType\":6,\"name\":" + api::json_string(item.name) +
                      ",\"description\":" + api::json_string(item.description) +
                      ",\"basePermissions\":" + std::to_string(item.base_permissions) +
                      ",\"currentPermissions\":" + std::to_string(item.current_permissions) +
                      ",\"everyonePermissions\":" + std::to_string(item.everyone_permissions) +
                      ",\"nextPermissions\":" + std::to_string(item.next_permissions) + '}';
    return transport_->send("POST", "/api/v1/inventory/" + std::string(user_id) + "/items", body)
               .status_code == 201;
}

bool Client::register_asset(std::string_view asset_id, std::string_view creator_id,
                            std::string_view sha256, std::uint64_t size,
                            std::string_view endpoint, bool origin) {
    const auto body = "{\"id\":" + api::json_string(asset_id) +
                      ",\"creatorUserId\":" + api::json_string(creator_id) +
                      ",\"sha256\":" + api::json_string(sha256) +
                      ",\"size\":" + std::to_string(size) +
                      ",\"endpoint\":" + api::json_string(endpoint) +
                      ",\"origin\":" + (origin ? "true" : "false") + '}';
    return transport_->send("POST", "/api/v1/assets", body).status_code == 201;
}

std::optional<FederatedAsset> Client::find_asset(std::string_view asset_id) {
    const auto response = transport_->send("GET", "/api/v1/assets/" + std::string(asset_id), {});
    if (response.status_code != 200) return std::nullopt;
    FederatedAsset asset;
    asset.asset_id = json_field(response.body, "id");
    asset.creator_id = json_field(response.body, "creatorUserId");
    asset.sha256 = json_field(response.body, "sha256");
    const auto size = json_u64(response.body, "size");
    asset.locations = asset_locations_from_json(response.body);
    if (asset.asset_id != asset_id || asset.creator_id.empty() || asset.sha256.size() != 64 ||
        !size || *size == 0 || asset.locations.empty()) return std::nullopt;
    asset.size = *size;
    return asset;
}

HttpResponse fetch_asset_from(std::string endpoint, std::string service_token,
                              std::string_view asset_id) {
    return socket_transport(std::move(endpoint), std::move(service_token))->send(
        "GET", "/api/v1/assets/" + std::string(asset_id), {});
}

std::optional<InventoryItem> Client::copy_library_item(std::string_view user_id,
                                                       std::string_view source_item_id,
                                                       std::string_view destination_folder_id,
                                                       std::string_view new_name) {
    const auto body = "{\"sourceItemId\":" + api::json_string(source_item_id) +
                      ",\"destinationFolderId\":" + api::json_string(destination_folder_id) +
                      ",\"name\":" + api::json_string(new_name) + '}';
    const auto response = transport_->send(
        "POST", "/api/v1/inventory/" + std::string(user_id) + "/copy-library-item", body);
    if (response.status_code != 201) return std::nullopt;
    return inventory_item_from_json(response.body, user_id);
}

std::optional<InventoryItem> Client::copy_inventory_item(std::string_view user_id,
                                                         std::string_view source_item_id,
                                                         std::string_view destination_folder_id,
                                                         std::string_view new_name) {
    const auto body = "{\"sourceItemId\":" + api::json_string(source_item_id) +
                      ",\"destinationFolderId\":" + api::json_string(destination_folder_id) +
                      ",\"name\":" + api::json_string(new_name) + '}';
    const auto response = transport_->send(
        "POST", "/api/v1/inventory/" + std::string(user_id) + "/copy-item", body);
    if (response.status_code != 201) return std::nullopt;
    return inventory_item_from_json(response.body, user_id);
}

std::optional<ViewerSession> ViewerSessionCache::validate(
    std::string_view session_id, std::chrono::steady_clock::time_point now) {
    const auto key = std::string(session_id);
    if (const auto found = entries_.find(key); found != entries_.end()) {
        if (now < found->second.expires_at) return found->second.session;
        entries_.erase(found);
    }
    auto session = client_.validate_viewer_session(session_id);
    if (session) entries_.insert_or_assign(key, Entry{*session, now + ttl_});
    return session;
}

void ViewerSessionCache::invalidate(std::string_view session_id) {
    entries_.erase(std::string(session_id));
}

bool Client::update_presence(std::string_view user_id, std::string_view region_id) {
    const auto body = "{\"regionId\":" + api::json_string(region_id) + '}';
    return transport_->send("PUT", "/api/v1/presence/" + std::string(user_id), body).status_code == 200;
}

bool Client::clear_presence(std::string_view user_id) {
    const auto status = transport_->send("DELETE", "/api/v1/presence/" + std::string(user_id), {}).status_code;
    return status == 204 || status == 404;
}

RegistrationLifecycle::RegistrationLifecycle(Client client, RegionSettings settings,
                                               std::string registered_region_id)
    : client_(std::move(client)), settings_(std::move(settings)),
      region_id_(std::move(registered_region_id)), already_registered_(!region_id_.empty()) {}

bool RegistrationLifecycle::start(std::chrono::steady_clock::time_point now) {
    if (!already_registered_) region_id_ = client_.register_region(settings_);
    if (region_id_.empty()) return false;
    renew_at_ = now + std::chrono::seconds(settings_.lease_seconds / 2);
    return true;
}

bool RegistrationLifecycle::tick(std::chrono::steady_clock::time_point now) {
    if (region_id_.empty() || now < renew_at_) return !region_id_.empty();
    const auto renewed = already_registered_ ?
        client_.renew_provisioned_lease(region_id_, settings_.lease_seconds) :
        client_.renew_lease(region_id_, settings_.lease_seconds);
    if (!renewed) return false;
    renew_at_ = now + std::chrono::seconds(settings_.lease_seconds / 2);
    return true;
}

void RegistrationLifecycle::stop() {
    if (!region_id_.empty()) {
        if (already_registered_) client_.deregister_provisioned(region_id_);
        else client_.deregister(region_id_);
    }
    region_id_.clear();
}

} // namespace homeworldz::grid
