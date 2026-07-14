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
