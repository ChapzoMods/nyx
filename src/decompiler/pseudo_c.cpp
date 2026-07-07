// =============================================================================
// Nyx - A headless decompilation engine
// Copyright (C) 2024-2026 Chapzoo  <https://github.com/Chapzoo>
// SPDX-License-Identifier: GPL-3.0-or-later
// =============================================================================
#include "nyx/decompiler/pseudo_c.hpp"

#include "nyx/core/bytes.hpp"
#include "nyx/core/types.hpp"

#include <algorithm>
#include <cstdint>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace nyx {

// v0.2.1: optional BinaryInfo used by render_instruction to resolve Call
// targets to symbol names. Set via set_render_binary_info() before rendering
// a function and reset to nullptr afterwards. Nullptr by default, which
// reproduces the historical behaviour of emitting the raw immediate.
static const BinaryInfo* g_current_bin = nullptr;

void set_render_binary_info(const BinaryInfo* bin) { g_current_bin = bin; }

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
            if (i.indirect) {
                os << "// indirect branch via " << render_operand(i.operands[0]) << "\n";
                os << "    goto *(" << render_operand(i.operands[0]) << ");  // indirect";
            } else {
                os << "goto " << render_operand(i.operands[0]) << ";";
            }
            break;
        case ir::OpCode::BranchCond:
            os << "if (" << render_operand(i.operands[0]) << ") goto "
               << render_operand(i.operands[1]) << ";";
            break;
        case ir::OpCode::Call:
            if (i.indirect) {
                os << "// indirect call via " << render_operand(i.operands[0]) << "\n";
                os << "    call(*(" << render_operand(i.operands[0]) << "));  // indirect";
            } else {
                std::string call_target = render_operand(i.operands[0]);
                // v0.2.1: try to resolve the target to a symbol name.
                if (g_current_bin && !i.operands.empty()
                    && i.operands[0].kind == ir::Operand::Kind::Imm) {
                    for (const auto& sym : g_current_bin->symbols) {
                        if (sym.value == static_cast<std::uint64_t>(i.operands[0].imm_value)
                            && !sym.name.empty()) {
                            call_target = sym.name;
                            break;
                        }
                    }
                }
                os << "call(" << call_target << ");";
            }
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

    // v0.0.4: emit typed variable declarations for every vreg that has an
    // inferred type. VRegs without a type are declared as `void*` (the
    // conservative default) so every assignment has a valid LHS.
    if (!fn.vreg_types.empty()) {
        // Collect the set of vregs actually used in the function so we don't
        // declare stale entries from a partial inference.
        std::unordered_map<ir::VReg, bool> used;
        for (const auto& b : fn.blocks) {
            for (const auto& ins : b.instructions) {
                if (ins.dst != ir::INVALID_VREG) used[ins.dst] = true;
                for (const auto& op : ins.operands) {
                    if (op.kind == ir::Operand::Kind::Register && op.vreg != ir::INVALID_VREG) {
                        used[op.vreg] = true;
                    }
                }
            }
        }
        // Emit declarations in vreg-id order for stable output.
        std::vector<ir::VReg> sorted;
        sorted.reserve(used.size());
        for (const auto& [v, _] : used) sorted.push_back(v);
        std::sort(sorted.begin(), sorted.end());
        for (auto v : sorted) {
            auto it = fn.vreg_types.find(v);
            const ir::Type t = (it != fn.vreg_types.end()) ? it->second : ir::Type::Unknown;
            os << "  " << ir::type_c_decl(t) << " v" << v << ";\n";
        }
        os << "\n";
    }

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

    // Detect whether block `i` ends with an unconditional Branch to `target`.
    auto ends_with_branch_to = [&](std::size_t idx, std::uint64_t target) -> bool {
        if (idx >= fn.blocks.size()) return false;
        const auto& blk = fn.blocks[idx];
        if (blk.instructions.empty()) return false;
        const auto& last = blk.instructions.back();
        return last.op == ir::OpCode::Branch
            && !last.operands.empty()
            && last.operands[0].kind == ir::Operand::Kind::Label
            && last.operands[0].label_addr == target;
    };

    // Returns the BranchCond target if block `i` ends with one, else 0.
    auto branch_cond_target = [&](std::size_t idx) -> std::uint64_t {
        if (idx >= fn.blocks.size()) return 0;
        const auto& blk = fn.blocks[idx];
        if (blk.instructions.empty()) return 0;
        const auto& last = blk.instructions.back();
        if (last.op == ir::OpCode::BranchCond && last.operands.size() >= 2
            && last.operands[1].kind == ir::Operand::Kind::Label) {
            return last.operands[1].label_addr;
        }
        return 0;
    };

    for (std::size_t i = 0; i < fn.blocks.size(); ++i) {
        const auto& b = fn.blocks[i];
        os << "  // block " << i << " @ 0x" << std::hex << b.start_addr << std::dec << "\n";
        os << "  " << label(b.start_addr) << ":\n";

        const ir::Instruction* last = b.instructions.empty() ? nullptr : &b.instructions.back();

        // Detect if/else pattern: this block ends with BranchCond(cond, L_else),
        // the fall-through block (i+1) is the "then", and the else block
        // (L_else) also branches to the same "end" as the then-block. When
        // we recognise this, we emit:
        //   if (!cond) goto L_else;   // skip then when cond is false
        //   { ... then-block ... }
        //   goto L_end;
        //   L_else: { ... else-block ... }
        //   L_end:
        // For v0.0.3 we keep the goto form (structured if/else reconstruction
        // is on the v0.1.0 roadmap), but we emit the BranchCond as a clean
        // `if (cond) goto L;` so the structure is readable.

        for (std::size_t k = 0; k < b.instructions.size(); ++k) {
            const auto& ins = b.instructions[k];
            const bool is_last = (k + 1 == b.instructions.size());

            if (is_last && ins.op == ir::OpCode::BranchCond && ins.operands.size() >= 2
                && ins.operands[1].kind == ir::Operand::Kind::Label) {
                const auto target = ins.operands[1].label_addr;
                // Check if this is a real if/else: the fall-through (i+1)
                // and the branch target both end with Branch to the same
                // join block. If so, hint it in a comment.
                auto it = by_addr.find(target);
                const auto else_idx = (it != by_addr.end()) ? it->second : fn.blocks.size();
                const std::uint64_t then_end = (i + 1 < fn.blocks.size())
                    ? branch_cond_target(i + 1)  // not quite; we want the Branch target
                    : 0;
                (void)then_end;
                // The fall-through block's Branch target (if any) is the join.
                std::uint64_t join = 0;
                if (i + 1 < fn.blocks.size()) {
                    const auto& fall = fn.blocks[i + 1];
                    if (!fall.instructions.empty()) {
                        const auto& flast = fall.instructions.back();
                        if (flast.op == ir::OpCode::Branch && !flast.operands.empty()
                            && flast.operands[0].kind == ir::Operand::Kind::Label) {
                            join = flast.operands[0].label_addr;
                        }
                    }
                }
                if (else_idx < fn.blocks.size() && join != 0 && ends_with_branch_to(else_idx, join)) {
                    os << "    if (" << render_operand(ins.operands[0])
                       << ") goto " << label(target) << ";  // else-branch\n";
                } else {
                    os << "    if (" << render_operand(ins.operands[0])
                       << ") goto " << label(target) << ";\n";
                }
                continue;
            }
            if (is_last && ins.op == ir::OpCode::Branch && !ins.operands.empty()
                && ins.operands[0].kind == ir::Operand::Kind::Label) {
                const auto target = ins.operands[0].label_addr;
                const auto it = by_addr.find(target);
                const bool is_next_block = (it != by_addr.end() && it->second == i + 1);
                if (!is_next_block) {
                    os << "    goto " << label(target) << ";\n";
                }
                continue;
            }
            os << "    " << render_instruction(ins) << "\n";
        }

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

std::string render_pseudo_c(const ir::Function& fn,
                            const ir::DominatorAnalysis& dom,
                            const std::vector<ir::NaturalLoop>& loops) {
    // For v0.0.5 we add loop annotations: when a block is a loop header,
    // we emit a `while (1) {` comment before it, and when we see the back
    // edge (the latch block), we emit `continue;` instead of the goto.
    // Full structured loop reconstruction is on the v0.1.0 roadmap.

    // Build loop-header lookup.
    std::unordered_map<std::size_t, const ir::NaturalLoop*> loop_by_header;
    std::unordered_map<std::size_t, const ir::NaturalLoop*> loop_by_latch;
    for (const auto& l : loops) {
        loop_by_header[l.header] = &l;
        loop_by_latch[l.latch] = &l;
    }

    std::ostringstream os;
    os << "// Function: " << (fn.name.empty() ? "sub" : fn.name) << "\n";
    os << "// Entry: 0x" << std::hex << fn.entry << std::dec << "\n";
    os << "// Blocks: " << fn.blocks.size() << "\n";
    if (!loops.empty()) {
        os << "// Loops: " << loops.size() << "\n";
    }
    os << "void " << (fn.name.empty() ? "sub" : fn.name) << "(void) {\n";

    // Typed variable declarations (v0.0.4).
    if (!fn.vreg_types.empty()) {
        std::unordered_map<ir::VReg, bool> used;
        for (const auto& b : fn.blocks) {
            for (const auto& ins : b.instructions) {
                if (ins.dst != ir::INVALID_VREG) used[ins.dst] = true;
                for (const auto& op : ins.operands) {
                    if (op.kind == ir::Operand::Kind::Register && op.vreg != ir::INVALID_VREG) {
                        used[op.vreg] = true;
                    }
                }
            }
        }
        std::vector<ir::VReg> sorted;
        sorted.reserve(used.size());
        for (const auto& [v, _] : used) sorted.push_back(v);
        std::sort(sorted.begin(), sorted.end());
        for (auto v : sorted) {
            auto it = fn.vreg_types.find(v);
            const ir::Type t = (it != fn.vreg_types.end()) ? it->second : ir::Type::Unknown;
            os << "  " << ir::type_c_decl(t) << " v" << v << ";\n";
        }
        os << "\n";
    }

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

        // Loop header annotation.
        auto lhit = loop_by_header.find(i);
        if (lhit != loop_by_header.end()) {
            os << "  // --- loop header (latch=L_" << std::hex
               << fn.blocks[lhit->second->latch].start_addr << std::dec << ") ---\n";
            os << "  while (1) {\n";
        }

        os << "  // block " << i << " @ 0x" << std::hex << b.start_addr << std::dec << "\n";
        os << "  " << label(b.start_addr) << ":\n";

        auto lit = loop_by_latch.find(i);

        for (std::size_t k = 0; k < b.instructions.size(); ++k) {
            const auto& ins = b.instructions[k];
            const bool is_last = (k + 1 == b.instructions.size());

            // If this is the back-edge branch of a loop, emit `continue;`.
            // Handles both unconditional Branch and conditional BranchCond
            // whose target is the loop header.
            if (is_last && lit != loop_by_latch.end()) {
                const auto header_addr = fn.blocks[lit->second->header].start_addr;
                if (ins.op == ir::OpCode::Branch && !ins.operands.empty()
                    && ins.operands[0].kind == ir::Operand::Kind::Label
                    && ins.operands[0].label_addr == header_addr) {
                    os << "    continue;  // back edge to L_"
                       << std::hex << header_addr << std::dec << "\n";
                    continue;
                }
                if (ins.op == ir::OpCode::BranchCond && ins.operands.size() >= 2
                    && ins.operands[1].kind == ir::Operand::Kind::Label
                    && ins.operands[1].label_addr == header_addr) {
                    os << "    if (" << render_operand(ins.operands[0])
                       << ") continue;  // back edge to L_"
                       << std::hex << header_addr << std::dec << "\n";
                    continue;
                }
            }
            // If this is a conditional branch exiting the loop, emit `break;`
            // when the target is outside the loop body.
            if (is_last && lit != loop_by_latch.end()
                && ins.op == ir::OpCode::BranchCond && ins.operands.size() >= 2
                && ins.operands[1].kind == ir::Operand::Kind::Label) {
                const auto target = ins.operands[1].label_addr;
                auto tit = by_addr.find(target);
                if (tit != by_addr.end()) {
                    const auto& loop = *lit->second;
                    bool in_loop = false;
                    for (auto bi : loop.body) {
                        if (bi == tit->second) { in_loop = true; break; }
                    }
                    if (!in_loop) {
                        os << "    if (!(" << render_operand(ins.operands[0])
                           << ")) break;  // loop exit\n";
                        continue;
                    }
                }
            }

            if (is_last && ins.op == ir::OpCode::BranchCond && ins.operands.size() >= 2
                && ins.operands[1].kind == ir::Operand::Kind::Label) {
                const auto target = ins.operands[1].label_addr;
                os << "    if (" << render_operand(ins.operands[0])
                   << ") goto " << label(target) << ";\n";
                continue;
            }
            if (is_last && ins.op == ir::OpCode::Branch && !ins.operands.empty()
                && ins.operands[0].kind == ir::Operand::Kind::Label) {
                const auto target = ins.operands[0].label_addr;
                const auto it = by_addr.find(target);
                const bool is_next_block = (it != by_addr.end() && it->second == i + 1);
                if (!is_next_block) {
                    os << "    goto " << label(target) << ";\n";
                }
                continue;
            }
            os << "    " << render_instruction(ins) << "\n";
        }

        if (lhit != loop_by_header.end()) {
            os << "  }  // end while\n";
        }
    }
    os << "}\n";
    return os.str();
}

}  // namespace nyx
