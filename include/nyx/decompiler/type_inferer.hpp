// =============================================================================
// Nyx - A headless decompilation engine
// Copyright (C) 2024-2026 Chapzoo  <https://github.com/Chapzoo>
// SPDX-License-Identifier: GPL-3.0-or-later
// =============================================================================
#pragma once

#include "nyx/core/arch.hpp"
#include "nyx/core/types.hpp"
#include "nyx/lifter/cfg.hpp"
#include "nyx/lifter/ir.hpp"

#include <cstdint>

namespace nyx {

/// Conservative type inference for IR virtual registers.
///
/// v0.0.4 strategy:
///   1. Walk the function's IR instructions. Each instruction that
///      produces a value (Mov, Load, BinOp, Cmp, Pop) gets its destination
///      vreg typed based on:
///        - The source operand: if it's an immediate, infer Int32 (or
///          Int64 if the value needs more than 32 bits).
///        - The memory operand size hint: a Load/Store with mem_size 1/2/
///          4/8 infers Int8/Int16/Int32/Int64.
///        - The symbol table: if a Mov/Load reads from a symbol whose
///          BinaryInfo::Symbol::size is known, infer from that size.
///   2. Function symbols (kind == Function) propagate Type::Func.
///   3. Pointers: a Load whose source memory operand comes from a code
///      section infers Ptr; a LEA-style mov infers Ptr.
///
/// The inferer is deliberately conservative: when in doubt, it leaves the
/// vreg as Type::Unknown (which renders as `void*` in pseudo-C). This
/// avoids asserting wrong types that would mislead the reader.
class TypeInferer {
public:
    TypeInferer(Arch arch, const BinaryInfo* bin = nullptr);

    /// Populates `fn.vreg_types` for every vreg that appears as a
    /// destination in the function. Idempotent: calling twice is safe.
    void infer(ir::Function& fn) const;

    /// v0.0.6: returns the C type name for a function's return type,
    /// resolved from DWARF if available. Returns "void" when unknown.
    [[nodiscard]] std::string function_return_type(const std::string& fn_name) const;

    /// Returns the inferred type for a vreg, or Type::Unknown if not found.
    [[nodiscard]] ir::Type type_of(const ir::Function& fn, ir::VReg v) const noexcept;

    /// Maps a byte size to a primitive IR type.
    [[nodiscard]] static ir::Type type_from_size(std::uint64_t size) noexcept;

private:
    Arch arch_;
    const BinaryInfo* bin_;

    [[nodiscard]] ir::Type type_from_symbol(std::uint64_t addr) const noexcept;
    [[nodiscard]] ir::Type type_from_imm(std::int64_t v) const noexcept;
    /// v0.0.6: resolves a DWARF type to an IR Type. Returns Unknown if
    /// no DWARF or the type can't be mapped.
    [[nodiscard]] ir::Type type_from_dwarf(std::uint64_t type_offset) const noexcept;
};

}  // namespace nyx
