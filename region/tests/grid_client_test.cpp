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
    if (!client.update_presence(session->agent_id, session->destination_region_id) ||
        transport->requests.back().method != "PUT" ||
        transport->requests.back().path != "/api/v1/presence/" + session->agent_id) return 1;
    if (!client.clear_presence(session->agent_id) || !client.revoke_viewer_session(session->session_id) ||
        transport->requests.back().path != "/api/v1/sessions/" + session->session_id) return 1;
    return 0;
}
