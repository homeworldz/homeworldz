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
        return {500, {}};
    }

    std::vector<Request> requests;
};

} // namespace

int main() {
    auto transport = std::make_shared<FakeTransport>();
    homeworldz::grid::Client client(transport);
    homeworldz::grid::RegionSettings settings{
        "Test Region", 1000, 1001, "http://localhost:42001", 60};
    homeworldz::grid::RegistrationLifecycle lifecycle(client, settings);
    const auto started = std::chrono::steady_clock::time_point{};
    if (!lifecycle.start(started) || lifecycle.region_id() != "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa") return 1;
    if (transport->requests.size() != 1 || transport->requests[0].method != "POST" ||
        transport->requests[0].body.find(R"("gridX":1000)") == std::string::npos) return 1;
    if (!lifecycle.tick(started + std::chrono::seconds(29)) || transport->requests.size() != 1) return 1;
    if (!lifecycle.tick(started + std::chrono::seconds(30)) || transport->requests.size() != 2 ||
        transport->requests[1].method != "PUT") return 1;
    lifecycle.stop();
    if (transport->requests.size() != 3 || transport->requests[2].method != "DELETE" ||
        !lifecycle.region_id().empty()) return 1;
    return 0;
}
