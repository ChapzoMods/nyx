// =============================================================================
// Nyx - A headless decompilation engine
// Copyright (C) 2024-2026 Chapzoo  <https://github.com/Chapzoo>
// SPDX-License-Identifier: GPL-3.0-or-later
// =============================================================================
#include "nyx/lifter/region_builder.hpp"

#include "nyx/core/bytes.hpp"
#include "nyx/decompiler/pseudo_c.hpp"
#include "nyx/core/logger.hpp"

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

    // Walk blocks in order, building regions.
    for (std::size_t i = 0; i < fn.blocks.size(); ++i) {
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
            }
            root->children.push_back(std::move(while_region));
            // Skip the loop body blocks (they're already in the While region).
            // We can't easily skip non-contiguous blocks, so we just continue
            // and let non-header blocks that were in the loop be skipped by
            // checking membership.
            continue;
        }

        // Check if this block is in a loop body (already handled).
        bool in_loop = false;
        for (const auto& l : loops) {
            for (auto bi : l.body) {
                if (bi == i && l.header != i) { in_loop = true; break; }
            }
            if (in_loop) break;
        }
        if (in_loop) continue;

        // Check for if/else pattern: block ends with BranchCond and
        // has 2 successors that converge.
        const auto& b = fn.blocks[i];
        if (!b.instructions.empty()) {
            const auto& last = b.instructions.back();
            if (last.op == OpCode::BranchCond && b.successors.size() >= 2) {
                auto if_region = std::make_unique<Region>();
                auto s1 = succ_idx(i);
                auto s2 = std::optional<std::size_t>{};
                if (b.successors.size() >= 2) {
                    auto it = fn.block_index.find(b.successors[1]);
                    if (it != fn.block_index.end()) s2 = it->second;
                }
                // Determine if it's IfThen or IfThenElse.
                if (s1.has_value() && s2.has_value()) {
                    // Check if s1 has a successor that equals s2's successor
                    // (diamond pattern -> IfThenElse).
                    auto s1_succ = succ_idx(*s1);
                    auto s2_succ = succ_idx(*s2);
                    if (s1_succ.has_value() && s2_succ.has_value() && *s1_succ == *s2_succ) {
                        if_region->kind = Region::Kind::IfThenElse;
                    } else {
                        if_region->kind = Region::Kind::IfThen;
                    }
                } else {
                    if_region->kind = Region::Kind::IfThen;
                }
                if (last.operands.size() >= 1 && last.operands[0].kind == Operand::Kind::Register) {
                    if_region->condition = last.operands[0].vreg;
                }
                // The block itself is the condition block.
                auto cond_block = std::make_unique<Region>();
                cond_block->kind = Region::Kind::Block;
                cond_block->block_idx = i;
                if_region->children.push_back(std::move(cond_block));
                // Add then-block.
                if (s1.has_value()) {
                    auto then_r = std::make_unique<Region>();
                    then_r->kind = Region::Kind::Block;
                    then_r->block_idx = *s1;
                    if_region->children.push_back(std::move(then_r));
                }
                // Add else-block if IfThenElse.
                if (if_region->kind == Region::Kind::IfThenElse && s2.has_value()) {
                    auto else_r = std::make_unique<Region>();
                    else_r->kind = Region::Kind::Block;
                    else_r->block_idx = *s2;
                    if_region->children.push_back(std::move(else_r));
                }
                root->children.push_back(std::move(if_region));
                continue;
            }
        }

        // Default: simple block.
        auto block_r = std::make_unique<Region>();
        block_r->kind = Region::Kind::Block;
        block_r->block_idx = i;
        root->children.push_back(std::move(block_r));
    }

    return root;
}

// ---------------------------------------------------------------------------
// render_structured
// ---------------------------------------------------------------------------
namespace {

void render_region(std::ostringstream& os, const Function& fn, const Region& r, int indent) {
    auto pad = [&os, indent]() {
        for (int i = 0; i < indent; ++i) os << "  ";
    };

    switch (r.kind) {
        case Region::Kind::Block: {
            if (r.block_idx >= fn.blocks.size()) break;
            const auto& b = fn.blocks[r.block_idx];
            pad(); os << "L_" << std::hex << b.start_addr << std::dec << ":\n";
            for (const auto& ins : b.instructions) {
                pad(); os << "  " << render_instruction(ins) << "\n";
            }
            break;
        }
        case Region::Kind::IfThen: {
            if (!r.children.empty()) {
                pad();
                os << "if (" << render_operand(Operand::reg(r.condition.value_or(INVALID_VREG)))
                   << ") {\n";
                for (const auto& c : r.children) {
                    if (c->kind == Region::Kind::Block && !c->children.empty()) {
                        // Skip the condition block (first child) in the body.
                        continue;
                    }
                    render_region(os, fn, *c, indent + 1);
                }
                pad(); os << "}\n";
            }
            break;
        }
        case Region::Kind::IfThenElse: {
            if (r.children.size() >= 3) {
                pad();
                os << "if (" << render_operand(Operand::reg(r.condition.value_or(INVALID_VREG)))
                   << ") {\n";
                render_region(os, fn, *r.children[1], indent + 1);
                pad(); os << "} else {\n";
                render_region(os, fn, *r.children[2], indent + 1);
                pad(); os << "}\n";
            }
            break;
        }
        case Region::Kind::While: {
            pad();
            os << "while (" << render_operand(Operand::reg(r.condition.value_or(INVALID_VREG)))
               << ") {\n";
            for (std::size_t ci = 0; ci < r.children.size(); ++ci) {
                if (ci == 0) continue;  // skip header block
                render_region(os, fn, *r.children[ci], indent + 1);
            }
            pad(); os << "}\n";
            break;
        }
        case Region::Kind::DoWhile: {
            pad(); os << "do {\n";
            for (const auto& c : r.children) {
                render_region(os, fn, *c, indent + 1);
            }
            pad();
            os << "} while (" << render_operand(Operand::reg(r.condition.value_or(INVALID_VREG)))
               << ");\n";
            break;
        }
        case Region::Kind::Switch:
        case Region::Kind::Sequence: {
            for (const auto& c : r.children) {
                render_region(os, fn, *c, indent);
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
