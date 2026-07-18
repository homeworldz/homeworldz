#include "homeworldz/region_transit.h"

#include <chrono>
#include <vector>

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

    homeworldz::region::CapabilityArrivalGate arrival_gate;
    if (arrival_gate.mark_seed_served({}, transit.id) ||
        arrival_gate.mark_seed_served(transit.session_id, {}) ||
        arrival_gate.consume_seed(transit.session_id, transit.id)) return 1;
    if (!arrival_gate.mark_seed_served(transit.session_id, transit.id) ||
        arrival_gate.mark_seed_served(transit.session_id, transit.id) ||
        arrival_gate.size() != 1 ||
        !arrival_gate.consume_seed(transit.session_id, transit.id) ||
        arrival_gate.consume_seed(transit.session_id, transit.id) ||
        arrival_gate.size() != 0) return 1;
    if (!arrival_gate.mark_seed_served(transit.session_id, transit.id)) return 1;
    auto second_transit = transit;
    second_transit.id = "44444444-4444-4444-8444-444444444444";
    if (!arrival_gate.mark_seed_served(transit.session_id, second_transit.id) ||
        arrival_gate.size() != 2) return 1;
    arrival_gate.clear_session(transit.session_id);
    if (arrival_gate.size() != 0) return 1;

    const homeworldz::grid::RegionNeighbor beta{
        "east", "beta", "Beta", 1002, 1000, 512, 512, 13,
        "http://region.example:42021", 42022, true};
    const std::vector sandbox_neighbors{beta};
    const auto into_beta = homeworldz::region::plan_avatar_border_crossing(
        1001, 1000, 256, 256, {256.2, 200.0, 30.0}, sandbox_neighbors);
    if (!into_beta || into_beta->destination.id != "beta" ||
        into_beta->position != std::array<float, 3>{0.3F, 200.0F, 30.0F}) return 1;

    const homeworldz::grid::RegionNeighbor sandbox{
        "west", "sandbox", "Sandbox", 1001, 1000, 256, 256, 13,
        "http://region.example:42001", 42002, true};
    const std::vector beta_neighbors{sandbox};
    const auto into_sandbox = homeworldz::region::plan_avatar_border_crossing(
        1002, 1000, 512, 512, {-0.2, 200.0, 31.0}, beta_neighbors);
    if (!into_sandbox || into_sandbox->destination.id != "sandbox" ||
        into_sandbox->position != std::array<float, 3>{255.7F, 200.0F, 31.0F}) return 1;
    if (homeworldz::region::plan_avatar_border_crossing(
            1002, 1000, 512, 512, {-0.2, 400.0, 31.0}, beta_neighbors)) return 1;
    auto offline_sandbox = sandbox;
    offline_sandbox.online = false;
    const std::vector offline_neighbors{offline_sandbox};
    if (homeworldz::region::plan_avatar_border_crossing(
            1002, 1000, 512, 512, {-0.2, 200.0, 31.0}, offline_neighbors)) return 1;
    return 0;
}
