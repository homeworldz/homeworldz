#include "homeworldz/script_runtime.h"

#include <array>
#include <memory>
#include <utility>
#include <vector>

namespace {

class TestHost final : public homeworldz::script::Host {
public:
    bool emit(const homeworldz::script::Identity&, const homeworldz::script::HostCommand&) override {
        ++commands;
        return true;
    }
    std::size_t commands{};
};

class TestInstance final : public homeworldz::script::Instance {
public:
    TestInstance(homeworldz::script::Identity identity, homeworldz::script::Limits limits,
                 homeworldz::script::Host& host)
        : identity_(std::move(identity)), limits_(limits), host_(host) {}

    bool enqueue(homeworldz::script::Event event) override {
        if (events_.size() >= limits_.maximum_queued_events) return false;
        events_.push_back(std::move(event));
        return true;
    }

    homeworldz::script::RunResult run_slice(std::uint64_t fuel) override {
        if (events_.empty()) return {};
        if (fuel < 10) return {homeworldz::script::RunState::yielded, fuel, {}};
        host_.emit(identity_, {homeworldz::script::HostCommandKind::say, 0, 0.0, "event"});
        events_.erase(events_.begin());
        return {homeworldz::script::RunState::completed_event, 10, {}};
    }

    homeworldz::script::Snapshot snapshot() const override {
        return {"test", 1, {std::byte{static_cast<unsigned char>(events_.size())}}};
    }

private:
    homeworldz::script::Identity identity_;
    homeworldz::script::Limits limits_;
    homeworldz::script::Host& host_;
    std::vector<homeworldz::script::Event> events_;
};

class TestEngine final : public homeworldz::script::Engine {
public:
    std::string_view id() const override { return "test"; }
    std::unique_ptr<homeworldz::script::Instance> instantiate(
        std::span<const std::byte> module, homeworldz::script::Identity identity,
        homeworldz::script::Limits limits, homeworldz::script::Host& host,
        const std::optional<homeworldz::script::Snapshot>&) override {
        if (module.empty()) return {};
        return std::make_unique<TestInstance>(std::move(identity), limits, host);
    }
};

} // namespace

int main() {
    TestHost host;
    TestEngine engine;
    homeworldz::script::Limits limits;
    limits.maximum_queued_events = 1;
    const std::array module{std::byte{0x00}, std::byte{0x61}, std::byte{0x73}, std::byte{0x6d}};
    auto instance = engine.instantiate(
        module, {"asset", "item", "object", "owner"}, limits, host, std::nullopt);
    if (!instance || engine.id() != "test" ||
        !instance->enqueue({homeworldz::script::EventKind::state_entry, {}, {}}) ||
        instance->enqueue({homeworldz::script::EventKind::timer, {}, {}})) return 1;
    if (instance->run_slice(5).state != homeworldz::script::RunState::yielded ||
        instance->run_slice(10).state != homeworldz::script::RunState::completed_event ||
        host.commands != 1 || instance->snapshot().state.size() != 1) return 1;
    return 0;
}
