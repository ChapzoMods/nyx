// =============================================================================
// Nyx - A headless decompilation engine
// Copyright (C) 2024-2026 Chapzoo  <https://github.com/Chapzoo>
// SPDX-License-Identifier: GPL-3.0-or-later
// =============================================================================
#pragma once

#include "nyx/lifter/ir.hpp"

#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace nyx::ir {

/// A basic block: contiguous sequence of IR instructions with a single
/// entry point (the block's start address) and zero or more exit edges.
struct BasicBlock {
    std::uint64_t            start_addr = 0;
    std::vector<Instruction> instructions;
    std::vector<std::uint64_t> successors;        // addresses; empty => falls off the end

    [[nodiscard]] bool empty() const noexcept { return instructions.empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return instructions.size(); }
    [[nodiscard]] std::uint64_t last_addr() const noexcept {
        return instructions.empty() ? start_addr : instructions.back().addr;
    }
};

/// Control-flow graph for a single function.
struct Function {
    std::string              name;
    std::uint64_t            entry = 0;
    std::vector<BasicBlock>  blocks;
    std::unordered_map<std::uint64_t, std::size_t> block_index;  // start_addr -> index in blocks

    /// v0.0.4: inferred type per virtual register. Populated by TypeInferer
    /// after the function is built. VRegs not present default to Type::Unknown.
    std::unordered_map<VReg, Type> vreg_types;

    /// Finds or creates a block starting at `addr`. Returns its index.
    std::size_t ensure_block(std::uint64_t addr);

    /// Computes successors for every block based on terminator opcodes.
    /// Call this once after the function is fully populated.
    void link_successors();

    [[nodiscard]] std::size_t instruction_count() const noexcept;
    [[nodiscard]] std::size_t block_count()       const noexcept { return blocks.size(); }
};

/// Builds a Function from a flat list of IR instructions by splitting on
/// terminators (branch / branch_cond / return) and on branch targets.
class CFGBuilder {
public:
    /// Splits `insns` into basic blocks. The function's `entry` is taken
    /// from the first instruction's address (or `forced_entry` if given).
    [[nodiscard]] static Function build(std::vector<Instruction> insns,
                                        std::uint64_t forced_entry = 0,
                                        std::string name = {});
};

}  // namespace nyx::ir
