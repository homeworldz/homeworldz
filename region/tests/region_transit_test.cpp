#include "homeworldz/region_transit.h"

#include <chrono>

int main() {
    using namespace std::chrono_literals;
    const auto now = std::chrono::steady_clock::time_point{};
    constexpr std::string_view destination = "22222222-2222-4222-8222-222222222222";
    homeworldz::grid::AvatarTransit transit{
        "33333333-3333-4333-8333-333333333333", 1,
        "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa",
        "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb",
        "11111111-1111-4111-8111-111111111111", std::string(destination),
        {128.0F, 64.0F, 30.0F}, {1.0F, 0.0F, 0.0F}, true, "accepted"};
    homeworldz::region::InboundTransitRegistry registry;
    if (!registry.stage(transit, destination, now, 30s) || registry.size(now) != 1) return 1;
    if (!registry.stage(transit, destination, now + 1s, 30s) || registry.size(now + 1s) != 1) return 1;
    if (registry.authorize("wrong-agent", transit.session_id, now + 2s)) return 1;
    const auto* authorized = registry.authorize(transit.agent_id, transit.session_id, now + 2s);
    if (!authorized || authorized->position != transit.position || !authorized->flying) return 1;
    auto conflict = transit;
    conflict.id = "44444444-4444-4444-8444-444444444444";
    if (registry.stage(conflict, destination, now + 2s, 30s)) return 1;
    const auto consumed = registry.consume(transit.session_id, now + 3s);
    if (!consumed || consumed->id != transit.id || registry.size(now + 3s) != 0) return 1;

    transit.state = "prepared";
    if (registry.stage(transit, destination, now, 30s)) return 1;
    transit.state = "accepted";
    if (registry.stage(transit, "55555555-5555-4555-8555-555555555555", now, 30s)) return 1;
    if (!registry.stage(transit, destination, now, 10s)) return 1;
    if (registry.authorize(transit.agent_id, transit.session_id, now + 10s) ||
        registry.size(now + 10s) != 0) return 1;
    return 0;
}
