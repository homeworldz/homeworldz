#pragma once

// The explicit-stack bytecode VM. Per ADR 0021 / SCRIPTING.md, script state
// lives entirely in data (instruction pointer, operand stack, locals), never on
// the native C++ call stack, so execution can be suspended after any single
// instruction and resumed elsewhere. run() executes a bounded instruction slice
// and yields, which is how an infinite LSL loop is contained rather than
// hanging the region thread.

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "homeworldz/script/bytecode.h"

namespace homeworldz::script {

enum class RunStatus {
    Finished, // reached Halt
    Yielded,  // spent its instruction budget with work remaining
    Error,    // a runtime fault stopped the script
};

// The host-function boundary. Given a bounded name and evaluated arguments, it
// performs bounded region work and optionally returns a value. It must not
// touch the network, filesystem, clocks, or randomness directly.
using HostFunction =
    std::function<std::optional<Value>(const std::string& name, const std::vector<Value>& args)>;

class VM {
public:
    // The Program must outlive the VM (bytecode is a shared immutable asset).
    explicit VM(const Program& program);

    void set_host(HostFunction host) { host_ = std::move(host); }

    // Executes up to instruction_budget instructions, then returns.
    RunStatus run(std::uint64_t instruction_budget);

    bool finished() const { return finished_; }
    const std::string& error() const { return error_; }
    std::uint64_t total_instructions() const { return total_; }

    // Compact, versioned binary capture of the full runtime state (this PoC's
    // slice of the crossing snapshot in SCRIPTING.md). Bytecode is referenced by
    // ABI version, not embedded.
    std::vector<std::uint8_t> snapshot() const;

    // Restores state produced by snapshot() into this VM. Throws ScriptError if
    // the format, snapshot version, or bytecode ABI does not match.
    void restore(const std::vector<std::uint8_t>& bytes);

private:
    void step();

    const Program& program_;
    HostFunction host_;

    std::size_t ip_ = 0;
    std::vector<Value> stack_;
    std::vector<Value> locals_;
    bool finished_ = false;
    std::string error_;
    std::uint64_t total_ = 0;
};

} // namespace homeworldz::script
