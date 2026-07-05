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

}  // namespace nyx::ir
