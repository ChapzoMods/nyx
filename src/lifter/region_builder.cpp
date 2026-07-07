// =============================================================================
// Nyx - A headless decompilation engine
// Copyright (C) 2024-2026 Chapzoo  <https://github.com/Chapzoo>
// SPDX-License-Identifier: GPL-3.0-or-later
// =============================================================================
#include "nyx/lifter/region_builder.hpp"

#include "nyx/core/bytes.hpp"
#include "nyx/decompiler/pseudo_c.hpp"
#include "nyx/core/logger.hpp"

#include <algorithm>
#include <functional>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace nyx::ir {

std::string Region::kind_name() const noexcept {
    switch (kind) {
        case Kind::Block:       return "Block";
        case Kind::IfThen:      return "IfThen";
        case Kind::IfThenElse:  return "IfThenElse";
        case Kind::While:       return "While";
        case Kind::DoWhile:     return "DoWhile";
        case Kind::Switch:      return "Switch";
        case Kind::Sequence:    return "Sequence";
        case Kind::Goto:        return "Goto";
    }
    return "?";
}

// ---------------------------------------------------------------------------
// structure_cfg
// ---------------------------------------------------------------------------
std::unique_ptr<Region> structure_cfg(
    const Function& fn,
    const DominatorAnalysis& dom,
    const std::vector<NaturalLoop>& loops)
{
    auto root = std::make_unique<Region>();
    root->kind = Region::Kind::Sequence;

    if (fn.blocks.empty()) return root;

    // Build loop-header lookup.
    std::unordered_map<std::size_t, const NaturalLoop*> loop_by_header;
    for (const auto& l : loops) {
        loop_by_header[l.header] = &l;
    }

    // Build successor lookup.
    auto succ_idx = [&](std::size_t b) -> std::optional<std::size_t> {
        if (b >= fn.blocks.size() || fn.blocks[b].successors.empty()) return std::nullopt;
        auto it = fn.block_index.find(fn.blocks[b].successors[0]);
        if (it == fn.block_index.end()) return std::nullopt;
        return it->second;
    };

    // Bug A: track every block index that has been assigned to a region
    // (if/else children, loop body). The main walk skips any block already
    // in `assigned` so it is never rendered twice.
    std::unordered_set<std::size_t> assigned;

    // Walk blocks in order, building regions.
    for (std::size_t i = 0; i < fn.blocks.size(); ++i) {
        if (assigned.count(i)) continue;

        // Check if this block is a loop header.
        auto lhit = loop_by_header.find(i);
        if (lhit != loop_by_header.end()) {
            const auto& loop = *lhit->second;
            auto while_region = std::make_unique<Region>();
            // Determine if it's While or DoWhile.
            // If the header's first instruction is a BranchCond that exits
            // the loop, it's a While. If the BranchCond is at the latch,
            // it's a DoWhile.
            const auto& hdr = fn.blocks[loop.header];
            if (!hdr.instructions.empty()) {
                const auto& first = hdr.instructions.front();
                if (first.op == OpCode::BranchCond) {
                    while_region->kind = Region::Kind::While;
                    if (first.operands.size() >= 1 && first.operands[0].kind == Operand::Kind::Register) {
                        while_region->condition = first.operands[0].vreg;
                    }
                } else {
                    while_region->kind = Region::Kind::DoWhile;
                    const auto& latch = fn.blocks[loop.latch];
                    if (!latch.instructions.empty()) {
                        const auto& last = latch.instructions.back();
                        if (last.op == OpCode::BranchCond && last.operands.size() >= 1
                            && last.operands[0].kind == Operand::Kind::Register) {
                            while_region->condition = last.operands[0].vreg;
                        }
                    }
                }
            } else {
                while_region->kind = Region::Kind::While;
            }
            // Add the loop body blocks as children.
            for (auto bi : loop.body) {
                auto child = std::make_unique<Region>();
                child->kind = Region::Kind::Block;
                child->block_idx = bi;
                while_region->children.push_back(std::move(child));
                assigned.insert(bi);
            }
            root->children.push_back(std::move(while_region));
            // Loop body blocks are now tracked in `assigned`; the main walk
            // will skip them via the `assigned.count(i)` check at the top.
            continue;
        }

        // Check for if/else pattern: block ends with BranchCond and
        // has 2 successors that converge.
        const auto& b = fn.blocks[i];
        bool made_if_region = false;
        if (!b.instructions.empty()) {
            const auto& last = b.instructions.back();
            if (last.op == OpCode::BranchCond && b.successors.size() >= 2) {
                auto s1 = succ_idx(i);
                auto s2 = std::optional<std::size_t>{};
                if (b.successors.size() >= 2) {
                    auto it = fn.block_index.find(b.successors[1]);
                    if (it != fn.block_index.end()) s2 = it->second;
                }
                // Skip degenerate if/else patterns so we don't accidentally
                // claim blocks that should be structured as loops:
                //  - both successors resolve to the same block (BranchCond
                //    target equals the fall-through);
                //  - a successor is itself a loop header (the loop should
                //    be its own While/DoWhile region, not an if/else child).
                const bool same_target = s1.has_value() && s2.has_value() && *s1 == *s2;
                const bool s1_is_loop = s1.has_value() && loop_by_header.count(*s1) != 0;
                const bool s2_is_loop = s2.has_value() && loop_by_header.count(*s2) != 0;
                if (!same_target && !s1_is_loop && !s2_is_loop) {
                    auto if_region = std::make_unique<Region>();
                    // Determine if it's IfThen or IfThenElse.
                    bool is_if_then_else = false;
                    if (s1.has_value() && s2.has_value()) {
                        // Check if s1 has a successor that equals s2's successor
                        // (diamond pattern -> IfThenElse).
                        auto s1_succ = succ_idx(*s1);
                        auto s2_succ = succ_idx(*s2);
                        if (s1_succ.has_value() && s2_succ.has_value() && *s1_succ == *s2_succ) {
                            is_if_then_else = true;
                        } else if (s1_succ.has_value()) {
                            // Else-if chain head detection: when the else block
                            // (s2) is itself a BranchCond whose two successors
                            // converge at the same point as s1's successor,
                            // treat the outer as an IfThenElse. The nested
                            // chain is built later by build_else_chain.
                            const auto& s2_blk = fn.blocks[*s2];
                            if (!s2_blk.instructions.empty()
                                && s2_blk.instructions.back().op == OpCode::BranchCond
                                && s2_blk.successors.size() >= 2) {
                                auto es1 = succ_idx(*s2);
                                std::optional<std::size_t> es2;
                                if (s2_blk.successors.size() >= 2) {
                                    auto it = fn.block_index.find(s2_blk.successors[1]);
                                    if (it != fn.block_index.end()) es2 = it->second;
                                }
                                if (es1.has_value() && es2.has_value()) {
                                    auto es1_succ = succ_idx(*es1);
                                    auto es2_succ = succ_idx(*es2);
                                    if (es1_succ.has_value() && es2_succ.has_value()
                                        && *es1_succ == *es2_succ
                                        && *es1_succ == *s1_succ) {
                                        is_if_then_else = true;
                                    }
                                }
                            }
                        }
                    }
                    if_region->kind = is_if_then_else
                        ? Region::Kind::IfThenElse
                        : Region::Kind::IfThen;
                    if (last.operands.size() >= 1 && last.operands[0].kind == Operand::Kind::Register) {
                        if_region->condition = last.operands[0].vreg;
                    }
                    // The block itself is the condition block.
                    auto cond_block = std::make_unique<Region>();
                    cond_block->kind = Region::Kind::Block;
                    cond_block->block_idx = i;
                    if_region->children.push_back(std::move(cond_block));
                    assigned.insert(i);
                    // Add then-block.
                    if (s1.has_value()) {
                        auto then_r = std::make_unique<Region>();
                        then_r->kind = Region::Kind::Block;
                        then_r->block_idx = *s1;
                        if_region->children.push_back(std::move(then_r));
                        assigned.insert(*s1);
                    }
                    // Add else-block if IfThenElse.
                    if (if_region->kind == Region::Kind::IfThenElse && s2.has_value()) {
                        // Else-if chain detection: if the else block (s2)
                        // itself ends with a BranchCond whose successors form
                        // a diamond, nest it as another IfThenElse rather
                        // than a plain Block. Walk recursively so chains of
                        // arbitrary depth collapse into nested IfThenElse
                        // regions (rendered as `} else if (...) { ... }`).
                        std::function<std::unique_ptr<Region>(std::size_t)> build_else_chain =
                            [&](std::size_t else_idx) -> std::unique_ptr<Region> {
                            if (else_idx >= fn.blocks.size()) return nullptr;
                            const auto& eb = fn.blocks[else_idx];
                            if (eb.instructions.empty()) return nullptr;
                            const auto& elast = eb.instructions.back();
                            if (elast.op != OpCode::BranchCond || eb.successors.size() < 2) return nullptr;

                            auto es1 = succ_idx(else_idx);
                            std::optional<std::size_t> es2;
                            if (eb.successors.size() >= 2) {
                                auto it = fn.block_index.find(eb.successors[1]);
                                if (it != fn.block_index.end()) es2 = it->second;
                            }
                            if (!es1.has_value() || !es2.has_value()) return nullptr;

                            // Diamond check: both successors converge.
                            auto es1_succ = succ_idx(*es1);
                            auto es2_succ = succ_idx(*es2);
                            if (!es1_succ.has_value() || !es2_succ.has_value()
                                || *es1_succ != *es2_succ) return nullptr;

                            // Same degenerate-case guards as the outer if/else.
                            const bool chain_same_target = (*es1 == *es2);
                            const bool chain_es1_loop = loop_by_header.count(*es1) != 0;
                            const bool chain_es2_loop = loop_by_header.count(*es2) != 0;
                            if (chain_same_target || chain_es1_loop || chain_es2_loop) return nullptr;

                            auto nested = std::make_unique<Region>();
                            nested->kind = Region::Kind::IfThenElse;
                            if (elast.operands.size() >= 1
                                && elast.operands[0].kind == Operand::Kind::Register) {
                                nested->condition = elast.operands[0].vreg;
                            }
                            // children[0]: condition block (the else block itself).
                            auto chain_cond_block = std::make_unique<Region>();
                            chain_cond_block->kind = Region::Kind::Block;
                            chain_cond_block->block_idx = else_idx;
                            nested->children.push_back(std::move(chain_cond_block));
                            assigned.insert(else_idx);
                            // children[1]: then-block.
                            auto then_r = std::make_unique<Region>();
                            then_r->kind = Region::Kind::Block;
                            then_r->block_idx = *es1;
                            nested->children.push_back(std::move(then_r));
                            assigned.insert(*es1);
                            // children[2]: else-block - recurse to handle
                            // deeper chains, otherwise plain Block.
                            auto inner = build_else_chain(*es2);
                            if (inner) {
                                nested->children.push_back(std::move(inner));
                            } else {
                                auto else_r = std::make_unique<Region>();
                                else_r->kind = Region::Kind::Block;
                                else_r->block_idx = *es2;
                                nested->children.push_back(std::move(else_r));
                                assigned.insert(*es2);
                            }
                            return nested;
                        };

                        auto nested_else = build_else_chain(*s2);
                        if (nested_else) {
                            if_region->children.push_back(std::move(nested_else));
                        } else {
                            auto else_r = std::make_unique<Region>();
                            else_r->kind = Region::Kind::Block;
                            else_r->block_idx = *s2;
                            if_region->children.push_back(std::move(else_r));
                            assigned.insert(*s2);
                        }
                    }
                    root->children.push_back(std::move(if_region));
                    made_if_region = true;
                }
            }
        }

        if (!made_if_region) {
            // Default: simple block.
            auto block_r = std::make_unique<Region>();
            block_r->kind = Region::Kind::Block;
            block_r->block_idx = i;
            root->children.push_back(std::move(block_r));
        }
    }

    // Final cleanup: remove any Block children that were assigned to a region.
    root->children.erase(
        std::remove_if(root->children.begin(), root->children.end(),
            [&](const std::unique_ptr<Region>& r) {
                return r->kind == Region::Kind::Block && assigned.count(r->block_idx);
            }),
        root->children.end());

    return root;
}

// ---------------------------------------------------------------------------
// render_structured
// ---------------------------------------------------------------------------
namespace {

/// Bug D: loop context propagated into render_region so that unconditional
/// Branch instructions inside a loop body can be rendered as `continue;`
/// (when the target is the loop header) or `break;` (when the target is
/// the block immediately after the loop). `post_loop_addr == 0` means
/// unknown / no exit.
struct LoopContext {
    std::uint64_t header_addr = 0;
    std::uint64_t post_loop_addr = 0;
};

/// Bug B: returns the C source for a loop/if condition. When the condition
/// vreg was never set (or is the sentinel INVALID_VREG), we emit `1` so
/// infinite loops render as `while (1)` instead of `while (v4294967295)`.
std::string render_condition(const std::optional<VReg>& cond) {
    if (!cond.has_value() || *cond == INVALID_VREG) return "1";
    return render_operand(Operand::reg(*cond));
}

void render_region(std::ostringstream& os, const Function& fn, const Region& r,
                   int indent, const LoopContext* lc = nullptr) {
    auto pad = [&os, indent]() {
        for (int i = 0; i < indent; ++i) os << "  ";
    };

    switch (r.kind) {
        case Region::Kind::Block: {
            if (r.block_idx >= fn.blocks.size()) break;
            const auto& b = fn.blocks[r.block_idx];
            pad(); os << "L_" << std::hex << b.start_addr << std::dec << ":\n";
            for (std::size_t k = 0; k < b.instructions.size(); ++k) {
                const auto& ins = b.instructions[k];
                const bool is_last = (k + 1 == b.instructions.size());
                // Bug D: an unconditional Branch (goto) whose target is the
                // enclosing loop's header becomes `continue;`, and one whose
                // target is the block right after the loop becomes `break;`.
                if (is_last && lc != nullptr
                    && ins.op == OpCode::Branch && !ins.operands.empty()
                    && ins.operands[0].kind == Operand::Kind::Label) {
                    const auto target = ins.operands[0].label_addr;
                    if (target == lc->header_addr) {
                        pad(); os << "  continue;\n";
                        continue;
                    }
                    if (lc->post_loop_addr != 0 && target == lc->post_loop_addr) {
                        pad(); os << "  break;\n";
                        continue;
                    }
                }
                // Also handle BranchCond that targets loop header (-> continue) or post-loop (-> break)
                if (lc && is_last && ins.op == OpCode::BranchCond && ins.operands.size() >= 2
                    && ins.operands[1].kind == Operand::Kind::Label) {
                    const auto target = ins.operands[1].label_addr;
                    if (target == lc->header_addr) {
                        os << "    if (" << render_operand(ins.operands[0]) << ") continue;\n";
                        continue;
                    }
                    if (lc->post_loop_addr != 0 && target == lc->post_loop_addr) {
                        os << "    if (!(" << render_operand(ins.operands[0]) << ")) break;\n";
                        continue;
                    }
                }
                pad(); os << "  " << render_instruction(ins) << "\n";
            }
            break;
        }
        case Region::Kind::IfThen: {
            if (!r.children.empty()) {
                pad();
                os << "if (" << render_condition(r.condition) << ") {\n";
                for (const auto& c : r.children) {
                    if (c->kind == Region::Kind::Block && !c->children.empty()) {
                        // Skip the condition block (first child) in the body.
                        continue;
                    }
                    render_region(os, fn, *c, indent + 1, lc);
                }
                pad(); os << "}\n";
            }
            break;
        }
        case Region::Kind::IfThenElse: {
            if (r.children.size() >= 3) {
                pad();
                os << "if (" << render_condition(r.condition) << ") {\n";
                render_region(os, fn, *r.children[1], indent + 1, lc);
                pad(); os << "} else {\n";
                render_region(os, fn, *r.children[2], indent + 1, lc);
                pad(); os << "}\n";
            }
            break;
        }
        case Region::Kind::While: {
            // Build the loop context so body Blocks can translate gotos.
            LoopContext loop_lc;
            if (!r.children.empty()) {
                loop_lc.header_addr = fn.blocks[r.children.front()->block_idx].start_addr;
                const auto& latch = fn.blocks[r.children.back()->block_idx];
                for (auto succ : latch.successors) {
                    if (succ != loop_lc.header_addr) {
                        loop_lc.post_loop_addr = succ;
                        break;
                    }
                }
            }
            pad();
            os << "while (" << render_condition(r.condition) << ") {\n";
            for (std::size_t ci = 0; ci < r.children.size(); ++ci) {
                if (ci == 0) continue;  // skip header block
                render_region(os, fn, *r.children[ci], indent + 1, &loop_lc);
            }
            pad(); os << "}\n";
            break;
        }
        case Region::Kind::DoWhile: {
            LoopContext loop_lc;
            if (!r.children.empty()) {
                loop_lc.header_addr = fn.blocks[r.children.front()->block_idx].start_addr;
                const auto& latch = fn.blocks[r.children.back()->block_idx];
                for (auto succ : latch.successors) {
                    if (succ != loop_lc.header_addr) {
                        loop_lc.post_loop_addr = succ;
                        break;
                    }
                }
            }
            pad(); os << "do {\n";
            for (const auto& c : r.children) {
                render_region(os, fn, *c, indent + 1, &loop_lc);
            }
            pad();
            os << "} while (" << render_condition(r.condition) << ");\n";
            break;
        }
        case Region::Kind::Switch:
        case Region::Kind::Sequence: {
            for (const auto& c : r.children) {
                render_region(os, fn, *c, indent, lc);
            }
            break;
        }
        case Region::Kind::Goto: {
            pad(); os << "goto L_" << std::hex << r.goto_target << std::dec << ";\n";
            break;
        }
    }
}

}  // namespace

std::string render_structured(const Function& fn, const Region& root) {
    std::ostringstream os;
    os << "// Function: " << (fn.name.empty() ? "sub" : fn.name) << "\n";
    os << "// Entry: 0x" << std::hex << fn.entry << std::dec << "\n";
    os << "void " << (fn.name.empty() ? "sub" : fn.name) << "(void) {\n";
    render_region(os, fn, root, 1);
    os << "}\n";
    return os.str();
}

}  // namespace nyx::ir
