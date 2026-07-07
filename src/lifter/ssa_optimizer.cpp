// =============================================================================
// Nyx - A headless decompilation engine
// Copyright (C) 2024-2026 Chapzoo  <https://github.com/Chapzoo>
// SPDX-License-Identifier: GPL-3.0-or-later
// =============================================================================
#include "nyx/lifter/ssa_optimizer.hpp"

#include "nyx/core/logger.hpp"

#include <algorithm>
#include <unordered_set>

namespace nyx::ir {

// ---------------------------------------------------------------------------
// Helper: check if an instruction has side effects (can't be DCE'd).
// ---------------------------------------------------------------------------
static bool has_side_effects(const Instruction& ins) noexcept {
    switch (ins.op) {
        case OpCode::Store:
        case OpCode::Call:
        case OpCode::Branch:
        case OpCode::BranchCond:
        case OpCode::Return:
        case OpCode::Push:
        case OpCode::Opaque:
            return true;
        default:
            return false;
    }
}

// ---------------------------------------------------------------------------
// Helper: check if an operand is an immediate.
// ---------------------------------------------------------------------------
static bool is_imm(const Operand& op) noexcept {
    return op.kind == Operand::Kind::Imm;
}

static std::int64_t imm_val(const Operand& op) noexcept {
    return op.imm_value;
}

// ---------------------------------------------------------------------------
// constant_folding_pass
// ---------------------------------------------------------------------------
std::size_t constant_folding_pass(Function& fn) {
    std::size_t changed = 0;
    for (auto& b : fn.blocks) {
        for (auto& ins : b.instructions) {
            if (ins.dst == INVALID_VREG) continue;
            if (ins.operands.size() < 2) continue;
            // Only fold binary ops where both operands are immediates.
            const auto& a = ins.operands[0];
            const auto& bb = ins.operands[1];
            if (!is_imm(a) || !is_imm(bb)) continue;

            std::optional<std::int64_t> result;
            switch (ins.op) {
                case OpCode::Add: result = imm_val(a) + imm_val(bb); break;
                case OpCode::Sub: result = imm_val(a) - imm_val(bb); break;
                case OpCode::Mul: result = imm_val(a) * imm_val(bb); break;
                case OpCode::Div:
                    if (imm_val(bb) != 0) result = imm_val(a) / imm_val(bb);
                    break;
                case OpCode::Mod:
                    if (imm_val(bb) != 0) result = imm_val(a) % imm_val(bb);
                    break;
                case OpCode::And: result = imm_val(a) & imm_val(bb); break;
                case OpCode::Or:  result = imm_val(a) | imm_val(bb); break;
                case OpCode::Xor: result = imm_val(a) ^ imm_val(bb); break;
                case OpCode::Shl: result = imm_val(a) << (imm_val(bb) & 63); break;
                case OpCode::Shr: result = static_cast<std::uint64_t>(imm_val(a)) >> (imm_val(bb) & 63); break;
                case OpCode::Sar: result = imm_val(a) >> (imm_val(bb) & 63); break;
                case OpCode::Cmp: result = (imm_val(a) == imm_val(bb)) ? 1 : 0; break;
                default: break;
            }
            if (result.has_value()) {
                ins.op = OpCode::Mov;
                ins.operands.clear();
                ins.operands.push_back(Operand::imm(*result));
                ++changed;
            }
        }
    }
    if (changed > 0) {
        NYX_INFO("SSA opt: constant folding changed " + std::to_string(changed) + " instructions");
    }
    return changed;
}

// ---------------------------------------------------------------------------
// expression_simplification_pass
// ---------------------------------------------------------------------------
std::size_t expression_simplification_pass(Function& fn) {
    std::size_t changed = 0;
    for (auto& b : fn.blocks) {
        for (auto& ins : b.instructions) {
            if (ins.dst == INVALID_VREG) continue;
            if (ins.operands.size() < 2) continue;
            const auto& a = ins.operands[0];
            const auto& bb = ins.operands[1];

            // v1 + 0 -> v1 (Mov)
            // v1 - 0 -> v1
            // v1 * 1 -> v1
            // v1 | 0 -> v1
            // v1 & -1 (all ones) -> v1
            // v1 ^ 0 -> v1
            // v1 * 0 -> 0
            // v1 & 0 -> 0
            // v1 << 0 -> v1
            // v1 >> 0 -> v1
            bool simplified = false;
            if (is_imm(bb)) {
                const auto v = imm_val(bb);
                switch (ins.op) {
                    case OpCode::Add:
                    case OpCode::Sub:
                    case OpCode::Or:
                    case OpCode::Xor:
                        if (v == 0) {
                            ins.op = OpCode::Mov;
                            ins.operands = {a};
                            simplified = true;
                        }
                        break;
                    case OpCode::Mul:
                        if (v == 1) {
                            ins.op = OpCode::Mov;
                            ins.operands = {a};
                            simplified = true;
                        } else if (v == 0) {
                            ins.op = OpCode::Mov;
                            ins.operands = {Operand::imm(0)};
                            simplified = true;
                        }
                        break;
                    case OpCode::And:
                        if (v == 0) {
                            ins.op = OpCode::Mov;
                            ins.operands = {Operand::imm(0)};
                            simplified = true;
                        } else if (v == -1) {
                            ins.op = OpCode::Mov;
                            ins.operands = {a};
                            simplified = true;
                        }
                        break;
                    case OpCode::Shl:
                    case OpCode::Shr:
                    case OpCode::Sar:
                        if (v == 0) {
                            ins.op = OpCode::Mov;
                            ins.operands = {a};
                            simplified = true;
                        }
                        break;
                    default: break;
                }
            }
            // Also check the first operand for commutative ops.
            if (!simplified && is_imm(a)) {
                const auto v = imm_val(a);
                switch (ins.op) {
                    case OpCode::Add:
                    case OpCode::Or:
                    case OpCode::Xor:
                        if (v == 0) {
                            ins.op = OpCode::Mov;
                            ins.operands = {bb};
                            simplified = true;
                        }
                        break;
                    case OpCode::Mul:
                        if (v == 1) {
                            ins.op = OpCode::Mov;
                            ins.operands = {bb};
                            simplified = true;
                        } else if (v == 0) {
                            ins.op = OpCode::Mov;
                            ins.operands = {Operand::imm(0)};
                            simplified = true;
                        }
                        break;
                    case OpCode::And:
                        if (v == 0) {
                            ins.op = OpCode::Mov;
                            ins.operands = {Operand::imm(0)};
                            simplified = true;
                        }
                        break;
                    default: break;
                }
            }
            if (simplified) ++changed;
        }
    }
    if (changed > 0) {
        NYX_INFO("SSA opt: expression simplification changed " + std::to_string(changed) + " instructions");
    }
    return changed;
}

// ---------------------------------------------------------------------------
// dead_code_elimination_pass
// ---------------------------------------------------------------------------
std::size_t dead_code_elimination_pass(Function& fn) {
    // Collect all vregs that are used as operands.
    std::unordered_set<VReg> used;
    for (const auto& b : fn.blocks) {
        for (const auto& ins : b.instructions) {
            for (const auto& op : ins.operands) {
                if (op.kind == Operand::Kind::Register && op.vreg != INVALID_VREG) {
                    used.insert(op.vreg);
                }
                if (op.kind == Operand::Kind::Mem) {
                    if (op.mem_base != INVALID_VREG) used.insert(op.mem_base);
                    if (op.mem_index != INVALID_VREG) used.insert(op.mem_index);
                }
            }
        }
    }

    std::size_t removed = 0;
    for (auto& b : fn.blocks) {
        auto& insns = b.instructions;
        insns.erase(
            std::remove_if(insns.begin(), insns.end(),
                [&](const Instruction& ins) {
                    if (has_side_effects(ins)) return false;
                    if (ins.dst == INVALID_VREG) return false;
                    if (used.count(ins.dst)) return false;
                    ++removed;
                    return true;
                }),
            insns.end());
    }
    if (removed > 0) {
        NYX_INFO("SSA opt: dead code elimination removed " + std::to_string(removed) + " instructions");
    }
    return removed;
}

// ---------------------------------------------------------------------------
// optimize (fixpoint)
// ---------------------------------------------------------------------------
std::size_t optimize(Function& fn, const OptimizationOptions& opts) {
    std::size_t total = 0;
    for (int iter = 0; iter < 5; ++iter) {  // max 5 iterations
        std::size_t changed = 0;
        if (opts.constant_folding)    changed += constant_folding_pass(fn);
        if (opts.expr_simplification) changed += expression_simplification_pass(fn);
        if (opts.dead_code_elim)      changed += dead_code_elimination_pass(fn);
        total += changed;
        if (changed == 0) break;
    }
    return total;
}

}  // namespace nyx::ir
