#pragma once

// HomeWorldz script bytecode: the immutable, versioned instruction format the
// handwritten LSL compiler emits and the explicit-stack VM consumes. Per ADR
// 0021 the authoritative scene never depends on this representation directly;
// it is an opaque module behind the script-engine boundary. This proof-of-
// concept covers a representative slice (integers, strings, arithmetic, string
// concatenation, comparisons, a cast, control flow, and bounded host calls) so
// the compile -> run -> snapshot/restore pipeline can be confirmed end to end.

#include <cstdint>
#include <string>
#include <vector>

namespace homeworldz::script {

// Bumped when the emitted bytecode or snapshot layout changes. A destination
// region refuses to restore state produced by an incompatible ABI.
inline constexpr std::uint32_t kBytecodeAbiVersion = 1;
inline constexpr std::uint32_t kCompilerVersion = 1;

// Compile-time value types. Void exists only for typing host calls used as
// statements; a runtime Value is always Integer or String.
enum class Type : std::uint8_t { Void, Integer, String };

// A tagged LSL value. LSL integers are 32-bit; strings are UTF-8 bytes.
struct Value {
    Type type = Type::Integer;
    std::int32_t integer = 0;
    std::string str;

    static Value make_integer(std::int32_t v) { return Value{Type::Integer, v, {}}; }
    static Value make_string(std::string v) { return Value{Type::String, 0, std::move(v)}; }
};

enum class Op : std::uint8_t {
    PushConst,     // a = constant index
    LoadLocal,     // a = local slot
    StoreLocal,    // a = local slot (pops the value)
    AddInt,        // pops b, a; pushes a + b
    SubInt,        // pops b, a; pushes a - b
    MulInt,        // pops b, a; pushes a * b
    ConcatStr,     // pops b, a (strings); pushes a ++ b
    LessInt,       // pops b, a; pushes (a < b) ? 1 : 0
    GreaterInt,    // pops b, a; pushes (a > b) ? 1 : 0
    EqualInt,      // pops b, a; pushes (a == b) ? 1 : 0
    CastIntToStr,  // pops an integer; pushes its decimal string
    Jump,          // a = target instruction index
    JumpIfZero,    // a = target; pops an integer, jumps when it is zero
    CallHost,      // a = host-function index, b = argument count
    Pop,           // discards the top of the operand stack
    Halt,          // ends execution of the current event handler
};

struct Instruction {
    Op op = Op::Halt;
    std::int32_t a = 0;
    std::int32_t b = 0;
};

// A compiled event handler: the instruction stream plus the constant pool, the
// names of the host functions it calls (resolved at the boundary, not baked in),
// and how many local slots the VM must allocate. Cached by source hash plus
// compiler and ABI version; this PoC keeps one handler (state_entry).
struct Program {
    std::vector<Instruction> code;
    std::vector<Value> constants;
    std::vector<std::string> host_names;
    std::int32_t local_count = 0;
    std::uint32_t compiler_version = kCompilerVersion;
    std::uint32_t abi_version = kBytecodeAbiVersion;
};

} // namespace homeworldz::script
