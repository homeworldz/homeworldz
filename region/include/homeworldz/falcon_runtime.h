#pragma once

#include "homeworldz/script_runtime.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace homeworldz::script {

struct FalconHostMessage {
    Identity identity;
    bool owner_only{};
    std::int32_t channel{};
    std::string text;
};

struct FalconRezResult {
    bool compiled{};
    bool running{};
    std::string diagnostic;
};

struct FalconTickResult {
    std::size_t scripts_visited{};
    std::uint64_t instructions{};
    std::size_t trapped{};
};

class FalconRuntime {
public:
    using HostSink = std::function<void(FalconHostMessage)>;

    explicit FalconRuntime(HostSink host_sink = {});
    ~FalconRuntime();
    FalconRuntime(const FalconRuntime&) = delete;
    FalconRuntime& operator=(const FalconRuntime&) = delete;

    FalconRezResult rez(Identity identity, std::string_view source, bool enabled);
    bool set_enabled(std::string_view object_id, std::string_view inventory_item_id,
                     bool enabled);
    bool erase(std::string_view object_id, std::string_view inventory_item_id);
    FalconTickResult run_tick(std::uint64_t total_instruction_budget = 0x00010000,
                              std::uint64_t per_script_slice = 0x00000400);
    std::size_t size() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace homeworldz::script
