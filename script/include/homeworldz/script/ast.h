#pragma once

// Abstract syntax tree for the LSL PoC subset, produced by the recursive-descent
// parser and lowered to bytecode by the compiler. Nodes are owned through
// unique_ptr; this is a small tree, so clarity is preferred over arena tricks.

#include <memory>
#include <string>
#include <vector>

#include "homeworldz/script/bytecode.h"

namespace homeworldz::script::ast {

struct Expr {
    virtual ~Expr() = default;
};
using ExprPtr = std::unique_ptr<Expr>;

struct IntLiteral : Expr {
    std::int32_t value = 0;
};

struct StringLiteral : Expr {
    std::string value;
};

struct VarRef : Expr {
    std::string name;
};

enum class BinaryOp { Add, Sub, Mul, Less, Greater, Equal };

struct Binary : Expr {
    BinaryOp op = BinaryOp::Add;
    ExprPtr lhs;
    ExprPtr rhs;
};

// A C-style cast such as (string)i. The PoC supports (string) and (integer).
struct Cast : Expr {
    Type target = Type::String;
    ExprPtr operand;
};

struct Call : Expr {
    std::string callee;
    std::vector<ExprPtr> args;
};

struct Stmt {
    virtual ~Stmt() = default;
};
using StmtPtr = std::unique_ptr<Stmt>;

struct Block : Stmt {
    std::vector<StmtPtr> statements;
};

struct VarDecl : Stmt {
    Type type = Type::Integer;
    std::string name;
    ExprPtr init; // may be null (default-initialized)
};

struct Assign : Stmt {
    std::string name;
    ExprPtr value;
};

struct ExprStmt : Stmt {
    ExprPtr expr;
};

struct While : Stmt {
    ExprPtr condition;
    std::unique_ptr<Block> body;
};

struct If : Stmt {
    ExprPtr condition;
    std::unique_ptr<Block> then_branch;
    std::unique_ptr<Block> else_branch; // may be null
};

// The single event handler this PoC compiles (default state's state_entry).
struct Script {
    std::unique_ptr<Block> state_entry;
};

} // namespace homeworldz::script::ast
