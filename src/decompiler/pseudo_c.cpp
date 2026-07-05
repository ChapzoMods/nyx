// =============================================================================
// Nyx - A headless decompilation engine
// Copyright (C) 2024-2026 Chapzoo  <https://github.com/Chapzoo>
// SPDX-License-Identifier: GPL-3.0-or-later
// =============================================================================
#include "nyx/decompiler/pseudo_c.hpp"

#include "nyx/core/bytes.hpp"

#include <sstream>
#include <string>

namespace nyx {

std::string_view op_name_short(ir::OpCode op) noexcept {
    switch (op) {
        case ir::OpCode::Add: return "+";
        case ir::OpCode::Sub: return "-";
        case ir::OpCode::Mul: return "*";
        case ir::OpCode::Div: return "/";
        case ir::OpCode::Mod: return "%";
        case ir::OpCode::And: return "&";
        case ir::OpCode::Or:  return "|";
        case ir::OpCode::Xor: return "^";
        case ir::OpCode::Shl: return "<<";
        case ir::OpCode::Shr: return ">>";
        case ir::OpCode::Sar: return ">>";  // arithmetic; we lose the signedness in C-level render
        default:              return "?";
    }
}

std::string render_operand(const ir::Operand& o) {
    switch (o.kind) {
        case ir::Operand::Kind::Register:
            return "v" + std::to_string(o.vreg);
        case ir::Operand::Kind::Imm:
            return to_hex(static_cast<std::uint64_t>(o.imm_value), 0, true);
        case ir::Operand::Kind::Mem: {
            std::string s = "*(";
            // size hint
            switch (o.mem_size) {
                case 1:  s += "u8*";  break;
                case 2:  s += "u16*"; break;
                case 4:  s += "u32*"; break;
                case 8:  s += "u64*"; break;
                default: s += "void*";break;
            }
            s += ")(";
            bool any = false;
            if (o.mem_base != ir::INVALID_VREG) { s += "v" + std::to_string(o.mem_base); any = true; }
            if (o.mem_index != ir::INVALID_VREG) {
                if (any) s += " + ";
                s += "v" + std::to_string(o.mem_index);
                if (o.mem_scale > 1) {
                    s += " * " + std::to_string(o.mem_scale);
                }
                any = true;
            }
            if (o.mem_disp != 0) {
                if (any) {
                    s += " + ";
                    s += to_hex(static_cast<std::uint64_t>(o.mem_disp), 0, true);
                } else {
                    s += to_hex(static_cast<std::uint64_t>(o.mem_disp), 0, true);
                }
            }
            s += ")";
            return s;
        }
        case ir::Operand::Kind::Label:
            return "L_" + to_hex(o.label_addr, 0, false);
        case ir::Operand::Kind::Symbol:
            return o.symbol;
    }
    return "?";
}

std::string render_instruction(const ir::Instruction& i) {
    std::ostringstream os;
    switch (i.op) {
        case ir::OpCode::Mov:
            os << "v" << i.dst << " = " << render_operand(i.operands[0]) << ";";
            break;
        case ir::OpCode::Load:
            os << "v" << i.dst << " = " << render_operand(i.operands[0]) << ";";
            break;
        case ir::OpCode::Store:
            os << render_operand(i.operands[0]) << " = " << render_operand(i.operands[1]) << ";";
            break;
        case ir::OpCode::Add: case ir::OpCode::Sub: case ir::OpCode::Mul:
        case ir::OpCode::Div: case ir::OpCode::Mod: case ir::OpCode::And:
        case ir::OpCode::Or:  case ir::OpCode::Xor: case ir::OpCode::Shl:
        case ir::OpCode::Shr: case ir::OpCode::Sar:
            os << "v" << i.dst << " = " << render_operand(i.operands[0])
               << ' ' << op_name_short(i.op) << ' ' << render_operand(i.operands[1]) << ";";
            break;
        case ir::OpCode::Neg:
            os << "v" << i.dst << " = -" << render_operand(i.operands[0]) << ";";
            break;
        case ir::OpCode::Not:
            os << "v" << i.dst << " = ~" << render_operand(i.operands[0]) << ";";
            break;
        case ir::OpCode::Cmp:
            os << "v" << i.dst << " = (" << render_operand(i.operands[0])
               << " == " << render_operand(i.operands[1]) << ");";
            break;
        case ir::OpCode::Branch:
            os << "goto " << render_operand(i.operands[0]) << ";";
            break;
        case ir::OpCode::BranchCond:
            os << "if (" << render_operand(i.operands[0]) << ") goto "
               << render_operand(i.operands[1]) << ";";
            break;
        case ir::OpCode::Call:
            os << "call(" << render_operand(i.operands[0]) << ");";
            break;
        case ir::OpCode::Return:
            os << "return;";
            break;
        case ir::OpCode::Push:
            os << "push(" << render_operand(i.operands[0]) << ");";
            break;
        case ir::OpCode::Pop:
            os << "v" << i.dst << " = pop();";
            break;
        case ir::OpCode::Nop:
            os << "; // nop";
            break;
        case ir::OpCode::Opaque:
            os << "; // " << (i.raw_mnemonic.empty() ? "opaque" : i.raw_mnemonic);
            break;
    }
    return os.str();
}

std::string render_pseudo_c(const ir::Function& fn) {
    std::ostringstream os;
    os << "// Function: " << (fn.name.empty() ? "sub" : fn.name) << "\n";
    os << "// Entry: 0x" << std::hex << fn.entry << std::dec << "\n";
    os << "// Blocks: " << fn.blocks.size() << "\n";
    os << "void " << (fn.name.empty() ? "sub" : fn.name) << "(void) {\n";

    for (std::size_t i = 0; i < fn.blocks.size(); ++i) {
        const auto& b = fn.blocks[i];
        os << "  // block " << i << " @ 0x" << std::hex << b.start_addr << std::dec << "\n";
        os << "  L_" << std::hex << b.start_addr << std::dec << ":\n";
        for (const auto& ins : b.instructions) {
            os << "    " << render_instruction(ins) << "\n";
        }
        if (!b.successors.empty()) {
            os << "    // successors: ";
            for (std::size_t j = 0; j < b.successors.size(); ++j) {
                if (j) os << ", ";
                os << "L_" << std::hex << b.successors[j] << std::dec;
            }
            os << "\n";
        }
    }
    os << "}\n";
    return os.str();
}

}  // namespace nyx
