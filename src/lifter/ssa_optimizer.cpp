// =============================================================================
// Nyx - A headless decompilation engine
// Copyright (C) 2024-2026 Chapzoo  <https://github.com/Chapzoo>
// SPDX-License-Identifier: GPL-3.0-or-later
// =============================================================================
#include "nyx/lifter/ssa_optimizer.hpp"

#include "nyx/core/logger.hpp"

#include <algorithm>
#include <cstdint>
#include <unordered_map>
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
// dead_store_elimination_pass (v0.3.1)
// ---------------------------------------------------------------------------
// Within a single basic block, when a Store writes to a constant address
// that was already written by an earlier Store in the same block — and no
// Load from that address or Call has happened in between — the earlier
// Store is dead (its value is unobservable) and can be removed. We only
// model addresses that are fully known at compile time:
//   * an Imm operand (`store(Imm(0x1000), v1)`), or
//   * a Mem operand with no base and no index register (e.g.
//     `Operand::mem(INVALID_VREG, 0x1000)`), which is how the lifter
//     emits absolute-address stores.
//
// Stores through register addresses are too complex for v0.3.1 and are
// ignored by this pass. Calls are conservatively treated as potentially
// reading or writing any memory address, so they flush the entire map.
// ---------------------------------------------------------------------------
static bool store_address_is_constant(const Operand& addr) noexcept {
    if (addr.kind == Operand::Kind::Imm) return true;
    if (addr.kind == Operand::Kind::Mem
        && addr.mem_base == INVALID_VREG
        && addr.mem_index == INVALID_VREG) {
        return true;
    }
    return false;
}

static std::uint64_t store_address_key(const Operand& addr) noexcept {
    // Pre: store_address_is_constant(addr) is true.
    if (addr.kind == Operand::Kind::Imm) {
        return static_cast<std::uint64_t>(addr.imm_value);
    }
    return static_cast<std::uint64_t>(addr.mem_disp);
}

std::size_t dead_store_elimination_pass(Function& fn) {
    std::size_t removed = 0;
    for (auto& b : fn.blocks) {
        auto& insns = b.instructions;
        if (insns.empty()) continue;

        // Map of constant-address key -> index of the most recent Store
        // to that address (with no intervening Load/Call). When a new
        // Store to the same key is encountered, the prior entry is
        // marked for removal and replaced.
        std::unordered_map<std::uint64_t, std::size_t> last_store;
        std::unordered_set<std::size_t> dead_indexes;

        for (std::size_t i = 0; i < insns.size(); ++i) {
            auto& ins = insns[i];
            if (ins.op == OpCode::Store && ins.operands.size() >= 1) {
                const auto& addr = ins.operands[0];
                if (store_address_is_constant(addr)) {
                    auto key = store_address_key(addr);
                    auto it = last_store.find(key);
                    if (it != last_store.end()) {
                        // Previous store to the same address with no
                        // intervening Load or Call — it is dead.
                        dead_indexes.insert(it->second);
                        ++removed;
                    }
                    last_store[key] = i;
                }
                continue;
            }
            if (ins.op == OpCode::Load && ins.operands.size() >= 1) {
                // A Load from a constant address observes any prior
                // Store to that address, so the Store is no longer
                // a candidate for elimination by future Stores.
                const auto& addr = ins.operands[0];
                if (store_address_is_constant(addr)) {
                    last_store.erase(store_address_key(addr));
                }
                // Loads through register addresses are conservatively
                // treated as reading from any tracked Store — flush.
                continue;
            }
            if (ins.op == OpCode::Call) {
                // Calls may read or write any memory location.
                last_store.clear();
                continue;
            }
            // Other opcodes (Mov, arithmetic, Branch, Return, Push, Pop,
            // etc.) do not access tracked memory addresses, so the map
            // stays unchanged.
        }

        if (dead_indexes.empty()) continue;

        // Rewrite the block without the dead stores. We can't use
        // std::remove_if with a predicate that depends on indexes
        // because indexes shift after removal, so we build a new vector.
        std::vector<Instruction> kept;
        kept.reserve(insns.size() - dead_indexes.size());
        for (std::size_t i = 0; i < insns.size(); ++i) {
            if (dead_indexes.count(i)) continue;
            kept.push_back(std::move(insns[i]));
        }
        insns = std::move(kept);
    }
    if (removed > 0) {
        NYX_INFO("SSA opt: dead store elimination removed " + std::to_string(removed) + " stores");
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
        if (opts.dead_store_elim)     changed += dead_store_elimination_pass(fn);
        if (opts.dead_code_elim)      changed += dead_code_elimination_pass(fn);
        total += changed;
        if (changed == 0) break;
    }
    return total;
}

}  // namespace nyx::ir
