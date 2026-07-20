#pragma once

// Lowers the AST to HomeWorldz bytecode with light semantic analysis: local
// slot allocation, static typing to select integer vs. string operators and
// casts, and host-call arity/type checking against a small built-in table.

#include "homeworldz/script/ast.h"
#include "homeworldz/script/bytecode.h"

namespace homeworldz::script {

// Compiles a parsed script's state_entry handler into an immutable Program.
// Throws ScriptError on a semantic error (unknown variable, type mismatch,
// bad host-call arity, etc.).
Program compile(const ast::Script& script);

// Convenience: source -> tokens -> AST -> bytecode.
Program compile_source(const std::string& source);

} // namespace homeworldz::script
