#pragma once

#include <chrono>
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

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

struct RegisteredRegion {
    std::string id;
    std::string name;
    int grid_x{};
    int grid_y{};
	std::string public_endpoint;
	int viewer_port{};
	std::string grid_name;
	std::string grid_public_url;
	int size_x{256};
	int size_y{256};
	int maturity{};
	std::string owner_id;
};

struct RegionNeighbor {
    std::string direction;
    std::string id;
    std::string name;
    int grid_x{};
    int grid_y{};
	int size_x{256};
	int size_y{256};
	int maturity{};
    std::string public_endpoint;
    int viewer_port{};
	bool online{};
    bool operator==(const RegionNeighbor&) const = default;
};

struct ViewerSession {
    std::string session_id;
    std::string secure_session_id;
    std::string agent_id;
    std::uint32_t circuit_code{};
    std::string destination_region_id;
};

struct AvatarTransitRequest {
    std::string id;
    std::string agent_id;
    std::string session_id;
    std::string source_region_id;
    std::string destination_region_id;
    std::array<float, 3> position{};
    std::array<float, 3> look_at{};
    bool flying{};
    int lifetime_seconds{30};
};

struct AvatarTransit {
    std::string id;
    std::uint64_t generation{};
    std::string agent_id;
    std::string session_id;
    std::string source_region_id;
    std::string destination_region_id;
    std::array<float, 3> position{};
    std::array<float, 3> look_at{};
    bool flying{};
    std::string state;
};

struct User {
    std::string id;
    std::string username;
};

struct AssetLocation {
    std::string endpoint;
    bool origin{};
};

struct HomeLocation {
    std::string region_id;
    std::array<float, 3> position{};
    std::array<float, 3> look_at{};
};

struct FederatedAsset {
    std::string asset_id;
    std::string creator_id;
    std::string sha256;
    std::uint64_t size{};
    std::vector<AssetLocation> locations;
};

struct TextureInventoryItem {
    std::string item_id;
    std::string creator_id;
    std::string folder_id;
    std::string asset_id;
    std::string name;
    std::string description;
    std::uint32_t everyone_permissions{};
    std::uint32_t next_permissions{};
};

struct ObjectInventoryItem {
    std::string item_id;
    std::string creator_id;
    std::string folder_id;
    std::string asset_id;
    std::string name;
    std::string description;
    std::uint32_t base_permissions{};
    std::uint32_t current_permissions{};
    std::uint32_t everyone_permissions{};
    std::uint32_t next_permissions{};
};

struct InventoryItem {
    std::string item_id;
    std::string creator_id;
    std::string owner_id;
    std::string folder_id;
    std::string asset_id;
    int asset_type{};
    int inventory_type{};
    std::string name;
    std::string description;
    std::uint32_t flags{};
    std::uint32_t base_permissions{};
    std::uint32_t current_permissions{};
    std::uint32_t everyone_permissions{};
    std::uint32_t next_permissions{};
    int sale_type{};
    int sale_price{};
};

struct TaskInventoryTransferRequest {
    std::string id;
    std::string user_id;
    std::string source_item_id;
    std::string region_id;
    std::string object_id;
    std::string task_item_id;
};

struct TaskInventoryTransfer {
    std::string id;
    std::string user_id;
    std::string source_item_id;
    std::string region_id;
    std::string object_id;
    std::string task_item_id;
    InventoryItem item;
    std::string state;
};

struct TaskInventoryExtractionRequest {
    std::string id;
    std::string user_id;
    std::string region_id;
    std::string object_id;
    std::string source_task_item_id;
    std::string destination_folder_id;
    std::string personal_item_id;
    InventoryItem item;
};

struct TaskInventoryExtraction {
    std::string id;
    std::string user_id;
    std::string region_id;
    std::string object_id;
    std::string source_task_item_id;
    std::string destination_folder_id;
    std::string personal_item_id;
    InventoryItem item;
    std::string state;
};

struct ObjectRezRequest {
    std::string id;
    std::string user_id;
    std::string source_item_id;
    std::string region_id;
    std::string object_id;
};

struct ObjectRez {
    std::string id;
    std::string user_id;
    std::string source_item_id;
    std::string region_id;
    std::string object_id;
    InventoryItem item;
    std::string state;
};

class Client {
public:
    explicit Client(std::shared_ptr<Transport> transport) : transport_(std::move(transport)) {}
    std::string register_region(const RegionSettings& settings);
	std::optional<RegisteredRegion> register_provisioned_region(
		std::string_view region_id, const RegionSettings& settings);
    std::optional<std::vector<RegionNeighbor>> find_region_neighbors(
        std::string_view region_id);
    bool renew_lease(std::string_view region_id, int lease_seconds);
    bool deregister(std::string_view region_id);
	bool renew_provisioned_lease(std::string_view region_id, int lease_seconds);
	bool deregister_provisioned(std::string_view region_id);
    std::optional<ViewerSession> validate_viewer_session(std::string_view session_id);
    std::optional<AvatarTransit> prepare_avatar_transit(const AvatarTransitRequest& request);
    std::optional<AvatarTransit> find_avatar_transit(std::string_view transit_id);
    std::optional<AvatarTransit> accept_avatar_transit(
        std::string_view transit_id, std::string_view destination_region_id);
    std::optional<AvatarTransit> activate_avatar_transit(
        std::string_view transit_id, std::string_view destination_region_id);
    std::optional<AvatarTransit> rollback_avatar_transit(
        std::string_view transit_id, std::string_view region_id, std::string_view reason);
    std::optional<User> find_user(std::string_view user_id);
    bool revoke_viewer_session(std::string_view session_id);
    bool create_inventory_folder(std::string_view user_id, std::string_view folder_id,
                                 std::string_view parent_id, std::string_view name, int type_default);
    bool move_inventory_folder(std::string_view user_id, std::string_view folder_id,
                               std::string_view parent_id);
    bool move_inventory_item(std::string_view user_id, std::string_view item_id,
                             std::string_view folder_id, std::string_view new_name);
    bool update_inventory_item_asset(std::string_view user_id, std::string_view item_id,
                                     std::string_view asset_id);
    std::optional<InventoryItem> find_inventory_item(std::string_view user_id,
                                                     std::string_view item_id);
    std::optional<std::string> find_system_inventory_folder(std::string_view user_id,
                                                            int folder_type);
    bool create_texture_inventory_item(std::string_view user_id, const TextureInventoryItem& item);
    bool create_object_inventory_item(std::string_view user_id, const ObjectInventoryItem& item);
    bool create_inventory_item(std::string_view user_id, const InventoryItem& item);
    std::optional<TaskInventoryTransfer> prepare_task_inventory_transfer(
        const TaskInventoryTransferRequest& request);
    std::optional<std::vector<TaskInventoryTransfer>> pending_task_inventory_transfers(
        std::string_view region_id);
    bool finalize_task_inventory_transfer(
        std::string_view transfer_id, std::string_view region_id);
    std::optional<TaskInventoryExtraction> prepare_task_inventory_extraction(
        const TaskInventoryExtractionRequest& request);
    std::optional<std::vector<TaskInventoryExtraction>> pending_task_inventory_extractions(
        std::string_view region_id);
    std::optional<TaskInventoryExtraction> finalize_task_inventory_extraction(
        std::string_view extraction_id, std::string_view region_id);
    std::optional<ObjectRez> prepare_object_rez(const ObjectRezRequest& request);
    std::optional<std::vector<ObjectRez>> pending_object_rezzes(std::string_view region_id);
    bool finalize_object_rez(std::string_view rez_id, std::string_view region_id);
    bool rollback_object_rez(std::string_view rez_id, std::string_view region_id);
    bool register_asset(std::string_view asset_id, std::string_view creator_id,
                        std::string_view sha256, std::uint64_t size,
                        std::string_view endpoint, bool origin);
    std::optional<FederatedAsset> find_asset(std::string_view asset_id);
    std::optional<InventoryItem> copy_library_item(std::string_view user_id,
                                                   std::string_view source_item_id,
                                                   std::string_view destination_folder_id,
                                                   std::string_view new_name);
    std::optional<InventoryItem> copy_inventory_item(std::string_view user_id,
                                                     std::string_view source_item_id,
                                                     std::string_view destination_folder_id,
                                                     std::string_view new_name);
    bool update_presence(std::string_view user_id, std::string_view region_id);
    bool clear_presence(std::string_view user_id);
    bool update_last_location(std::string_view user_id, std::string_view region_id,
                              const std::array<float, 3>& position,
                              const std::array<float, 3>& look_at, bool flying);
    bool set_home_location(std::string_view user_id, std::string_view region_id,
                           const std::array<float, 3>& position,
                           const std::array<float, 3>& look_at);
    std::optional<HomeLocation> home_location(std::string_view user_id);
    bool set_gesture_active(std::string_view user_id, std::string_view item_id,
                            std::string_view asset_id, bool active);

private:
    std::shared_ptr<Transport> transport_;
};

HttpResponse fetch_asset_from(std::string endpoint, std::string service_token,
                              std::string_view asset_id);
bool prepare_avatar_arrival(Transport& destination, std::string_view transit_id);

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
    RegistrationLifecycle(Client client, RegionSettings settings,
                          std::string registered_region_id = {});
    bool start(std::chrono::steady_clock::time_point now);
    bool tick(std::chrono::steady_clock::time_point now);
    void stop();
    const std::string& region_id() const { return region_id_; }

private:
    Client client_;
    RegionSettings settings_;
    std::string region_id_;
    std::chrono::steady_clock::time_point renew_at_{};
	bool already_registered_{};
};

} // namespace homeworldz::grid
