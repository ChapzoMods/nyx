// =============================================================================
// Nyx - A headless decompilation engine
// Copyright (C) 2024-2026 Chapzoo  <https://github.com/Chapzoo>
// SPDX-License-Identifier: GPL-3.0-or-later
// =============================================================================
#pragma once

#include "nyx/core/arch.hpp"
#include "nyx/core/bytes.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace nyx::ir {

/// Opaque virtual register id. The lifter hands out fresh ids per function;
/// real machine registers are mapped to vregs at lift time.
using VReg = std::uint32_t;
constexpr VReg INVALID_VREG = 0xFFFFFFFFu;

/// Opaque SSA value id - used when an instruction produces a fresh value.
using VId = std::uint32_t;
constexpr VId INVALID_VID = 0xFFFFFFFFu;

/// Inferred primitive type for a virtual register. v0.0.4 introduces a
/// conservative type lattice; the TypeInferer populates this based on
/// symbol sizes and section flags. `Unknown` is the default for any vreg
/// the inferer can't classify.
enum class Type : std::uint8_t {
    Unknown = 0,
    Int8,    // char / signed char / unsigned char (1 byte)
    Int16,   // short (2 bytes)
    Int32,   // int / float (4 bytes)
    Int64,   // long long / double (8 bytes)
    Ptr,     // pointer (size = arch bitness / 8)
    Func,    // function pointer
};

[[nodiscard]] std::string_view type_name(Type t) noexcept;
[[nodiscard]] std::string_view type_c_decl(Type t) noexcept;
[[nodiscard]] std::uint8_t type_size(Type t, std::uint8_t arch_bitness) noexcept;

/// Operand of an IR instruction.
struct Operand {
    enum class Kind : std::uint8_t {
        Register,    // virtual register
        Imm,         // immediate constant
        Mem,         // memory dereference [base + index*scale + disp]
        Label,       // basic-block label
        Symbol,      // external symbol reference
    };
    Kind kind = Kind::Register;

    // Tagged union - keep POD-ish so IR copies stay cheap.
    VReg         vreg       = INVALID_VREG;
    std::int64_t imm_value  = 0;
    VReg         mem_base   = INVALID_VREG;
    VReg         mem_index  = INVALID_VREG;
    std::uint8_t mem_scale  = 1;
    std::int64_t mem_disp   = 0;
    std::uint8_t mem_size   = 0;    // 0 => arch default
    std::uint64_t label_addr = 0;  // target address
    std::string  symbol;            // external symbol name

    [[nodiscard]] static Operand reg(VReg r) {
        Operand o; o.kind = Kind::Register; o.vreg = r; return o;
    }
    [[nodiscard]] static Operand imm(std::int64_t v) {
        Operand o; o.kind = Kind::Imm; o.imm_value = v; return o;
    }
    [[nodiscard]] static Operand mem(VReg base, std::int64_t disp = 0, std::uint8_t size = 0) {
        Operand o; o.kind = Kind::Mem; o.mem_base = base; o.mem_disp = disp; o.mem_size = size; return o;
    }
    [[nodiscard]] static Operand label(std::uint64_t addr) {
        Operand o; o.kind = Kind::Label; o.label_addr = addr; return o;
    }
    [[nodiscard]] static Operand sym(std::string s) {
        Operand o; o.kind = Kind::Symbol; o.symbol = std::move(s); return o;
    }
};

/// Opcode of the IR. Deliberately small - we lift the common cases and
/// fall back to `Opaque` for anything Capstone decoded but the lifter
/// doesn't model yet. This keeps v0.0.1 robust without lying about
/// completeness.
enum class OpCode : std::uint8_t {
    // Data movement
    Mov,
    Load,           // dst = *src (memory read)
    Store,          // *dst = src (memory write)
    // Arithmetic
    Add, Sub, Mul, Div, Mod,
    And, Or, Xor, Shl, Shr, Sar, Neg, Not,
    // Comparison
    Cmp,
    // Control flow
    Branch,         // unconditional jump to label
    BranchCond,     // conditional jump (operand[0]=cond vreg, operand[1]=label)
    Call,           // call symbol/imm
    Return,
    // Misc
    Push, Pop,
    Nop,
    Opaque,         // raw mnemonic preserved for debugging
};

[[nodiscard]] std::string_view op_name(OpCode op) noexcept;

/// One IR instruction. POD-like; uses std::vector<Operand> for flexibility.
struct Instruction {
    OpCode                op = OpCode::Nop;
    VReg                  dst = INVALID_VREG;  // destination vreg (INVALID_VREG => void)
    std::vector<Operand>  operands;
    std::uint64_t         addr = 0;            // originating machine address
    std::string           raw_mnemonic;        // for Opaque/debug

    /// v0.0.5: true when this is a Branch / Call whose target is a register
    /// or memory location (i.e. an indirect control-flow transfer that the
    /// CFG builder could not resolve statically). The actual target operand
    /// lives in `operands[0]` as a Register or Mem operand.
    bool                  indirect = false;

    [[nodiscard]] bool is_terminator() const noexcept {
        return op == OpCode::Branch || op == OpCode::BranchCond
            || op == OpCode::Return;
    }
};

/// Helper to build IR instructions concisely.
class Builder {
public:
    Builder& mov(VReg dst, Operand src)              { return emit(OpCode::Mov, dst, {std::move(src)}); }
    Builder& load(VReg dst, Operand addr, std::uint8_t sz = 0) {
        auto o = addr; o.mem_size = sz ? sz : addr.mem_size;
        return emit(OpCode::Load, dst, {std::move(o)});
    }
    Builder& store(Operand addr, Operand src, std::uint8_t sz = 0) {
        auto o = addr; o.mem_size = sz ? sz : addr.mem_size;
        return emit(OpCode::Store, INVALID_VREG, {std::move(o), std::move(src)});
    }
    Builder& binop(OpCode op, VReg dst, Operand a, Operand b) { return emit(op, dst, {std::move(a), std::move(b)}); }
    Builder& cmp(VReg dst, Operand a, Operand b)     { return emit(OpCode::Cmp, dst, {std::move(a), std::move(b)}); }
    Builder& branch(std::uint64_t target)            { return emit(OpCode::Branch, INVALID_VREG, {Operand::label(target)}); }
    Builder& branch_cond(Operand cond, std::uint64_t target) { return emit(OpCode::BranchCond, INVALID_VREG, {std::move(cond), Operand::label(target)}); }
    Builder& call(Operand target)                    { return emit(OpCode::Call, INVALID_VREG, {std::move(target)}); }
    /// v0.0.5: indirect branch through a register/memory operand. The
    /// target cannot be resolved statically; the CFG marks this block as
    /// having an indirect terminator.
    Builder& branch_indirect(Operand target) {
        Instruction i; i.op = OpCode::Branch; i.indirect = true;
        i.operands = {std::move(target)};
        ins_.push_back(std::move(i));
        return *this;
    }
    /// v0.0.5: indirect call through a register/memory operand.
    Builder& call_indirect(Operand target) {
        Instruction i; i.op = OpCode::Call; i.indirect = true;
        i.operands = {std::move(target)};
        ins_.push_back(std::move(i));
        return *this;
    }
    Builder& ret()                                   { return emit(OpCode::Return, INVALID_VREG, {}); }
    Builder& nop()                                   { return emit(OpCode::Nop, INVALID_VREG, {}); }
    Builder& push(Operand v)                         { return emit(OpCode::Push, INVALID_VREG, {std::move(v)}); }
    Builder& pop(VReg dst)                           { return emit(OpCode::Pop, dst, {}); }
    Builder& opaque(std::string mnem) {
        Instruction i; i.op = OpCode::Opaque; i.raw_mnemonic = std::move(mnem); ins_.push_back(std::move(i)); return *this;
    }

    [[nodiscard]] std::vector<Instruction> finish() && { return std::move(ins_); }
    [[nodiscard]] const std::vector<Instruction>& view() const noexcept { return ins_; }

private:
    Builder& emit(OpCode op, VReg dst, std::vector<Operand> ops) {
        Instruction i; i.op = op; i.dst = dst; i.operands = std::move(ops); ins_.push_back(std::move(i));
        return *this;
    }
    std::vector<Instruction> ins_;
};

}  // namespace nyx::ir
