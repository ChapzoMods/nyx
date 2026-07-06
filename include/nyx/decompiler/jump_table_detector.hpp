// =============================================================================
// Nyx - A headless decompilation engine
// Copyright (C) 2024-2026 Chapzoo  <https://github.com/Chapzoo>
// SPDX-License-Identifier: GPL-3.0-or-later
// =============================================================================
#pragma once

#include "nyx/core/bytes.hpp"
#include "nyx/core/types.hpp"
#include "nyx/lifter/cfg.hpp"
#include "nyx/parsers/disassembler.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace nyx {

/// Result of jump-table detection for a single function.
struct JumpTable {
    /// Address of the indirect branch that uses this table.
    std::uint64_t branch_addr = 0;
    /// Virtual address of the table itself (if known).
    std::uint64_t table_addr = 0;
    /// Number of entries detected (approximate).
    std::size_t entry_count = 0;
    /// Entry size in bytes (typically 4 for 32-bit, 8 for 64-bit).
    std::uint8_t entry_size = 4;
    /// List of resolved target addresses (empty if we couldn't read them).
    std::vector<std::uint64_t> targets;
};

/// Detects compiler-generated jump tables in a function's disassembly.
///
/// v0.0.5 heuristics (conservative):
///   1. Look for an instruction sequence ending in an indirect jump:
///        x86:    jmp qword ptr [reg + reg*scale]  /  jmp [reg*4 + base]
///        ARM64:  br xN  preceded by  ldr xN, [xM, xN, lsl #3]
///        MIPS:   jr $tN  preceded by  lw $tN, offset($tN)
///   2. Walk backwards a few instructions looking for the table base and
///      an index register; if we find a load from a read-only section,
///      treat it as a jump table.
///   3. Try to read the table entries from the binary and resolve them
///      to absolute addresses.
///
/// The detector returns one JumpTable per indirect branch it identifies.
/// Callers can use the target list to build a `switch` in pseudo-C.
class JumpTableDetector {
public:
    JumpTableDetector(Arch arch, Endian endian, const BinaryInfo* bin = nullptr);

    /// Scans `insns` for jump-table patterns. Returns one entry per
    /// detected table.
    [[nodiscard]] std::vector<JumpTable> detect(
        const std::vector<DecodedInstruction>& insns,
        ByteView file_bytes = {}) const;

private:
    Arch arch_;
    Endian endian_;
    const BinaryInfo* bin_;

    [[nodiscard]] std::optional<JumpTable> detect_x86(
        const std::vector<DecodedInstruction>& insns,
        std::size_t idx, ByteView file_bytes) const;
    [[nodiscard]] std::optional<JumpTable> detect_arm64(
        const std::vector<DecodedInstruction>& insns,
        std::size_t idx, ByteView file_bytes) const;
};

}  // namespace nyx
