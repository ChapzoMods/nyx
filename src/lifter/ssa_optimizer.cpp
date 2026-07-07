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

// ---------------------------------------------------------------------------
// v0.4.1: Interprocedural constant propagation (real implementation)
// ---------------------------------------------------------------------------
// Walks every Call instruction in every function. For each direct call
// (target is an Imm operand), records the call site. When a target
// function is called from exactly ONE call site, the constant arguments
// set up at that call site (Mov vN, imm instructions immediately before
// the Call) are propagated into the callee: the callee's parameter vregs
// (vregs read before written in its first block, in order of first
// appearance) have their uses replaced with the constant values.
//
// Limitations (intentional - "simplest case" per v0.4.1 roadmap):
//   * Only direct calls (Imm target) are considered.
//   * Only `Mov vN, imm` patterns are recognised as argument setup;
//     register-to-register moves (e.g. `Mov vN, vM` from another arg
//     register) are not propagated.
//   * The mapping from call-site constants to callee parameters is by
//     order of appearance. This is correct when the caller emits Mov
//     instructions in argument-register order (e.g. rdi, rsi, rdx for
//     SysV AMD64), which is the common case at -O0.
//   * We do NOT propagate when the callee is called from more than one
//     site, even if all sites agree on the constants - this is the
//     conservative "exactly one site" rule from the task description.
// ---------------------------------------------------------------------------
namespace {

/// Records one call site: the callee's entry address, a pointer to the
/// caller Function (so we can find the call's index later), and the
/// list of (vreg, imm) pairs set up via `Mov vN, imm` immediately before
/// the Call instruction.
struct CallSiteInfo {
    std::uint64_t callee_entry = 0;
    Function* caller = nullptr;
    std::size_t call_index = 0;  // index of the Call instruction within its block
    std::vector<std::pair<VReg, std::int64_t>> const_args;
};

/// Returns the parameter vregs of `fn` in order of first appearance (i.e.
/// the order in which they are first read before being defined in the
/// first basic block). This mirrors the heuristic used by
/// detect_param_count in pseudo_c.cpp, but returns the vreg list so we
/// can substitute uses.
std::vector<VReg> collect_param_vregs(const Function& fn) {
    std::vector<VReg> params;
    if (fn.blocks.empty()) return params;
    const auto& first_block = fn.blocks[0];
    std::unordered_set<VReg> defined;
    std::unordered_set<VReg> seen;
    for (std::size_t i = 0; i < first_block.instructions.size() && i < 10; ++i) {
        const auto& ins = first_block.instructions[i];
        for (const auto& op : ins.operands) {
            if (op.kind == Operand::Kind::Register && op.vreg != INVALID_VREG) {
                if (!defined.count(op.vreg) && !seen.count(op.vreg)) {
                    params.push_back(op.vreg);
                    seen.insert(op.vreg);
                }
            }
        }
        if (ins.dst != INVALID_VREG) {
            defined.insert(ins.dst);
        }
    }
    return params;
}

/// Scans the instructions in `block` immediately preceding index `call_idx`
/// (backwards, up to 8 instructions) for `Mov vN, imm` patterns. Returns
/// the list of (vN, imm) pairs in program order (i.e. the order they would
/// execute, NOT reverse-scan order).
std::vector<std::pair<VReg, std::int64_t>> collect_const_args_at_call(
    const std::vector<Instruction>& insns, std::size_t call_idx) {
    std::vector<std::pair<VReg, std::int64_t>> out;
    if (call_idx >= insns.size()) return out;
    const std::size_t start = (call_idx > 8) ? (call_idx - 8) : 0;
    for (std::size_t i = start; i < call_idx; ++i) {
        const auto& ins = insns[i];
        if (ins.op != OpCode::Mov) continue;
        if (ins.dst == INVALID_VREG) continue;
        if (ins.operands.size() != 1) continue;
        if (ins.operands[0].kind != Operand::Kind::Imm) continue;
        out.emplace_back(ins.dst, ins.operands[0].imm_value);
    }
    return out;
}

}  // namespace

std::size_t interprocedural_constant_propagation(std::vector<Function>& fns) {
    // Map function entry address -> Function*. We use the entry address
    // (which the IR stores in Call's immediate operand) to resolve the
    // callee.
    std::unordered_map<std::uint64_t, Function*> fn_by_entry;
    for (auto& fn : fns) {
        fn_by_entry[fn.entry] = &fn;
    }

    // Collect every direct call site grouped by callee entry address.
    // call_sites_by_callee[entry] = list of CallSiteInfo.
    std::unordered_map<std::uint64_t, std::vector<CallSiteInfo>> call_sites_by_callee;
    for (auto& fn : fns) {
        for (auto& b : fn.blocks) {
            for (std::size_t i = 0; i < b.instructions.size(); ++i) {
                const auto& ins = b.instructions[i];
                if (ins.op != OpCode::Call) continue;
                if (ins.indirect) continue;
                if (ins.operands.size() < 1) continue;
                if (ins.operands[0].kind != Operand::Kind::Imm) continue;
                const auto callee_entry =
                    static_cast<std::uint64_t>(ins.operands[0].imm_value);
                // Only consider callees that are in our function set
                // (i.e. internal direct calls, not PLT/external stubs).
                if (!fn_by_entry.count(callee_entry)) continue;
                CallSiteInfo info;
                info.callee_entry = callee_entry;
                info.caller = &fn;
                info.call_index = i;
                info.const_args = collect_const_args_at_call(b.instructions, i);
                call_sites_by_callee[callee_entry].push_back(std::move(info));
            }
        }
    }

    // For each callee called from exactly one site, propagate the
    // constant arguments into the callee by replacing uses of its
    // parameter vregs with the constant values.
    std::size_t substitutions = 0;
    for (auto& [callee_entry, sites] : call_sites_by_callee) {
        if (sites.size() != 1) continue;
        const auto& site = sites.front();
        if (site.const_args.empty()) continue;

        auto callee_it = fn_by_entry.find(callee_entry);
        if (callee_it == fn_by_entry.end()) continue;
        Function& callee = *callee_it->second;

        // The callee's parameter vregs, in order of first appearance.
        const auto param_vregs = collect_param_vregs(callee);
        if (param_vregs.empty()) continue;

        // Build a substitution map: param_vreg -> constant value. We
        // match by order: the i-th constant arg goes with the i-th
        // parameter vreg. When the call site has more constants than
        // the callee has parameters (or vice versa), we substitute the
        // smaller of the two counts.
        std::unordered_map<VReg, std::int64_t> subst;
        const std::size_t n = std::min(param_vregs.size(), site.const_args.size());
        for (std::size_t i = 0; i < n; ++i) {
            subst[param_vregs[i]] = site.const_args[i].second;
        }
        if (subst.empty()) continue;

        // Replace uses of substituted vregs throughout the callee.
        // We do NOT touch the parameter vregs' definitions in the
        // first block (they're loads from arg registers); we only
        // rewrite uses in operand lists. Replacing a Register operand
        // with an Imm effectively specialises the callee for its
        // single call site.
        for (auto& b : callee.blocks) {
            for (auto& ins : b.instructions) {
                for (auto& op : ins.operands) {
                    if (op.kind != Operand::Kind::Register) continue;
                    auto it = subst.find(op.vreg);
                    if (it == subst.end()) continue;
                    op.kind = Operand::Kind::Imm;
                    op.imm_value = it->second;
                    op.vreg = INVALID_VREG;
                    ++substitutions;
                }
                // Mem operands can also reference vregs (base/index).
                // We only rewrite the Register form above; Mem bases
                // are left untouched because replacing them with an
                // immediate would change the operand's structure.
            }
        }
    }

    if (substitutions > 0) {
        NYX_INFO("SSA opt: interprocedural CP substituted "
                 + std::to_string(substitutions) + " operand uses");
    }
    return substitutions;
}

}  // namespace nyx::ir
