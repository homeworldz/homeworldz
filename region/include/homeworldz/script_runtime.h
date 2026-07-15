#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace homeworldz::script {

using Bytes = std::vector<std::byte>;

struct Identity {
    std::string asset_id;
    std::string inventory_item_id;
    std::string object_id;
    std::string owner_id;
};

struct Limits {
    std::uint64_t fuel_per_slice{1'000'000};
    std::uint64_t maximum_memory_bytes{0x00010000};
    std::size_t maximum_queued_events{0x0040};
};

enum class EventKind {
    state_entry,
    state_exit,
    touch_start,
    touch_end,
    timer,
    listen,
    changed,
    custom,
};

struct Event {
    EventKind kind{EventKind::custom};
    std::string name;
    Bytes payload;
};

enum class HostCommandKind {
    say,
    set_timer,
    set_object_name,
};

struct HostCommand {
    HostCommandKind kind{HostCommandKind::say};
    std::int32_t channel{};
    double number{};
    std::string text;
};

class Host {
public:
    virtual ~Host() = default;
    virtual bool emit(const Identity& script, const HostCommand& command) = 0;
};

enum class RunState {
    idle,
    yielded,
    completed_event,
    trapped,
};

struct RunResult {
    RunState state{RunState::idle};
    std::uint64_t fuel_consumed{};
    std::string diagnostic;
};

struct Snapshot {
    std::string engine_id;
    std::uint32_t format_version{};
    Bytes state;
};

struct CompileResult {
    Bytes module;
    std::vector<std::string> diagnostics;
    bool succeeded() const { return diagnostics.empty() && !module.empty(); }
};

class Compiler {
public:
    virtual ~Compiler() = default;
    virtual CompileResult compile_lsl(std::string_view source) = 0;
};

class Instance {
public:
    virtual ~Instance() = default;
    virtual bool enqueue(Event event) = 0;
    virtual RunResult run_slice(std::uint64_t fuel) = 0;
    virtual Snapshot snapshot() const = 0;
};

class Engine {
public:
    virtual ~Engine() = default;
    virtual std::string_view id() const = 0;
    virtual std::unique_ptr<Instance> instantiate(
        std::span<const std::byte> module, Identity identity, Limits limits,
        Host& host, const std::optional<Snapshot>& snapshot = std::nullopt) = 0;
};

} // namespace homeworldz::script
