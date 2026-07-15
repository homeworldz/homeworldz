#include "homeworldz/grid_client.h"

#include <chrono>
#include <memory>
#include <string>
#include <vector>

namespace {

struct Request {
    std::string method;
    std::string path;
    std::string body;
};

class FakeTransport final : public homeworldz::grid::Transport {
public:
    homeworldz::grid::HttpResponse send(std::string_view method, std::string_view path,
                                        std::string_view body) override {
        requests.push_back({std::string(method), std::string(path), std::string(body)});
        if (method == "POST" && path.ends_with("/copy-library-item"))
            return {201, R"({"id":"11111111-1111-4111-8111-111111111111","ownerUserId":"cccccccc-cccc-4ccc-8ccc-cccccccccccc","creatorUserId":"00000000-0000-0000-0000-000000000002","folderId":"22222222-2222-4222-8222-222222222222","assetId":"33333333-3333-4333-8333-333333333333","assetType":5,"inventoryType":18,"name":"Default Shirt","description":"","flags":4,"basePermissions":2147483647,"currentPermissions":2147483647,"everyonePermissions":2147483647,"nextPermissions":2147483647,"saleType":0,"salePrice":0})"};
        if (method == "POST") return {201, R"({"id":"aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa"})"};
        if (method == "PUT") return {200, R"({"id":"aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa"})"};
        if (method == "DELETE") return {204, {}};
        if (method == "GET" && path.starts_with("/api/v1/users/"))
            return {200, R"({"id":"cccccccc-cccc-4ccc-8ccc-cccccccccccc","username":"jim.tarber","createdAt":"2026-07-14T00:00:00Z"})"};
        if (method == "GET" && path.find("/system-folders/") != std::string_view::npos)
            return {200, R"({"id":"22222222-2222-4222-8222-222222222222","ownerUserId":"cccccccc-cccc-4ccc-8ccc-cccccccccccc","parentId":"eeeeeeee-eeee-4eee-8eee-eeeeeeeeeeee","name":"Objects","typeDefault":6,"version":1})"};
        if (method == "GET" && path.find("/inventory/") != std::string_view::npos &&
            path.find("/items/") != std::string_view::npos)
            return {200, R"({"id":"44444444-4444-4444-8444-444444444444","ownerUserId":"cccccccc-cccc-4ccc-8ccc-cccccccccccc","creatorUserId":"77777777-7777-4777-8777-777777777777","folderId":"55555555-5555-4555-8555-555555555555","assetId":"66666666-6666-4666-8666-666666666666","assetType":6,"inventoryType":6,"name":"Prim2","description":"","flags":0,"basePermissions":647168,"currentPermissions":647168,"everyonePermissions":0,"nextPermissions":581632,"saleType":0,"salePrice":0})"};
        if (method == "GET" && path.starts_with("/api/v1/assets/"))
            return {200, R"({"id":"66666666-6666-4666-8666-666666666666","creatorUserId":"77777777-7777-4777-8777-777777777777","sha256":"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef","size":332,"locations":[{"endpoint":"http://origin.example:42001","origin":true,"verifiedAt":"2026-07-14T00:00:00Z"},{"endpoint":"http://replica.example:42001","origin":false,"verifiedAt":"2026-07-14T00:00:00Z"}]})"};
        if (method == "GET") return {200, R"({"id":"bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb","userId":"cccccccc-cccc-4ccc-8ccc-cccccccccccc","expiresAt":"2026-07-14T00:00:00Z","viewerCircuitCode":123456,"destinationRegionId":"aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa"})"};
        return {500, {}};
    }

    std::vector<Request> requests;
};

} // namespace

int main() {
    auto transport = std::make_shared<FakeTransport>();
    homeworldz::grid::Client client(transport);
    homeworldz::grid::RegionSettings settings{
        "Test Region", 1000, 1001, "http://localhost:42001", 42003, 60};
    homeworldz::grid::RegistrationLifecycle lifecycle(client, settings);
    const auto started = std::chrono::steady_clock::time_point{};
    if (!lifecycle.start(started) || lifecycle.region_id() != "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa") return 1;
    if (transport->requests.size() != 1 || transport->requests[0].method != "POST" ||
        transport->requests[0].body.find(R"("gridX":1000)") == std::string::npos ||
        transport->requests[0].body.find(R"("viewerPort":42003)") == std::string::npos) return 1;
    if (!lifecycle.tick(started + std::chrono::seconds(29)) || transport->requests.size() != 1) return 1;
    if (!lifecycle.tick(started + std::chrono::seconds(30)) || transport->requests.size() != 2 ||
        transport->requests[1].method != "PUT") return 1;
    lifecycle.stop();
    if (transport->requests.size() != 3 || transport->requests[2].method != "DELETE" ||
        !lifecycle.region_id().empty()) return 1;
    const auto session = client.validate_viewer_session("bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb");
    if (!session || session->agent_id != "cccccccc-cccc-4ccc-8ccc-cccccccccccc" ||
        session->circuit_code != 123456 || session->destination_region_id != "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa" ||
        transport->requests.back().method != "GET") return 1;
    const auto user = client.find_user(session->agent_id);
    if (!user || user->id != session->agent_id || user->username != "jim.tarber" ||
        transport->requests.back().path != "/api/v1/users/" + session->agent_id) return 1;
    homeworldz::grid::ViewerSessionCache cache(client, std::chrono::seconds(5));
    const auto requests_before_cache = transport->requests.size();
    const auto cached = cache.validate(session->session_id, started);
    if (!cached || transport->requests.size() != requests_before_cache + 1) return 1;
    if (!cache.validate(session->session_id, started + std::chrono::seconds(4)) ||
        transport->requests.size() != requests_before_cache + 1) return 1;
    if (!cache.validate(session->session_id, started + std::chrono::seconds(5)) ||
        transport->requests.size() != requests_before_cache + 2) return 1;
    cache.invalidate(session->session_id);
    if (!cache.validate(session->session_id, started + std::chrono::seconds(6)) ||
        transport->requests.size() != requests_before_cache + 3) return 1;
    if (!client.create_inventory_folder(session->agent_id,
            "dddddddd-dddd-4ddd-8ddd-dddddddddddd",
            "eeeeeeee-eeee-4eee-8eee-eeeeeeeeeeee", "Projects", -1) ||
        transport->requests.back().path != "/api/v1/inventory/" + session->agent_id + "/folders" ||
        transport->requests.back().body.find(R"("name":"Projects")") == std::string::npos ||
        transport->requests.back().body.find(R"("typeDefault":-1)") == std::string::npos) return 1;
    if (!client.move_inventory_folder(session->agent_id,
            "dddddddd-dddd-4ddd-8ddd-dddddddddddd",
            "ffffffff-ffff-4fff-8fff-ffffffffffff") ||
        transport->requests.back().method != "PUT" ||
        transport->requests.back().path != "/api/v1/inventory/" + session->agent_id +
            "/folders/dddddddd-dddd-4ddd-8ddd-dddddddddddd" ||
        transport->requests.back().body.find(
            R"("parentId":"ffffffff-ffff-4fff-8fff-ffffffffffff")") == std::string::npos)
        return 1;
    if (!client.move_inventory_item(session->agent_id,
            "11111111-1111-4111-8111-111111111111",
            "ffffffff-ffff-4fff-8fff-ffffffffffff", "Renamed Texture") ||
        transport->requests.back().method != "PUT" ||
        transport->requests.back().path != "/api/v1/inventory/" + session->agent_id +
            "/items/11111111-1111-4111-8111-111111111111" ||
        transport->requests.back().body.find(
            R"("folderId":"ffffffff-ffff-4fff-8fff-ffffffffffff")") == std::string::npos ||
        transport->requests.back().body.find(R"("name":"Renamed Texture")") == std::string::npos)
        return 1;
    const auto objects_folder = client.find_system_inventory_folder(session->agent_id, 6);
    if (!objects_folder || *objects_folder != "22222222-2222-4222-8222-222222222222" ||
        transport->requests.back().method != "GET" ||
        transport->requests.back().path != "/api/v1/inventory/" + session->agent_id + "/system-folders/6")
        return 1;
    const auto found_object = client.find_inventory_item(
        session->agent_id, "44444444-4444-4444-8444-444444444444");
    if (!found_object || found_object->name != "Prim2" || found_object->asset_type != 6 ||
        found_object->asset_id != "66666666-6666-4666-8666-666666666666" ||
        transport->requests.back().path != "/api/v1/inventory/" + session->agent_id +
            "/items/44444444-4444-4444-8444-444444444444")
        return 1;
    const homeworldz::grid::TextureInventoryItem texture{
        "11111111-1111-4111-8111-111111111111", session->agent_id,
        "22222222-2222-4222-8222-222222222222", "33333333-3333-4333-8333-333333333333",
        "Terrain & Sky", "Uploaded <texture>", 0, 0x7fffffff};
    if (!client.create_texture_inventory_item(session->agent_id, texture) ||
        transport->requests.back().path != "/api/v1/inventory/" + session->agent_id + "/items" ||
        transport->requests.back().body.find(R"("assetType":0,"inventoryType":0)") == std::string::npos ||
        transport->requests.back().body.find(R"("creatorUserId":"cccccccc-cccc-4ccc-8ccc-cccccccccccc")") == std::string::npos ||
        transport->requests.back().body.find(R"("name":"Terrain & Sky")") == std::string::npos ||
        transport->requests.back().body.find(R"("nextPermissions":2147483647)") == std::string::npos) return 1;
    const homeworldz::grid::ObjectInventoryItem object{
        "44444444-4444-4444-8444-444444444444", "77777777-7777-4777-8777-777777777777",
        "55555555-5555-4555-8555-555555555555", "66666666-6666-4666-8666-666666666666",
        "Primitive", "", 0x0009e000, 0x0009e000, 0, 0x0008e000};
    if (!client.create_object_inventory_item(session->agent_id, object) ||
        transport->requests.back().body.find(R"("assetType":6,"inventoryType":6)") == std::string::npos ||
        transport->requests.back().body.find(
            R"("creatorUserId":"77777777-7777-4777-8777-777777777777")") == std::string::npos ||
        transport->requests.back().body.find(R"("basePermissions":647168)") == std::string::npos ||
        transport->requests.back().body.find(R"("name":"Primitive")") == std::string::npos) return 1;
    if (!client.register_asset(
            "66666666-6666-4666-8666-666666666666",
            "77777777-7777-4777-8777-777777777777",
            "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
            332, "http://region.example:42001", true) ||
        transport->requests.back().path != "/api/v1/assets" ||
        transport->requests.back().body.find(R"("size":332)") == std::string::npos ||
        transport->requests.back().body.find(R"("origin":true)") == std::string::npos)
        return 1;
    const auto asset = client.find_asset("66666666-6666-4666-8666-666666666666");
    if (!asset || asset->creator_id != "77777777-7777-4777-8777-777777777777" ||
        asset->size != 332 || asset->locations.size() != 2 || !asset->locations[0].origin ||
        asset->locations[1].origin || asset->locations[1].endpoint != "http://replica.example:42001" ||
        transport->requests.back().path != "/api/v1/assets/66666666-6666-4666-8666-666666666666")
        return 1;
    const auto copied = client.copy_library_item(
        session->agent_id, "d5e46210-b9d1-11dc-95ff-0800200c9a66",
        "00000000-0000-0000-0000-000000000000", "");
    if (!copied || copied->owner_id != session->agent_id || copied->creator_id != "00000000-0000-0000-0000-000000000002" ||
        copied->asset_type != 5 || copied->inventory_type != 18 || copied->flags != 4 ||
        copied->base_permissions != 2147483647 || copied->name != "Default Shirt" ||
        transport->requests.back().path != "/api/v1/inventory/" + session->agent_id + "/copy-library-item" ||
        transport->requests.back().body.find(R"("sourceItemId":"d5e46210-b9d1-11dc-95ff-0800200c9a66")") == std::string::npos)
        return 1;
    if (!client.update_presence(session->agent_id, session->destination_region_id) ||
        transport->requests.back().method != "PUT" ||
        transport->requests.back().path != "/api/v1/presence/" + session->agent_id) return 1;
    if (!client.clear_presence(session->agent_id) || !client.revoke_viewer_session(session->session_id) ||
        transport->requests.back().path != "/api/v1/sessions/" + session->session_id) return 1;
    return 0;
}
