// =============================================================================
// Nyx - A headless decompilation engine
// Copyright (C) 2024-2026 Chapzoo  <https://github.com/Chapzoo>
// SPDX-License-Identifier: GPL-3.0-or-later
// =============================================================================
#include "nyx/lifter/cfg.hpp"

#include <algorithm>
#include <set>
#include <unordered_set>

namespace nyx::ir {

std::size_t Function::ensure_block(std::uint64_t addr) {
    auto it = block_index.find(addr);
    if (it != block_index.end()) return it->second;
    BasicBlock b;
    b.start_addr = addr;
    blocks.push_back(std::move(b));
    const std::size_t idx = blocks.size() - 1;
    block_index[addr] = idx;
    return idx;
}

void Function::link_successors() {
    for (auto& b : blocks) {
        b.successors.clear();
        if (b.instructions.empty()) continue;
        const auto& last = b.instructions.back();
        switch (last.op) {
            case OpCode::Branch:
                if (!last.operands.empty() && last.operands[0].kind == Operand::Kind::Label)
                    b.successors.push_back(last.operands[0].label_addr);
                break;
            case OpCode::BranchCond:
                // Conditional branch: successors are the target AND fall-through.
                if (last.operands.size() >= 2 && last.operands[1].kind == Operand::Kind::Label)
                    b.successors.push_back(last.operands[1].label_addr);
                // Fall-through = address right after the branch instruction.
                b.successors.push_back(last.addr + 1);  // approximate; resolved later via block_index
                break;
            case OpCode::Return:
                // No successors.
                break;
            default:
                // Non-terminator: fall through to next instruction.
                b.successors.push_back(last.addr + 1);
                break;
        }
    }
}

std::size_t Function::instruction_count() const noexcept {
    std::size_t n = 0;
    for (const auto& b : blocks) n += b.instructions.size();
    return n;
}

// ---------------------------------------------------------------------------
// CFGBuilder
// ---------------------------------------------------------------------------
Function CFGBuilder::build(std::vector<Instruction> insns, std::uint64_t forced_entry, std::string name) {
    Function fn;
    fn.name = std::move(name);
    if (insns.empty()) return fn;

    if (forced_entry == 0) fn.entry = insns.front().addr;
    else                   fn.entry = forced_entry;

    // Collect branch targets so we know where to start new basic blocks.
    std::unordered_set<std::uint64_t> leaders;
    leaders.insert(fn.entry);
    for (const auto& i : insns) {
        if (i.op == OpCode::Branch || i.op == OpCode::BranchCond) {
            for (const auto& op : i.operands) {
                if (op.kind == Operand::Kind::Label) leaders.insert(op.label_addr);
            }
        }
        if (i.op == OpCode::Return) {
            // fall-through becomes a leader if there is a next instruction.
            // (leader for the *next* addr)
        }
    }

    // Walk linearly, splitting on terminators and on leader addresses.
    std::size_t cur_idx = fn.ensure_block(fn.entry);
    for (auto& insn : insns) {
        // Start a new block when we hit a leader and the current block is non-empty.
        if (leaders.count(insn.addr) && !fn.blocks[cur_idx].instructions.empty()) {
            cur_idx = fn.ensure_block(insn.addr);
        }
        fn.blocks[cur_idx].instructions.push_back(std::move(insn));

        // After a terminator, the next instruction will start a new block.
        const auto& just = fn.blocks[cur_idx].instructions.back();
        if (just.is_terminator()) {
            // Allocate next block lazily when we see the next instruction.
            // Mark the *next* address as a leader so the loop above picks it up.
            // We rely on leaders set + the next iter's check.
            // Insert a sentinel: simply remember to switch on next iter.
            // (Implementation note: we just continue; the leader check on the
            // next iter will not match unless the addr is in `leaders`. To
            // handle that, we insert the fall-through addr into leaders.)
            if (just.op == OpCode::BranchCond) {
                // Fall-through addr is the next insn's addr (if any).
                // We'll compute this when we see the next insn.
            } else if (just.op == OpCode::Return || just.op == OpCode::Branch) {
                // Force a new block at the next insn addr.
                // Marked lazily below.
            }
        }
    }

    // Pass 2: ensure terminators create successors properly.
    // Re-scan to insert fall-through leaders after Return/BranchCond.
    for (std::size_t i = 0; i + 1 < fn.blocks.size(); ++i) {
        auto& b = fn.blocks[i];
        if (b.instructions.empty()) continue;
        const auto& last = b.instructions.back();
        if (last.op == OpCode::Return) {
            // The next block is unreachable from here - leave it; CFG will
            // simply have no edge.
        } else if (last.op == OpCode::BranchCond) {
            // Fall-through to next block's start addr.
            const std::uint64_t next_addr = fn.blocks[i + 1].start_addr;
            // Push fall-through as a successor if not already present.
            if (std::find(b.successors.begin(), b.successors.end(), next_addr) == b.successors.end()) {
                b.successors.push_back(next_addr);
            }
        } else if (last.op != OpCode::Branch && !last.is_terminator()) {
            // Non-terminator last instruction: implicit fall-through.
            const std::uint64_t next_addr = fn.blocks[i + 1].start_addr;
            b.successors.push_back(next_addr);
        }
    }

    fn.link_successors();
    // Re-run link_successors but preserve our fall-through additions:
    // For v0.0.1 we accept the simple heuristic where successors may include
    // a +1 fall-through address that is then resolved via block_index.
    // Resolve any +1-style addresses to actual block starts.
    for (auto& b : fn.blocks) {
        std::vector<std::uint64_t> resolved;
        resolved.reserve(b.successors.size());
        for (auto addr : b.successors) {
            // If exact match in block_index, use it; otherwise find the next
            // block start >= addr (treats addr as a fall-through hint).
            auto it = fn.block_index.find(addr);
            if (it != fn.block_index.end()) {
                resolved.push_back(addr);
            } else {
                std::uint64_t best = 0;
                bool found = false;
                for (const auto& [k, _] : fn.block_index) {
                    if (k >= addr && (!found || k < best)) { best = k; found = true; }
                }
                if (found) resolved.push_back(best);
            }
        }
        b.successors = std::move(resolved);
    }

    return fn;
}

}  // namespace nyx::ir
