// Proof-of-concept confirmation for the HomeWorldz LSL scripting approach
// (ADR 0021 / SCRIPTING.md). It exercises the whole pipeline on a representative
// script and asserts the properties that make the approach viable:
//
//   1. Source -> tokens -> AST -> versioned bytecode (handwritten front end).
//   2. Explicit-stack execution with a host-call boundary and no OS access.
//   3. Cooperative scheduling: an infinite loop consumes its instruction budget
//      and yields instead of hanging the region thread.
//   4. The differentiator: the VM can be snapshotted after ANY single
//      instruction and restored into a fresh VM to finish identically -- the
//      property that lets a script cross regions mid-handler.
//   5. Semantic diagnostics reject ill-typed programs at compile time.

#include "homeworldz/script/compiler.h"
#include "homeworldz/script/lexer.h"
#include "homeworldz/script/vm.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

using homeworldz::script::compile_source;
using homeworldz::script::HostFunction;
using homeworldz::script::Program;
using homeworldz::script::RunStatus;
using homeworldz::script::ScriptError;
using homeworldz::script::Value;
using homeworldz::script::VM;

namespace {

int g_failures = 0;

void require(bool condition, const std::string& what) {
    if (condition) {
        std::cout << "  [ok] " << what << "\n";
    } else {
        std::cout << "  [FAIL] " << what << "\n";
        ++g_failures;
    }
}

// A deterministic host boundary that records what scripts "say". No network,
// filesystem, clock, or randomness -- exactly the boundary ADR 0021 requires.
HostFunction make_host(std::vector<std::string>& out) {
    return [&out](const std::string& name,
                  const std::vector<Value>& args) -> std::optional<Value> {
        if (name == "llOwnerSay" && args.size() == 1) {
            out.push_back(args[0].str);
        } else if (name == "llSay" && args.size() == 2) {
            out.push_back(args[1].str);
        }
        return std::nullopt; // both are void
    };
}

const char* kDemoSource = R"LSL(
default
{
    state_entry()
    {
        integer i = 0;
        while (i < 5)
        {
            llOwnerSay("tick " + (string)i);
            i = i + 1;
        }
        llOwnerSay("done");
    }
}
)LSL";

} // namespace

int main() {
    std::cout << "HomeWorldz LSL scripting PoC\n";

    // (1) Compile the representative script.
    const Program program = compile_source(kDemoSource);
    std::cout << "compiled: " << program.code.size() << " instructions, "
              << program.constants.size() << " constants, host funcs:";
    for (const auto& h : program.host_names) {
        std::cout << ' ' << h;
    }
    std::cout << " (bytecode ABI v" << program.abi_version << ")\n";

    const std::vector<std::string> expected = {
        "tick 0", "tick 1", "tick 2", "tick 3", "tick 4", "done"};

    // (2) Full run establishes the reference output and instruction count.
    std::vector<std::string> reference;
    VM vm(program);
    vm.set_host(make_host(reference));
    const RunStatus status = vm.run(1'000'000);
    require(status == RunStatus::Finished, "script runs to completion");
    require(reference == expected, "produces the expected output");
    const std::uint64_t total = vm.total_instructions();
    std::cout << "  (executed " << total << " instructions)\n";

    // (3) Cooperative scheduling: run in tiny slices; still finishes identically.
    {
        std::vector<std::string> out;
        VM sliced(program);
        sliced.set_host(make_host(out));
        int slices = 0;
        RunStatus s = RunStatus::Yielded;
        while (s != RunStatus::Finished) {
            s = sliced.run(4); // 4-instruction budget per region tick
            ++slices;
            if (slices > 100000) {
                break; // safety net for the test itself
            }
        }
        require(s == RunStatus::Finished && out == expected,
                "same result across many 4-instruction slices");
        require(slices > 1, "execution actually spanned multiple ticks");
    }

    // (3b) An infinite loop is contained, not fatal.
    {
        const Program spin = compile_source(
            "default{state_entry(){ integer i=0; while(1){ i=i+1; } }}");
        std::vector<std::string> out;
        VM vm_spin(spin);
        vm_spin.set_host(make_host(out));
        const RunStatus a = vm_spin.run(1000);
        const RunStatus b = vm_spin.run(1000);
        require(a == RunStatus::Yielded && b == RunStatus::Yielded,
                "infinite loop yields instead of hanging");
        require(vm_spin.total_instructions() == 2000,
                "each slice runs exactly its instruction budget");
    }

    // (4) Snapshot after EVERY instruction, restore into a fresh VM, finish.
    {
        bool all_identical = true;
        for (std::uint64_t k = 1; k <= total; ++k) {
            std::vector<std::string> combined;
            VM first(program);
            first.set_host(make_host(combined));
            first.run(k); // stop after exactly k instructions (an arbitrary boundary)

            const std::vector<std::uint8_t> snap = first.snapshot();

            VM second(program);
            second.set_host(make_host(combined));
            second.restore(snap);
            second.run(1'000'000);

            if (!(second.finished() && combined == expected)) {
                all_identical = false;
                std::cout << "  [FAIL] mismatch when crossing after instruction " << k << "\n";
                break;
            }
        }
        require(all_identical,
                "snapshot/restore after any instruction reproduces the run exactly");
    }

    // (5) Semantic diagnostics.
    {
        bool threw = false;
        try {
            compile_source("default{state_entry(){ integer i = \"nope\"; }}");
        } catch (const ScriptError&) {
            threw = true;
        }
        require(threw, "ill-typed initializer is rejected at compile time");
    }

    if (g_failures == 0) {
        std::cout << "ALL PoC CHECKS PASSED\n";
        return EXIT_SUCCESS;
    }
    std::cout << g_failures << " CHECK(S) FAILED\n";
    return EXIT_FAILURE;
}
