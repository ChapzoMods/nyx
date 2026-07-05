// =============================================================================
// Nyx - A headless decompilation engine
// Copyright (C) 2024-2026 Chapzoo  <https://github.com/Chapzoo>
// SPDX-License-Identifier: GPL-3.0-or-later
// =============================================================================
#include "nyx/decompiler/pseudo_c.hpp"

#include "nyx/core/bytes.hpp"

#include <sstream>
#include <string>
#include <unordered_map>

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

    // Build a quick lookup: block start_addr -> index. Used to detect the
    // if/else pattern (BranchCond + fall-through Branch).
    std::unordered_map<std::uint64_t, std::size_t> by_addr;
    for (std::size_t i = 0; i < fn.blocks.size(); ++i) {
        by_addr[fn.blocks[i].start_addr] = i;
    }

    auto label = [](std::uint64_t a) {
        std::ostringstream s;
        s << "L_" << std::hex << a << std::dec;
        return s.str();
    };

    for (std::size_t i = 0; i < fn.blocks.size(); ++i) {
        const auto& b = fn.blocks[i];
        os << "  // block " << i << " @ 0x" << std::hex << b.start_addr << std::dec << "\n";
        os << "  " << label(b.start_addr) << ":\n";

        // Detect the if/else pattern: the block ends with BranchCond and the
        // next instruction in the *same* block would be a Branch. Since our
        // CFG splits terminators into their own block tails, the pattern is:
        //   block[i] ends with BranchCond(cond, target_else)
        //   block[i+1] (fall-through) is the "then" branch
        // We emit the BranchCond as `if (cond) goto else_label;` and let the
        // fall-through naturally represent the then-branch.
        bool emitted_if = false;
        const ir::Instruction* last = b.instructions.empty() ? nullptr : &b.instructions.back();

        for (std::size_t k = 0; k < b.instructions.size(); ++k) {
            const auto& ins = b.instructions[k];
            const bool is_last = (k + 1 == b.instructions.size());

            // If this is the BranchCond terminator of a block that has a
            // single fall-through successor, emit it as `if (!cond) goto then;`
            // style. The raw form is `if (cond) goto L_else;`.
            if (is_last && ins.op == ir::OpCode::BranchCond && ins.operands.size() >= 2
                && ins.operands[1].kind == ir::Operand::Kind::Label) {
                const auto target = ins.operands[1].label_addr;
                os << "    if (" << render_operand(ins.operands[0])
                   << ") goto " << label(target) << ";\n";
                emitted_if = true;
                continue;
            }
            // If this is a Branch that jumps backwards or far away, emit as
            // `goto L;` (the fall-through case is implicit).
            if (is_last && ins.op == ir::OpCode::Branch && !ins.operands.empty()
                && ins.operands[0].kind == ir::Operand::Kind::Label) {
                const auto target = ins.operands[0].label_addr;
                // Only emit an explicit goto when the target is NOT the very
                // next block (which would be a redundant jump).
                const auto it = by_addr.find(target);
                const bool is_next_block = (it != by_addr.end() && it->second == i + 1);
                if (!is_next_block) {
                    os << "    goto " << label(target) << ";\n";
                }
                continue;
            }
            os << "    " << render_instruction(ins) << "\n";
        }
        (void)emitted_if;

        // Successor comment for blocks that don't terminate on a branch we
        // already rendered (helps when reading the raw CFG).
        if (!b.successors.empty() && !(last && (last->op == ir::OpCode::Branch
                                              || last->op == ir::OpCode::BranchCond))) {
            os << "    // successors: ";
            for (std::size_t j = 0; j < b.successors.size(); ++j) {
                if (j) os << ", ";
                os << label(b.successors[j]);
            }
            os << "\n";
        }
    }
    os << "}\n";
    return os.str();
}

}  // namespace nyx
