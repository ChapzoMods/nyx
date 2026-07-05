// =============================================================================
// Nyx - A headless decompilation engine
// Copyright (C) 2024-2026 Chapzoo  <https://github.com/Chapzoo>
// SPDX-License-Identifier: GPL-3.0-or-later
// =============================================================================
#include "nyx/lifter/ir.hpp"

#include <string_view>

namespace nyx::ir {

std::string_view op_name(OpCode op) noexcept {
    switch (op) {
        case OpCode::Mov:        return "mov";
        case OpCode::Load:       return "load";
        case OpCode::Store:      return "store";
        case OpCode::Add:        return "add";
        case OpCode::Sub:        return "sub";
        case OpCode::Mul:        return "mul";
        case OpCode::Div:        return "div";
        case OpCode::Mod:        return "mod";
        case OpCode::And:        return "and";
        case OpCode::Or:         return "or";
        case OpCode::Xor:        return "xor";
        case OpCode::Shl:        return "shl";
        case OpCode::Shr:        return "shr";
        case OpCode::Sar:        return "sar";
        case OpCode::Neg:        return "neg";
        case OpCode::Not:        return "not";
        case OpCode::Cmp:        return "cmp";
        case OpCode::Branch:     return "br";
        case OpCode::BranchCond: return "br_cond";
        case OpCode::Call:       return "call";
        case OpCode::Return:     return "ret";
        case OpCode::Push:       return "push";
        case OpCode::Pop:        return "pop";
        case OpCode::Nop:        return "nop";
        case OpCode::Opaque:     return "opaque";
    }
    return "?";
}

std::string_view type_name(Type t) noexcept {
    switch (t) {
        case Type::Unknown: return "unknown";
        case Type::Int8:    return "i8";
        case Type::Int16:   return "i16";
        case Type::Int32:   return "i32";
        case Type::Int64:   return "i64";
        case Type::Ptr:     return "ptr";
        case Type::Func:    return "func";
    }
    return "unknown";
}

std::string_view type_c_decl(Type t) noexcept {
    switch (t) {
        case Type::Unknown: return "void*";
        case Type::Int8:    return "char";
        case Type::Int16:   return "short";
        case Type::Int32:   return "int";
        case Type::Int64:   return "long long";
        case Type::Ptr:     return "void*";
        case Type::Func:    return "void (*)(void)";
    }
    return "void*";
}

std::uint8_t type_size(Type t, std::uint8_t arch_bitness) noexcept {
    switch (t) {
        case Type::Unknown: return 0;
        case Type::Int8:    return 1;
        case Type::Int16:   return 2;
        case Type::Int32:   return 4;
        case Type::Int64:   return 8;
        case Type::Ptr:     return arch_bitness == 64 ? 8 : (arch_bitness == 32 ? 4 : 0);
        case Type::Func:    return arch_bitness == 64 ? 8 : (arch_bitness == 32 ? 4 : 0);
    }
    return 0;
}

}  // namespace nyx::ir
