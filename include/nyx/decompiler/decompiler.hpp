// =============================================================================
// Nyx - A headless decompilation engine
// Copyright (C) 2024-2026 Chapzoo  <https://github.com/Chapzoo>
// SPDX-License-Identifier: GPL-3.0-or-later
// =============================================================================
#pragma once

#include "nyx/core/types.hpp"
#include "nyx/lifter/cfg.hpp"
#include "nyx/lifter/instruction_lifter.hpp"
#include "nyx/parsers/disassembler.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace nyx {

/// Decompiled function: the textual pseudo-C body plus metadata useful
/// for the various output writers.
struct DecompiledFunction {
    std::string              name;
    std::uint64_t            entry = 0;
    std::vector<std::string> lines;     // pseudo-C source lines, indented 4 spaces
    std::size_t              block_count = 0;
    std::size_t              insn_count   = 0;
};

/// Top-level decompilation pipeline: takes a BinaryInfo and produces one
/// DecompiledFunction per detected function symbol (or, when stripped,
/// one function per executable section using a linear-sweep heuristic).
class Decompiler {
public:
    struct Options {
        /// When true, also emit functions discovered via linear sweep even
        /// if no symbol exists for them.
        bool linear_sweep_fallback = true;
        /// Maximum number of instructions to decode per function (safety cap).
        std::size_t max_insns_per_function = 5000;
    };

    Decompiler();
    explicit Decompiler(Options opts);

    /// Decompile every code section of `bin`.
    [[nodiscard]] std::vector<DecompiledFunction> decompile(const BinaryInfo& bin) const;

    /// Decompile a single address range. Exposed for tests.
    [[nodiscard]] DecompiledFunction decompile_range(
        const BinaryInfo& bin,
        std::uint64_t start_addr,
        ByteView bytes,
        std::string name = "sub_<hex>") const;

private:
    Options opts_;
};

}  // namespace nyx
