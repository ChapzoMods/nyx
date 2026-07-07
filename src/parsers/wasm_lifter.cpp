// =============================================================================
// Nyx - A headless decompilation engine
// Copyright (C) 2024-2026 Chapzoo  <https://github.com/Chapzoo>
// SPDX-License-Identifier: GPL-3.0-or-later
// =============================================================================
//
// WASM bytecode lifter: walks the code bytes of a single WASM function body
// and produces a Nyx IR Function. The lifter models the WASM value stack:
// producers (local.get, i32.const, ...) push IR operands, and consumers
// (i32.add, local.set, br_if, call, ...) pop them.
//
// Mapping:
//   WASM local N  ->  vreg N+1   (vreg 0 is reserved as INVALID_VREG)
//   intermediate  ->  fresh vreg starting at (n_locals + 1) + 1
//
// Only the common bytecodes are handled; unknown ones become Opaque.
// =============================================================================
#include "nyx/parsers/wasm_lifter.hpp"

#include "nyx/core/bytes.hpp"
#include "nyx/core/logger.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace nyx {

namespace {

// WASM section/opcode constants (only the ones we lift).
constexpr std::uint8_t OP_UNREACHABLE   = 0x00;
constexpr std::uint8_t OP_NOP           = 0x01;
constexpr std::uint8_t OP_BLOCK         = 0x02;
constexpr std::uint8_t OP_LOOP          = 0x03;
constexpr std::uint8_t OP_IF            = 0x04;
constexpr std::uint8_t OP_ELSE          = 0x05;
constexpr std::uint8_t OP_END           = 0x0B;
constexpr std::uint8_t OP_BR            = 0x0C;
constexpr std::uint8_t OP_BR_IF         = 0x0D;
constexpr std::uint8_t OP_RETURN        = 0x0F;
constexpr std::uint8_t OP_CALL          = 0x10;
constexpr std::uint8_t OP_DROP          = 0x1A;
constexpr std:: uint8_t OP_SELECT       = 0x1B;
constexpr std::uint8_t OP_LOCAL_GET     = 0x20;
constexpr std::uint8_t OP_LOCAL_SET     = 0x21;
constexpr std::uint8_t OP_LOCAL_TEE     = 0x22;
constexpr std::uint8_t OP_GLOBAL_GET    = 0x23;
constexpr std::uint8_t OP_GLOBAL_SET    = 0x24;
constexpr std::uint8_t OP_I32_CONST     = 0x41;
constexpr std::uint8_t OP_I64_CONST     = 0x42;
constexpr std::uint8_t OP_F32_CONST     = 0x43;
constexpr std::uint8_t OP_F64_CONST     = 0x44;
constexpr std::uint8_t OP_I32_EQZ       = 0x45;
constexpr std::uint8_t OP_I32_EQ        = 0x46;
constexpr std::uint8_t OP_I32_NE        = 0x47;
constexpr std::uint8_t OP_I32_LT_S      = 0x48;
constexpr std::uint8_t OP_I32_LT_U      = 0x49;
constexpr std::uint8_t OP_I32_GT_S      = 0x4A;
constexpr std::uint8_t OP_I32_GT_U      = 0x4B;
constexpr std::uint8_t OP_I32_LE_S      = 0x4C;
constexpr std::uint8_t OP_I32_LE_U      = 0x4D;
constexpr std::uint8_t OP_I32_GE_S      = 0x4E;
constexpr std::uint8_t OP_I32_GE_U      = 0x4F;
constexpr std::uint8_t OP_I32_ADD       = 0x6A;
constexpr std::uint8_t OP_I32_SUB       = 0x6B;
constexpr std::uint8_t OP_I32_MUL       = 0x6C;
constexpr std::uint8_t OP_I32_DIV_S     = 0x6D;
constexpr std::uint8_t OP_I32_DIV_U     = 0x6E;
constexpr std::uint8_t OP_I32_REM_S     = 0x6F;
constexpr std::uint8_t OP_I32_REM_U     = 0x70;
constexpr std::uint8_t OP_I32_AND       = 0x71;
constexpr std::uint8_t OP_I32_OR        = 0x72;
constexpr std::uint8_t OP_I32_XOR       = 0x73;
constexpr std::uint8_t OP_I32_SHL       = 0x74;
constexpr std::uint8_t OP_I32_SHR_S     = 0x75;
constexpr std::uint8_t OP_I32_SHR_U     = 0x76;

/// Unsigned LEB128 reader.
std::uint64_t read_uleb128(const std::uint8_t*& p, const std::uint8_t* end) {
    std::uint64_t result = 0;
    int shift = 0;
    while (p < end && shift < 64) {
        const std::uint8_t byte = *p++;
        result |= static_cast<std::uint64_t>(byte & 0x7F) << shift;
        if ((byte & 0x80) == 0) break;
        shift += 7;
    }
    return result;
}

/// Signed LEB128 reader.
std::int64_t read_sleb128(const std::uint8_t*& p, const std::uint8_t* end) {
    std::int64_t result = 0;
    int shift = 0;
    std::uint8_t byte = 0;
    while (p < end && shift < 64) {
        byte = *p++;
        result |= static_cast<std::int64_t>(byte & 0x7F) << shift;
        shift += 7;
        if ((byte & 0x80) == 0) break;
    }
    if (shift < 64 && (byte & 0x40)) {
        result |= -(static_cast<std::int64_t>(1) << shift);
    }
    return result;
}

/// Skip a block type byte (currently 0x40 / 0x7F / 0x7E / 0x7D / 0x7C or a
/// signed positive s33 typeidx). For our simple lifter we just consume one
/// byte when it looks like a value type, otherwise treat it as an empty
/// block type. This is a heuristic; the spec allows multi-byte type indices
/// but in practice the common forms are single-byte.
void skip_block_type(const std::uint8_t*& p, const std::uint8_t* end) {
    if (p >= end) return;
    const std::uint8_t t = *p;
    if (t == 0x40 || t == 0x7F || t == 0x7E || t == 0x7D || t == 0x7C) {
        ++p;
    } else {
        // s33 typeidx encoded as sleb128 - read & discard.
        (void)read_sleb128(p, end);
    }
}

}  // namespace

ir::Function lift_wasm_function(const WasmFuncBody& body,
                                const WasmFuncType& type,
                                std::string name) {
    ir::Function fn;
    fn.name = std::move(name);

    // Synthetic entry address: WASM has no real VAs, so we reuse the
    // parser's convention of 0x1000 * (func_idx + 1).
    const std::uint64_t entry_addr = 0x1000ull * (static_cast<std::uint64_t>(body.func_idx) + 1);
    fn.entry = entry_addr;

    // Locals: params (from type) followed by declared locals. Local N maps
    // to vreg N+1 (vreg 0 is INVALID_VREG).
    const std::uint32_t n_params = static_cast<std::uint32_t>(type.params.size());
    const std::uint32_t n_locals = static_cast<std::uint32_t>(body.locals.size());
    const std::uint32_t first_temp_vreg = n_params + n_locals + 1;
    std::uint32_t next_vreg = first_temp_vreg;

    // Synthetic label used as the target of every br / br_if. We do not
    // track block nesting, so all branches target a single "function exit"
    // label past the end of the code.
    const std::uint64_t exit_label = entry_addr + body.code.size() + 1;

    // Value stack: holds IR operands pushed by producers.
    std::vector<ir::Operand> stack;
    stack.reserve(16);

    ir::Builder b;
    const std::uint8_t* p = body.code.data();
    const std::uint8_t* end = body.code.data() + body.code.size();
    std::uint64_t pc = entry_addr;

    auto push = [&](ir::Operand o) { stack.push_back(o); };
    auto pop = [&]() -> ir::Operand {
        if (stack.empty()) return ir::Operand::reg(ir::INVALID_VREG);
        ir::Operand o = stack.back();
        stack.pop_back();
        return o;
    };
    auto fresh = [&]() -> ir::VReg {
        const ir::VReg v = next_vreg++;
        return v;
    };
    // Emit a binary op that pops two operands (b then a) and pushes a fresh
    // result vreg.
    auto emit_binop = [&](ir::OpCode oc) {
        const ir::Operand rhs = pop();
        const ir::Operand lhs = pop();
        const ir::VReg dst = fresh();
        b.binop(oc, dst, lhs, rhs);
        push(ir::Operand::reg(dst));
    };
    // Emit a comparison op (modelled as Cmp).
    auto emit_cmp = [&]() {
        const ir::Operand rhs = pop();
        const ir::Operand lhs = pop();
        const ir::VReg dst = fresh();
        b.cmp(dst, lhs, rhs);
        push(ir::Operand::reg(dst));
    };

    while (p < end) {
        const std::uint8_t op = *p++;
        const std::uint64_t insn_addr = pc;
        pc = (p - body.code.data()) + entry_addr;

        switch (op) {
            case OP_UNREACHABLE:
                b.opaque("unreachable");
                break;
            case OP_NOP:
                b.nop();
                break;
            case OP_BLOCK:
            case OP_LOOP:
            case OP_IF:
                // Skip the block type. We do not model block nesting; the
                // contained instructions are lifted linearly.
                skip_block_type(p, end);
                break;
            case OP_ELSE:
                // No operand; just continue lifting until matching END.
                break;
            case OP_END: {
                // Top-level END terminates the function. Treat it as Return
                // (the WASM runtime implicitly returns the topmost result).
                b.ret();
                break;
            }
            case OP_BR: {
                const auto label_idx = read_uleb128(p, end);
                (void)label_idx;  // we collapse all br targets to exit_label
                b.branch(exit_label);
                break;
            }
            case OP_BR_IF: {
                const auto label_idx = read_uleb128(p, end);
                (void)label_idx;
                const ir::Operand cond = pop();
                b.branch_cond(cond, exit_label);
                break;
            }
            case OP_RETURN: {
                b.ret();
                break;
            }
            case OP_CALL: {
                const auto func_idx = read_uleb128(p, end);
                b.call(ir::Operand::imm(static_cast<std::int64_t>(func_idx)));
                // Assume the call produces a single value; if it doesn't,
                // the spurious stack entry will simply be discarded.
                push(ir::Operand::reg(fresh()));
                break;
            }
            case OP_DROP: {
                (void)pop();
                break;
            }
            case OP_SELECT: {
                const ir::Operand cond = pop();
                const ir::Operand f = pop();
                const ir::Operand t = pop();
                const ir::VReg dst = fresh();
                // Model select as: dst = (cond) ? t : f  ->  cmp + select is
                // not modelled in IR; emit a Cmp then a Mov as a placeholder.
                b.cmp(dst, cond, ir::Operand::imm(0));
                (void)t; (void)f;
                push(ir::Operand::reg(dst));
                break;
            }
            case OP_LOCAL_GET: {
                const auto idx = static_cast<std::uint32_t>(read_uleb128(p, end));
                const ir::VReg local_v = idx + 1;
                const ir::VReg dst = fresh();
                b.mov(dst, ir::Operand::reg(local_v));
                push(ir::Operand::reg(dst));
                break;
            }
            case OP_LOCAL_SET: {
                const auto idx = static_cast<std::uint32_t>(read_uleb128(p, end));
                const ir::VReg local_v = idx + 1;
                const ir::Operand v = pop();
                b.mov(local_v, v);
                break;
            }
            case OP_LOCAL_TEE: {
                const auto idx = static_cast<std::uint32_t>(read_uleb128(p, end));
                const ir::VReg local_v = idx + 1;
                const ir::Operand v = pop();
                b.mov(local_v, v);
                // tee: leave the value on the stack.
                push(v);
                break;
            }
            case OP_GLOBAL_GET: {
                const auto idx = read_uleb128(p, end);
                const ir::VReg dst = fresh();
                b.mov(dst, ir::Operand::imm(static_cast<std::int64_t>(idx)));
                push(ir::Operand::reg(dst));
                break;
            }
            case OP_GLOBAL_SET: {
                const auto idx = read_uleb128(p, end);
                const ir::Operand v = pop();
                const ir::VReg dst = fresh();
                b.mov(dst, v);
                (void)idx;
                break;
            }
            case OP_I32_CONST: {
                const auto v = read_sleb128(p, end);
                const ir::VReg dst = fresh();
                b.mov(dst, ir::Operand::imm(v));
                push(ir::Operand::reg(dst));
                break;
            }
            case OP_I64_CONST: {
                const auto v = read_sleb128(p, end);
                const ir::VReg dst = fresh();
                b.mov(dst, ir::Operand::imm(v));
                push(ir::Operand::reg(dst));
                break;
            }
            case OP_F32_CONST: {
                // Skip 4 bytes of float data.
                if (p + 4 <= end) p += 4;
                const ir::VReg dst = fresh();
                b.mov(dst, ir::Operand::imm(0));
                push(ir::Operand::reg(dst));
                break;
            }
            case OP_F64_CONST: {
                if (p + 8 <= end) p += 8;
                const ir::VReg dst = fresh();
                b.mov(dst, ir::Operand::imm(0));
                push(ir::Operand::reg(dst));
                break;
            }
            case OP_I32_EQZ: {
                const ir::Operand a = pop();
                const ir::VReg dst = fresh();
                b.cmp(dst, a, ir::Operand::imm(0));
                push(ir::Operand::reg(dst));
                break;
            }
            case OP_I32_EQ: case OP_I32_NE:
            case OP_I32_LT_S: case OP_I32_LT_U:
            case OP_I32_GT_S: case OP_I32_GT_U:
            case OP_I32_LE_S: case OP_I32_LE_U:
            case OP_I32_GE_S: case OP_I32_GE_U:
                emit_cmp();
                break;
            case OP_I32_ADD: emit_binop(ir::OpCode::Add); break;
            case OP_I32_SUB: emit_binop(ir::OpCode::Sub); break;
            case OP_I32_MUL: emit_binop(ir::OpCode::Mul); break;
            case OP_I32_DIV_S:
            case OP_I32_DIV_U: emit_binop(ir::OpCode::Div); break;
            case OP_I32_REM_S:
            case OP_I32_REM_U: emit_binop(ir::OpCode::Mod); break;
            case OP_I32_AND: emit_binop(ir::OpCode::And); break;
            case OP_I32_OR:  emit_binop(ir::OpCode::Or);  break;
            case OP_I32_XOR: emit_binop(ir::OpCode::Xor); break;
            case OP_I32_SHL: emit_binop(ir::OpCode::Shl); break;
            case OP_I32_SHR_S: emit_binop(ir::OpCode::Sar); break;
            case OP_I32_SHR_U: emit_binop(ir::OpCode::Shr); break;
            default: {
                // Unknown bytecode - record it as Opaque with the opcode hex.
                std::string mnem = "wasm_op_0x";
                const char hex[] = "0123456789abcdef";
                mnem.push_back(hex[(op >> 4) & 0xF]);
                mnem.push_back(hex[op & 0xF]);
                b.opaque(mnem);
                break;
            }
        }

        // Stamp the just-emitted instruction(s) with the originating address.
        const auto& view = b.view();
        if (!view.empty()) {
            // The builder does not expose per-instruction addressing; the
            // CFG builder will assign addresses based on `pc` ordering, so
            // we leave the raw addresses unset here.
        }
        (void)insn_addr;
    }

    // Ensure the function always ends with a Return (WASM bodies end with
    // END which we already converted, but guard against truncated input).
    if (b.view().empty() || b.view().back().op != ir::OpCode::Return) {
        b.ret();
    }

    auto insns = std::move(b).finish();
    // Assign sequential addresses so the CFG builder can split correctly.
    std::uint64_t a = entry_addr;
    for (auto& in : insns) {
        in.addr = a++;
    }
    fn = ir::CFGBuilder::build(std::move(insns), entry_addr, fn.name);
    return fn;
}

}  // namespace nyx
