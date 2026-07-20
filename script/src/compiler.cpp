#include "homeworldz/script/compiler.h"

#include "homeworldz/script/lexer.h"
#include "homeworldz/script/parser.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace homeworldz::script {
namespace {

const char* type_name(Type type) {
    switch (type) {
    case Type::Integer: return "integer";
    case Type::String: return "string";
    case Type::Void: return "void";
    }
    return "?";
}

// A bounded host-function signature. Real HomeWorldz will have a generated
// table; the PoC hand-lists the two functions the demo needs.
struct HostSignature {
    std::vector<Type> params;
    Type result;
};

const HostSignature* lookup_host(const std::string& name) {
    static const std::unordered_map<std::string, HostSignature> table = {
        {"llOwnerSay", HostSignature{{Type::String}, Type::Void}},
        {"llSay", HostSignature{{Type::Integer, Type::String}, Type::Void}},
    };
    const auto it = table.find(name);
    return it == table.end() ? nullptr : &it->second;
}

class Compiler {
public:
    Program compile(const ast::Script& script) {
        if (!script.state_entry) {
            throw ScriptError("script has no state_entry handler");
        }
        compile_block(*script.state_entry);
        emit(Op::Halt);
        return std::move(program_);
    }

private:
    struct Local {
        std::int32_t slot;
        Type type;
    };

    std::int32_t emit(Op op, std::int32_t a = 0, std::int32_t b = 0) {
        program_.code.push_back(Instruction{op, a, b});
        return static_cast<std::int32_t>(program_.code.size()) - 1;
    }

    void patch_target(std::int32_t at, std::int32_t target) {
        program_.code[static_cast<std::size_t>(at)].a = target;
    }

    std::int32_t here() const { return static_cast<std::int32_t>(program_.code.size()); }

    std::int32_t add_constant(Value value) {
        for (std::size_t i = 0; i < program_.constants.size(); ++i) {
            const Value& existing = program_.constants[i];
            if (existing.type == value.type && existing.integer == value.integer &&
                existing.str == value.str) {
                return static_cast<std::int32_t>(i);
            }
        }
        program_.constants.push_back(std::move(value));
        return static_cast<std::int32_t>(program_.constants.size()) - 1;
    }

    std::int32_t add_host(const std::string& name) {
        for (std::size_t i = 0; i < program_.host_names.size(); ++i) {
            if (program_.host_names[i] == name) {
                return static_cast<std::int32_t>(i);
            }
        }
        program_.host_names.push_back(name);
        return static_cast<std::int32_t>(program_.host_names.size()) - 1;
    }

    const Local& lookup(const std::string& name) {
        const auto it = locals_.find(name);
        if (it == locals_.end()) {
            throw ScriptError("undeclared variable '" + name + "'");
        }
        return it->second;
    }

    void compile_block(const ast::Block& block) {
        for (const auto& stmt : block.statements) {
            compile_statement(*stmt);
        }
    }

    void compile_statement(const ast::Stmt& stmt) {
        if (const auto* block = dynamic_cast<const ast::Block*>(&stmt)) {
            compile_block(*block);
        } else if (const auto* decl = dynamic_cast<const ast::VarDecl*>(&stmt)) {
            compile_var_decl(*decl);
        } else if (const auto* assign = dynamic_cast<const ast::Assign*>(&stmt)) {
            compile_assign(*assign);
        } else if (const auto* expr = dynamic_cast<const ast::ExprStmt*>(&stmt)) {
            const Type result = compile_expression(*expr->expr);
            if (result != Type::Void) {
                emit(Op::Pop); // discard an unused value
            }
        } else if (const auto* loop = dynamic_cast<const ast::While*>(&stmt)) {
            compile_while(*loop);
        } else if (const auto* branch = dynamic_cast<const ast::If*>(&stmt)) {
            compile_if(*branch);
        } else {
            throw ScriptError("internal: unknown statement");
        }
    }

    void compile_var_decl(const ast::VarDecl& decl) {
        if (locals_.count(decl.name) != 0) {
            throw ScriptError("variable '" + decl.name + "' already declared");
        }
        const std::int32_t slot = program_.local_count++;
        if (decl.init) {
            const Type init_type = compile_expression(*decl.init);
            if (init_type != decl.type) {
                throw ScriptError("cannot initialize " + std::string(type_name(decl.type)) +
                                  " '" + decl.name + "' with " + type_name(init_type));
            }
        } else {
            emit(Op::PushConst, default_constant(decl.type));
        }
        emit(Op::StoreLocal, slot);
        locals_.emplace(decl.name, Local{slot, decl.type});
    }

    void compile_assign(const ast::Assign& assign) {
        const Local local = lookup(assign.name);
        const Type value_type = compile_expression(*assign.value);
        if (value_type != local.type) {
            throw ScriptError("cannot assign " + std::string(type_name(value_type)) +
                              " to " + type_name(local.type) + " '" + assign.name + "'");
        }
        emit(Op::StoreLocal, local.slot);
    }

    void compile_while(const ast::While& loop) {
        const std::int32_t start = here();
        require_integer(compile_expression(*loop.condition), "while condition");
        const std::int32_t exit_jump = emit(Op::JumpIfZero, 0);
        compile_block(*loop.body);
        emit(Op::Jump, start);
        patch_target(exit_jump, here());
    }

    void compile_if(const ast::If& branch) {
        require_integer(compile_expression(*branch.condition), "if condition");
        const std::int32_t else_jump = emit(Op::JumpIfZero, 0);
        compile_block(*branch.then_branch);
        if (branch.else_branch) {
            const std::int32_t end_jump = emit(Op::Jump, 0);
            patch_target(else_jump, here());
            compile_block(*branch.else_branch);
            patch_target(end_jump, here());
        } else {
            patch_target(else_jump, here());
        }
    }

    // Emits code that pushes exactly one value (unless the result is Void, for a
    // void host call used as a statement) and returns the value's static type.
    Type compile_expression(const ast::Expr& expr) {
        if (const auto* lit = dynamic_cast<const ast::IntLiteral*>(&expr)) {
            emit(Op::PushConst, add_constant(Value::make_integer(lit->value)));
            return Type::Integer;
        }
        if (const auto* lit = dynamic_cast<const ast::StringLiteral*>(&expr)) {
            emit(Op::PushConst, add_constant(Value::make_string(lit->value)));
            return Type::String;
        }
        if (const auto* ref = dynamic_cast<const ast::VarRef*>(&expr)) {
            const Local local = lookup(ref->name);
            emit(Op::LoadLocal, local.slot);
            return local.type;
        }
        if (const auto* cast = dynamic_cast<const ast::Cast*>(&expr)) {
            return compile_cast(*cast);
        }
        if (const auto* binary = dynamic_cast<const ast::Binary*>(&expr)) {
            return compile_binary(*binary);
        }
        if (const auto* call = dynamic_cast<const ast::Call*>(&expr)) {
            return compile_call(*call);
        }
        throw ScriptError("internal: unknown expression");
    }

    Type compile_cast(const ast::Cast& cast) {
        const Type operand = compile_expression(*cast.operand);
        if (cast.target == Type::String) {
            if (operand == Type::Integer) {
                emit(Op::CastIntToStr);
            } else if (operand != Type::String) {
                throw ScriptError("cannot cast void to string");
            }
            return Type::String;
        }
        // (integer) cast: only the identity case is supported in the PoC.
        if (operand != Type::Integer) {
            throw ScriptError("(integer) cast from string is not supported in this PoC");
        }
        return Type::Integer;
    }

    Type compile_binary(const ast::Binary& binary) {
        const Type lhs = compile_expression(*binary.lhs);
        const Type rhs = compile_expression(*binary.rhs);
        switch (binary.op) {
        case ast::BinaryOp::Add:
            if (lhs == Type::Integer && rhs == Type::Integer) {
                emit(Op::AddInt);
                return Type::Integer;
            }
            if (lhs == Type::String && rhs == Type::String) {
                emit(Op::ConcatStr);
                return Type::String;
            }
            throw ScriptError("operator '+' requires two integers or two strings");
        case ast::BinaryOp::Sub:
            require_two_integers(lhs, rhs, "-");
            emit(Op::SubInt);
            return Type::Integer;
        case ast::BinaryOp::Mul:
            require_two_integers(lhs, rhs, "*");
            emit(Op::MulInt);
            return Type::Integer;
        case ast::BinaryOp::Less:
            require_two_integers(lhs, rhs, "<");
            emit(Op::LessInt);
            return Type::Integer;
        case ast::BinaryOp::Greater:
            require_two_integers(lhs, rhs, ">");
            emit(Op::GreaterInt);
            return Type::Integer;
        case ast::BinaryOp::Equal:
            require_two_integers(lhs, rhs, "==");
            emit(Op::EqualInt);
            return Type::Integer;
        }
        throw ScriptError("internal: unknown binary operator");
    }

    Type compile_call(const ast::Call& call) {
        const HostSignature* signature = lookup_host(call.callee);
        if (signature == nullptr) {
            throw ScriptError("unknown function '" + call.callee + "'");
        }
        if (call.args.size() != signature->params.size()) {
            throw ScriptError("function '" + call.callee + "' expects " +
                              std::to_string(signature->params.size()) + " argument(s)");
        }
        for (std::size_t i = 0; i < call.args.size(); ++i) {
            const Type arg_type = compile_expression(*call.args[i]);
            if (arg_type != signature->params[i]) {
                throw ScriptError("argument " + std::to_string(i + 1) + " to '" +
                                  call.callee + "' must be " +
                                  type_name(signature->params[i]));
            }
        }
        emit(Op::CallHost, add_host(call.callee),
             static_cast<std::int32_t>(call.args.size()));
        return signature->result;
    }

    std::int32_t default_constant(Type type) {
        return type == Type::String ? add_constant(Value::make_string(""))
                                     : add_constant(Value::make_integer(0));
    }

    static void require_integer(Type type, const char* context) {
        if (type != Type::Integer) {
            throw ScriptError(std::string(context) + " must be an integer");
        }
    }

    static void require_two_integers(Type lhs, Type rhs, const char* op) {
        if (lhs != Type::Integer || rhs != Type::Integer) {
            throw ScriptError(std::string("operator '") + op + "' requires integers");
        }
    }

    Program program_;
    std::unordered_map<std::string, Local> locals_;
};

} // namespace

Program compile(const ast::Script& script) {
    Compiler compiler;
    return compiler.compile(script);
}

Program compile_source(const std::string& source) {
    return compile(parse(source));
}

} // namespace homeworldz::script
