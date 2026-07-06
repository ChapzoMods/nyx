// =============================================================================
// Nyx - A headless decompilation engine
// Copyright (C) 2024-2026 Chapzoo  <https://github.com/Chapzoo>
// SPDX-License-Identifier: GPL-3.0-or-later
// =============================================================================
//
// SSA construction using the standard Cytron et al. algorithm:
//   1. Compute dominance frontiers.
//   2. Insert phi nodes at DF of every definition.
//   3. Rename variables via dominator-tree DFS with per-variable stacks.
// =============================================================================
#include "nyx/lifter/ssa_builder.hpp"

#include "nyx/core/logger.hpp"

#include <algorithm>
#include <stack>

namespace nyx::ir {

// ---------------------------------------------------------------------------
// compute_dominance_frontiers
// ---------------------------------------------------------------------------
std::vector<std::unordered_set<std::size_t>>
compute_dominance_frontiers(const Function& fn, const DominatorAnalysis& dom) {
    std::vector<std::unordered_set<std::size_t>> df(fn.blocks.size());

    // Build predecessor lists.
    std::vector<std::vector<std::size_t>> preds(fn.blocks.size());
    for (std::size_t i = 0; i < fn.blocks.size(); ++i) {
        if (!dom.reachable(i)) continue;
        for (auto succ_addr : fn.blocks[i].successors) {
            auto it = fn.block_index.find(succ_addr);
            if (it == fn.block_index.end()) continue;
            preds[it->second].push_back(i);
        }
    }

    // For each block with >= 2 predecessors, walk up the idom tree.
    for (std::size_t b = 0; b < fn.blocks.size(); ++b) {
        if (preds[b].size() < 2) continue;
        for (std::size_t p : preds[b]) {
            std::size_t runner = p;
            while (runner != b) {
                auto idom = dom.immediate_dominator(runner);
                if (!idom.has_value()) break;
                if (idom.value() == b) break;
                df[runner].insert(b);
                runner = idom.value();
            }
            // Also add b to p's DF if p doesn't dominate b.
            auto idom_p = dom.immediate_dominator(p);
            if (!idom_p.has_value() || idom_p.value() != b) {
                df[p].insert(b);
            }
        }
    }

    return df;
}

// ---------------------------------------------------------------------------
// build_ssa
// ---------------------------------------------------------------------------
SSAResult build_ssa(const Function& fn, const DominatorAnalysis& dom) {
    SSAResult result;
    Function& ssa = result.fn;

    // Handle empty functions gracefully.
    if (fn.blocks.empty()) {
        ssa = fn;
        return result;
    }

    // Copy the function structure (blocks, entry, name, vreg_types).
    ssa = fn;
    ssa.blocks.clear();
    ssa.block_index.clear();

    // Re-create blocks with empty instruction lists (we'll fill them).
    for (std::size_t i = 0; i < fn.blocks.size(); ++i) {
        BasicBlock bb;
        bb.start_addr = fn.blocks[i].start_addr;
        bb.successors = fn.blocks[i].successors;
        ssa.blocks.push_back(std::move(bb));
        ssa.block_index[fn.blocks[i].start_addr] = i;
    }

    // Compute dominance frontiers.
    auto df = compute_dominance_frontiers(fn, dom);

    // Collect all variables that are defined (written) in the function.
    std::unordered_set<VReg> all_vars;
    for (const auto& b : fn.blocks) {
        for (const auto& ins : b.instructions) {
            if (ins.dst != INVALID_VREG) all_vars.insert(ins.dst);
        }
    }

    // Collect definition sites: for each variable, the set of blocks
    // where it's defined.
    std::unordered_map<VReg, std::unordered_set<std::size_t>> def_sites;
    for (const auto v : all_vars) {
        for (std::size_t i = 0; i < fn.blocks.size(); ++i) {
            for (const auto& ins : fn.blocks[i].instructions) {
                if (ins.dst == v) {
                    def_sites[v].insert(i);
                    break;  // one entry per block is enough
                }
            }
        }
    }

    // Insert phi instructions: for each variable v, for each block d in
    // def_sites[v], for each block f in DF(d), insert a phi for v at f
    // (if not already inserted).
    // phi instruction representation: we use OpCode::Mov with a special
    // marker. Since the IR doesn't have a Phi opcode, we model phis as
    // a comment-line in the block header using Opaque with a specific
    // raw_mnemonic prefix "phi:".
    //
    // For v0.1.0 we keep it simple: insert an Opaque instruction at the
    // top of each block that needs a phi, with the mnemonic
    // "phi v<orig> = phi(...)". The renaming pass below then uses the
    // SSA versions for the actual instructions.
    std::unordered_map<std::size_t, std::unordered_set<VReg>> phi_inserted;
    for (const auto v : all_vars) {
        std::unordered_set<std::size_t> worklist(def_sites[v].begin(),
                                                  def_sites[v].end());
        std::unordered_set<std::size_t> has_phi;
        while (!worklist.empty()) {
            std::size_t d = *worklist.begin();
            worklist.erase(worklist.begin());
            for (std::size_t f : df[d]) {
                if (has_phi.count(f)) continue;
                has_phi.insert(f);
                phi_inserted[f].insert(v);
                // If f doesn't already define v, add it to worklist.
                if (!def_sites[v].count(f)) {
                    def_sites[v].insert(f);
                    worklist.insert(f);
                }
            }
        }
    }

    // Renaming pass: walk the dominator tree in DFS, maintaining a stack
    // of current SSA versions per original variable.
    VReg next_ssa = 1;  // SSA versions start at 1
    std::unordered_map<VReg, std::stack<VReg>> stacks;
    // Initialize stacks for all variables with version 0 (undefined).
    for (const auto v : all_vars) {
        stacks[v].push(0);  // bottom of stack = undefined
    }

    auto new_name = [&](VReg orig) -> VReg {
        const VReg v = next_ssa++;
        stacks[orig].push(v);
        result.original[v] = orig;
        result.versions[orig].push_back(v);
        return v;
    };

    auto top_name = [&](VReg orig) -> VReg {
        auto it = stacks.find(orig);
        if (it == stacks.end() || it->second.empty()) return 0;
        return it->second.top();
    };

    // Build dominator tree children list.
    std::vector<std::vector<std::size_t>> dom_children(fn.blocks.size());
    auto entry_it = fn.block_index.find(fn.entry);
    std::size_t entry = (entry_it != fn.block_index.end()) ? entry_it->second : 0;
    for (std::size_t i = 0; i < fn.blocks.size(); ++i) {
        if (i == entry) continue;
        auto idom = dom.immediate_dominator(i);
        if (idom.has_value()) {
            dom_children[idom.value()].push_back(i);
        }
    }

    // Recursive rename via explicit stack (avoid stack overflow).
    // Each frame: (block_idx, child_iter)
    struct Frame {
        std::size_t block;
        std::size_t child_idx;
        std::vector<std::pair<VReg, std::size_t>> defs_to_pop;  // (orig, old_stack_size)
    };

    std::vector<Frame> frames;
    frames.push_back({entry, 0, {}});

    while (!frames.empty()) {
        Frame& frame = frames.back();
        std::size_t b = frame.block;

        if (frame.child_idx == 0) {
            // First visit: insert phi instructions and rename.
            // Insert phi Opaque instructions at the top.
            auto pit = phi_inserted.find(b);
            if (pit != phi_inserted.end()) {
                for (const auto v : pit->second) {
                    const VReg new_v = new_name(v);
                    Instruction phi;
                    phi.op = OpCode::Opaque;
                    phi.dst = new_v;
                    phi.addr = ssa.blocks[b].start_addr;
                    phi.raw_mnemonic = "phi v" + std::to_string(v);
                    ssa.blocks[b].instructions.push_back(std::move(phi));
                    frame.defs_to_pop.push_back({v, stacks[v].size() - 1});
                }
            }

            // Rename instructions in this block.
            for (const auto& orig_ins : fn.blocks[b].instructions) {
                Instruction ins = orig_ins;

                // Rename source operands (Register kind).
                for (auto& op : ins.operands) {
                    if (op.kind == Operand::Kind::Register && op.vreg != INVALID_VREG) {
                        op.vreg = top_name(op.vreg);
                    }
                    // Rename memory base/index registers.
                    if (op.kind == Operand::Kind::Mem) {
                        if (op.mem_base != INVALID_VREG) {
                            op.mem_base = top_name(op.mem_base);
                        }
                        if (op.mem_index != INVALID_VREG) {
                            op.mem_index = top_name(op.mem_index);
                        }
                    }
                }

                // Rename destination.
                if (ins.dst != INVALID_VREG) {
                    const VReg orig_dst = ins.dst;
                    ins.dst = new_name(orig_dst);
                    frame.defs_to_pop.push_back({orig_dst, stacks[orig_dst].size() - 1});
                }

                ssa.blocks[b].instructions.push_back(std::move(ins));
            }
        }

        // Process children.
        if (frame.child_idx < dom_children[b].size()) {
            std::size_t child = dom_children[b][frame.child_idx];
            frame.child_idx++;
            frames.push_back({child, 0, {}});
        } else {
            // Done with this block: pop all defs.
            for (auto it = frame.defs_to_pop.rbegin(); it != frame.defs_to_pop.rend(); ++it) {
                auto& [orig, old_size] = *it;
                while (stacks[orig].size() > old_size) {
                    stacks[orig].pop();
                }
            }
            frames.pop_back();
        }
    }

    NYX_INFO("SSA: constructed " + std::to_string(next_ssa - 1) + " SSA versions from "
             + std::to_string(all_vars.size()) + " original vregs");
    return result;
}

}  // namespace nyx::ir
