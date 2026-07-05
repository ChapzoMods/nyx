// =============================================================================
// Nyx - A headless decompilation engine
// Copyright (C) 2024-2026 Chapzoo  <https://github.com/Chapzoo>
// SPDX-License-Identifier: GPL-3.0-or-later
// =============================================================================
#include "nyx/lifter/instruction_lifter.hpp"

#include "nyx/core/bytes.hpp"
#include "nyx/core/logger.hpp"

#include <capstone/capstone.h>
#include <capstone/x86.h>
#include <capstone/arm.h>
#include <capstone/arm64.h>

#include <algorithm>
#include <cstring>
#include <optional>
#include <string>
#include <unordered_map>

namespace nyx {

InstructionLifter::InstructionLifter(Arch arch, Endian endian)
    : arch_(arch), endian_(endian) {}

ir::VReg InstructionLifter::map_reg(std::uint32_t machine_reg) const {
    auto it = reg_map_.find(machine_reg);
    if (it != reg_map_.end()) return it->second;
    ir::VReg v = next_vreg_++;
    reg_map_[machine_reg] = v;
    return v;
}

ir::VReg InstructionLifter::map_reg_by_name(const std::string& name) const {
    // Map register names (e.g. "rax", "x0", "w5", "sp") to stable vregs.
    // We hash the name into a 32-bit key and feed it through map_reg so the
    // per-function allocator deduplicates them naturally.
    const std::uint32_t key = static_cast<std::uint32_t>(std::hash<std::string>{}(name));
    return map_reg(key);
}

// ---------------------------------------------------------------------------
// Shared operand-parsing utilities used by every architecture's lifter.
//
// Capstone gives us a mnemonic string and an op_str string like
// "rax, 0x10" or "[x0, #0x10]". We parse these back into IR Operands
// without depending on Capstone's detail struct, so the lifter code stays
// uniform across architectures and the public DecodedInstruction stays
// lightweight.
// ---------------------------------------------------------------------------
namespace {

/// Splits an op_str on top-level commas (respecting [] and () nesting).
std::vector<std::string> split_ops(const std::string& s) {
    std::vector<std::string> parts;
    std::string cur;
    int depth = 0;
    for (char c : s) {
        if (c == '[' || c == '(') ++depth;
        else if (c == ']' || c == ')') --depth;
        if (c == ',' && depth == 0) {
            while (!cur.empty() && (cur.back() == ' ' || cur.back() == '\t')) cur.pop_back();
            while (!cur.empty() && (cur.front() == ' ' || cur.front() == '\t')) cur.erase(0, 1);
            if (!cur.empty()) parts.push_back(cur);
            cur.clear();
            continue;
        }
        cur.push_back(c);
    }
    while (!cur.empty() && (cur.back() == ' ' || cur.back() == '\t')) cur.pop_back();
    while (!cur.empty() && (cur.front() == ' ' || cur.front() == '\t')) cur.erase(0, 1);
    if (!cur.empty()) parts.push_back(cur);
    return parts;
}

bool is_imm_token(const std::string& s) {
    if (s.empty()) return false;
    std::size_t i = 0;
    if (s.size() >= 3 && s[0] == '-' && s[1] == '0' && (s[2] == 'x' || s[2] == 'X')) i = 3;
    else if (s.size() >= 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) i = 2;
    else return std::all_of(s.begin(), s.end(), [](char c){ return (c >= '0' && c <= '9') || c == '-'; });
    if (i >= s.size()) return false;
    for (; i < s.size(); ++i) {
        const char c = s[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return false;
    }
    return true;
}

std::int64_t parse_imm(const std::string& s) {
    try {
        return static_cast<std::int64_t>(std::stoull(s, nullptr, 0));
    } catch (const std::exception&) {
        return 0;
    }
}

bool is_mem_token(const std::string& s) {
    return !s.empty() && (s.front() == '[' || s.front() == '(');
}

/// Parses a memory operand of the form "[base + index*scale + disp]" or
/// "(base + index*scale + disp)". Returns an IR Mem operand.
/// `map_reg` is a callable that converts a register name string to a VReg.
template <typename RegMap>
ir::Operand parse_mem_operand(std::string t, RegMap map_reg) {
    // Strip outer brackets/parens. Handle both "[base+disp]" (x86/ARM) and
    // "disp(reg)" (MIPS/PPC) forms.
    if (!t.empty() && t.front() == '[') t.erase(0, 1);
    if (!t.empty() && t.back()  == ']') t.pop_back();
    // For "disp(reg)" the '(' is in the middle - split it explicitly.
    std::string disp_part;
    if (auto p = t.find('('); p != std::string::npos) {
        disp_part = t.substr(0, p);
        t = t.substr(p);
        if (!t.empty() && t.front() == '(') t.erase(0, 1);
        if (!t.empty() && t.back()  == ')') t.pop_back();
    }
    while (!t.empty() && t.front() == ' ') t.erase(0, 1);
    while (!t.empty() && t.back()  == ' ') t.pop_back();
    // Trim disp_part.
    while (!disp_part.empty() && disp_part.front() == ' ') disp_part.erase(0, 1);
    while (!disp_part.empty() && disp_part.back()  == ' ') disp_part.pop_back();
    if (t.empty() && disp_part.empty()) return ir::Operand::mem(ir::INVALID_VREG, 0);

    ir::Operand op{};
    op.kind      = ir::Operand::Kind::Mem;
    op.mem_base  = ir::INVALID_VREG;
    op.mem_index = ir::INVALID_VREG;
    op.mem_scale = 1;
    op.mem_disp  = 0;
    op.mem_size  = 0;

    // disp_part holds an optional leading displacement (MIPS/PPC form).
    if (!disp_part.empty() && is_imm_token(disp_part)) {
        op.mem_disp = parse_imm(disp_part);
    }

    std::string tok;
    int sign = 1;
    auto flush = [&]() {
        while (!tok.empty() && tok.front() == ' ') tok.erase(0, 1);
        while (!tok.empty() && tok.back()  == ' ') tok.pop_back();
        if (tok.empty()) { tok.clear(); return; }

        // Size override ("byte ptr", "dword ptr", ...): discard.
        if (tok == "ptr" || tok == "short" || tok == "long" || tok == "tbyte") { tok.clear(); return; }
        if (tok.find("ptr") != std::string::npos) { tok.clear(); return; }
        if (tok == "byte" || tok == "word" || tok == "dword" || tok == "qword" || tok == "xmmword") {
            tok.clear(); return;
        }

        // scale*reg form: "1*eax" or "eax*4".
        if (auto star = tok.find('*'); star != std::string::npos) {
            std::string left = tok.substr(0, star);
            std::string right = tok.substr(star + 1);
            while (!left.empty() && left.back()  == ' ') left.pop_back();
            while (!right.empty() && right.front() == ' ') right.erase(0, 1);
            if (left.find_first_not_of("0123456789") == std::string::npos) {
                op.mem_scale = static_cast<std::uint8_t>(std::stoul(left));
                op.mem_index = map_reg(right);
            } else {
                op.mem_index = map_reg(left);
                op.mem_scale = static_cast<std::uint8_t>(std::stoul(right));
            }
            tok.clear(); return;
        }

        // Strip leading '#' (ARM/MIPS immediate prefix) for imm detection.
        std::string imm_test = tok;
        if (!imm_test.empty() && imm_test.front() == '#') imm_test.erase(0, 1);
        if (is_imm_token(imm_test)) {
            op.mem_disp += sign * parse_imm(imm_test);
            tok.clear(); return;
        }

        if (op.mem_base == ir::INVALID_VREG) op.mem_base = map_reg(tok);
        else if (op.mem_index == ir::INVALID_VREG) op.mem_index = map_reg(tok);
        tok.clear();
    };

    for (char c : t) {
        if (c == '+' || c == '-') { flush(); sign = (c == '+') ? 1 : -1; continue; }
        // Commas inside the bracket (ARM64/PPC/MIPS syntax like
        // "[x1, #0x10]" or "disp(rA)") act as token separators too.
        if (c == ',') { flush(); continue; }
        tok.push_back(c);
    }
    flush();
    return op;
}

/// Parses a single operand string into an IR Operand.
template <typename RegMap>
ir::Operand parse_operand(std::string t, RegMap map_reg) {
    while (!t.empty() && (t.front() == ' ' || t.front() == '\t')) t.erase(0, 1);
    while (!t.empty() && (t.back()  == ' ' || t.back()  == '\t')) t.pop_back();
    if (t.empty()) return ir::Operand::imm(0);

    if (is_mem_token(t)) return parse_mem_operand(t, map_reg);

    // MIPS/PPC "disp(reg)" form: the '(' is not at the front.
    if (t.find('(') != std::string::npos && t.back() == ')') {
        return parse_mem_operand(t, map_reg);
    }

    std::string bare = (t.front() == '#') ? t.substr(1) : t;
    if (is_imm_token(bare)) {
        return ir::Operand::imm(parse_imm(bare));
    }

    if (t.find("ptr") != std::string::npos) {
        auto p = t.find('[');
        if (p == std::string::npos) p = t.find('(');
        if (p != std::string::npos) return parse_mem_operand(t.substr(p), map_reg);
    }

    return ir::Operand::reg(map_reg(t));
}

}  // namespace

// ---------------------------------------------------------------------------
// x86 / x86-64 lifting - real lifter using the shared operand parser.
//
// v0.0.3 coverage: mov/movzx/movsx/movsxd, lea, add/sub/imul/mul/div/idiv,
// and/or/xor/shl/sal/shr/sar, inc/dec/neg/not, push/pop, cmp/test, jmp/jcc,
// call, ret/retn/retq, leave, nop/int3/hlt/syscall/int, plus size variants
// (movb/movw/movl/movq). Anything else falls back to Opaque.
// ---------------------------------------------------------------------------
std::vector<ir::Instruction> InstructionLifter::lift_x86(const DecodedInstruction& insn) const {
    ir::Builder b;
    const std::string& m = insn.mnemonic;
    const std::string& o = insn.op_str;

    auto reg = [&](const std::string& s) { return map_reg_by_name(s); };
    auto op_at = [&](const std::vector<std::string>& parts, std::size_t i) -> ir::Operand {
        if (i >= parts.size()) return ir::Operand::imm(0);
        return parse_operand(parts[i], [this](const std::string& s){ return this->map_reg_by_name(s); });
    };
    (void)reg;

    const auto parts = split_ops(o);

    auto fresh = [&]() -> ir::VReg {
        return map_reg(0xBEEF0000u + static_cast<std::uint32_t>(b.view().size()));
    };

    if (m == "nop" || m == "nopw" || m == "int3" || m == "hlt") {
        b.nop();
    } else if (m == "mov" || m == "movq" || m == "movl" || m == "movw" || m == "movb"
            || m == "movzx" || m == "movsx" || m == "movsxd") {
        if (parts.size() >= 2) {
            const auto d = op_at(parts, 0);
            const auto s = op_at(parts, 1);
            if (d.kind == ir::Operand::Kind::Register && s.kind == ir::Operand::Kind::Mem) {
                // mov reg, [mem] -> load
                b.load(d.vreg, s);
            } else if (d.kind == ir::Operand::Kind::Register) {
                b.mov(d.vreg, s);
            } else if (d.kind == ir::Operand::Kind::Mem) {
                b.store(d, s);
            }
        }
    } else if (m == "lea") {
        if (parts.size() >= 2) {
            const auto d = op_at(parts, 0);
            const auto s = op_at(parts, 1);
            if (d.kind == ir::Operand::Kind::Register && s.kind == ir::Operand::Kind::Mem) {
                if (s.mem_base != ir::INVALID_VREG) {
                    b.mov(d.vreg, ir::Operand::reg(s.mem_base));
                    if (s.mem_disp != 0)
                        b.binop(ir::OpCode::Add, d.vreg, ir::Operand::reg(d.vreg), ir::Operand::imm(s.mem_disp));
                } else {
                    b.mov(d.vreg, ir::Operand::imm(s.mem_disp));
                }
            }
        }
    } else if (m == "add" || m == "sub" || m == "imul" || m == "mul" || m == "div" || m == "idiv"
            || m == "and" || m == "or" || m == "xor" || m == "shl" || m == "sal" || m == "shr" || m == "sar") {
        if (parts.size() >= 2) {
            auto d = op_at(parts, 0);
            auto s = op_at(parts, 1);
            ir::OpCode oc = ir::OpCode::Add;
            if (m == "sub")                   oc = ir::OpCode::Sub;
            else if (m == "mul" || m == "imul") oc = ir::OpCode::Mul;
            else if (m == "div" || m == "idiv") oc = ir::OpCode::Div;
            else if (m == "and")              oc = ir::OpCode::And;
            else if (m == "or")               oc = ir::OpCode::Or;
            else if (m == "xor")              oc = ir::OpCode::Xor;
            else if (m == "shl" || m == "sal") oc = ir::OpCode::Shl;
            else if (m == "shr")              oc = ir::OpCode::Shr;
            else if (m == "sar")              oc = ir::OpCode::Sar;
            if (d.kind == ir::Operand::Kind::Register) {
                b.binop(oc, d.vreg, ir::Operand::reg(d.vreg), s);
            } else if (d.kind == ir::Operand::Kind::Mem) {
                ir::VReg tmp = map_reg(0xDEAD0000u + static_cast<std::uint32_t>(b.view().size()));
                b.load(tmp, d);
                b.binop(oc, tmp, ir::Operand::reg(tmp), s);
                b.store(d, ir::Operand::reg(tmp));
            }
        }
    } else if (m == "inc" || m == "dec" || m == "neg" || m == "not") {
        if (!parts.empty()) {
            auto d = op_at(parts, 0);
            if (d.kind == ir::Operand::Kind::Register) {
                if (m == "inc")      b.binop(ir::OpCode::Add, d.vreg, ir::Operand::reg(d.vreg), ir::Operand::imm(1));
                else if (m == "dec") b.binop(ir::OpCode::Sub, d.vreg, ir::Operand::reg(d.vreg), ir::Operand::imm(1));
                else if (m == "neg") b.binop(ir::OpCode::Sub, d.vreg, ir::Operand::imm(0), ir::Operand::reg(d.vreg));
                else                 b.binop(ir::OpCode::Xor, d.vreg, ir::Operand::reg(d.vreg), ir::Operand::imm(-1));
            }
        }
    } else if (m == "push") {
        if (!parts.empty()) b.push(op_at(parts, 0));
        else                b.nop();
    } else if (m == "pop") {
        if (!parts.empty()) {
            auto d = op_at(parts, 0);
            if (d.kind == ir::Operand::Kind::Register) b.pop(d.vreg);
            else                                       b.opaque("pop " + o);
        } else b.opaque("pop");
    } else if (m == "cmp" || m == "test") {
        if (parts.size() >= 2) {
            ir::VReg tmp = fresh();
            b.cmp(tmp, op_at(parts, 0), op_at(parts, 1));
        }
    } else if (m == "jmp") {
        if (auto t = insn.direct_target()) b.branch(*t);
        else b.opaque("jmp " + o);
    } else if (m.rfind("j", 0) == 0 && m != "jmp" && m.size() >= 2 && m[0] == 'j') {
        if (auto t = insn.direct_target()) {
            ir::VReg cond = fresh();
            b.branch_cond(ir::Operand::reg(cond), *t);
        } else {
            b.opaque(m + " " + o);
        }
    } else if (m == "call") {
        if (auto t = insn.direct_target()) {
            b.call(ir::Operand::imm(static_cast<std::int64_t>(*t)));
        } else if (!o.empty()) {
            if (o.find("0x") == 0)             b.call(ir::Operand::imm(parse_imm(o)));
            else if (o.find('_') != std::string::npos
                  || o.find('@') != std::string::npos) b.call(ir::Operand::sym(o));
            else                                b.call(op_at(parts, 0));
        } else {
            b.opaque("call");
        }
    } else if (m == "ret" || m == "retn" || m == "retq") {
        b.ret();
    } else if (m == "leave") {
        // leave = mov rsp, rbp; pop rbp. Approximate as a single opaque op.
        b.opaque("leave");
    } else if (m == "syscall" || m == "int" || m == "int3") {
        b.opaque(m + " " + o);
    } else {
        b.opaque(m + (o.empty() ? "" : (" " + o)));
    }

    auto out = std::move(b).finish();
    for (auto& i : out) i.addr = insn.address;
    return out;
}

// ---------------------------------------------------------------------------
// ARM64 / AArch64 lifting - uses the shared operand parser.
//
// v0.0.3 coverage: mov/movz/movk/movn, add/sub/mul/and/or/eor/lsl/lsr/asr,
// udiv/sdiv, neg/mvn/negs, ldr/ldrsw/ldrb/ldrh/ldur, str/strb/strh/stur,
// ldp/stp, cmp/cmn/tst, b/bl/blr, b.<cond>, cbz/cbnz/tbz/tbnz, adrp/adr,
// br/braa/brab, ret/retaa/retab, nop/wfi/wfe/yield/sev.
// ---------------------------------------------------------------------------
std::vector<ir::Instruction> InstructionLifter::lift_arm64(const DecodedInstruction& insn) const {
    ir::Builder b;
    const std::string& m = insn.mnemonic;
    const std::string& o = insn.op_str;
    const auto ops = split_ops(o);

    auto op = [&](std::size_t i) -> ir::Operand {
        if (i >= ops.size()) return ir::Operand::imm(0);
        return parse_operand(ops[i], [this](const std::string& s){ return this->map_reg_by_name(s); });
    };
    auto reg = [&](std::size_t i) -> ir::VReg {
        if (i >= ops.size()) return ir::INVALID_VREG;
        // The operand may have a '#' prefix or be a register name; strip
        // the '#' so map_reg_by_name sees a clean register name.
        std::string t = ops[i];
        while (!t.empty() && (t.front() == ' ' || t.front() == '\t' || t.front() == '#')) t.erase(0, 1);
        while (!t.empty() && (t.back()  == ' ' || t.back()  == '\t')) t.pop_back();
        if (t.empty() || is_imm_token(t) || is_mem_token(t)) return ir::INVALID_VREG;
        return map_reg_by_name(t);
    };
    auto fresh = [&]() -> ir::VReg {
        return map_reg(0xBEEF0000u + static_cast<std::uint32_t>(b.view().size()));
    };

    if (m == "nop" || m == "wfi" || m == "wfe" || m == "yield" || m == "sev") {
        b.nop();
    } else if (m == "ret" || m == "retaa" || m == "retab") {
        b.ret();
    } else if (m == "b") {
        if (auto t = insn.direct_target()) b.branch(*t);
        else b.opaque(m + " " + o);
    } else if (m == "bl" || m == "blr") {
        std::optional<std::uint64_t> t = (m == "bl") ? insn.direct_target() : std::nullopt;
        if (m == "bl" && t.has_value()) {
            b.call(ir::Operand::imm(static_cast<std::int64_t>(*t)));
        } else if (!ops.empty()) {
            b.call(op(0));
        } else {
            b.opaque(m + " " + o);
        }
    } else if (m.rfind("b.", 0) == 0 || m == "cbz" || m == "cbnz" || m == "tbz" || m == "tbnz") {
        if (auto t = insn.direct_target()) {
            ir::VReg cond = fresh();
            if (m == "cbz" || m == "cbnz" || m == "tbz" || m == "tbnz") {
                const ir::VReg tested = reg(0);
                b.cmp(cond, ir::Operand::reg(tested), ir::Operand::imm(0));
            }
            b.branch_cond(ir::Operand::reg(cond), *t);
        } else {
            b.opaque(m + " " + o);
        }
    } else if (m == "mov" || m == "movz" || m == "orr" || m == "movn") {
        if (ops.size() >= 2) b.mov(reg(0), op(1));
        else b.opaque(m + " " + o);
    } else if (m == "movk") {
        if (ops.size() >= 2) b.mov(reg(0), op(1));
        else b.opaque(m + " " + o);
    } else if (m == "add" || m == "sub" || m == "mul" || m == "and" || m == "or" || m == "eor"
            || m == "lsl" || m == "lsr" || m == "asr" || m == "udiv" || m == "sdiv") {
        if (ops.size() >= 3) {
            ir::OpCode oc = ir::OpCode::Add;
            if (m == "sub")  oc = ir::OpCode::Sub;
            else if (m == "mul")  oc = ir::OpCode::Mul;
            else if (m == "udiv" || m == "sdiv") oc = ir::OpCode::Div;
            else if (m == "and")  oc = ir::OpCode::And;
            else if (m == "or")   oc = ir::OpCode::Or;
            else if (m == "eor")  oc = ir::OpCode::Xor;
            else if (m == "lsl")  oc = ir::OpCode::Shl;
            else if (m == "lsr")  oc = ir::OpCode::Shr;
            else if (m == "asr")  oc = ir::OpCode::Sar;
            b.binop(oc, reg(0), op(1), op(2));
        } else b.opaque(m + " " + o);
    } else if (m == "neg" || m == "mvn" || m == "negs") {
        if (ops.size() >= 2) {
            if (m == "mvn") b.binop(ir::OpCode::Xor, reg(0), op(1), ir::Operand::imm(-1));
            else            b.binop(ir::OpCode::Sub, reg(0), ir::Operand::imm(0), op(1));
        } else b.opaque(m + " " + o);
    } else if (m == "ldr" || m == "ldrsw" || m == "ldrb" || m == "ldrh" || m == "ldur") {
        if (ops.size() >= 2) b.load(reg(0), op(1));
        else b.opaque(m + " " + o);
    } else if (m == "str" || m == "strb" || m == "strh" || m == "stur") {
        if (ops.size() >= 2) b.store(op(1), op(0));
        else b.opaque(m + " " + o);
    } else if (m == "ldp") {
        if (ops.size() >= 3) {
            b.load(reg(0), op(2));
            ir::Operand a2 = op(2);
            if (a2.kind == ir::Operand::Kind::Mem) { a2.mem_disp += 8; b.load(reg(1), a2); }
            else b.opaque(m + " " + o);
        } else b.opaque(m + " " + o);
    } else if (m == "stp") {
        if (ops.size() >= 3) {
            b.store(op(2), op(0));
            ir::Operand a2 = op(2);
            if (a2.kind == ir::Operand::Kind::Mem) { a2.mem_disp += 8; b.store(a2, op(1)); }
            else b.opaque(m + " " + o);
        } else b.opaque(m + " " + o);
    } else if (m == "cmp" || m == "cmn" || m == "tst") {
        if (ops.size() >= 2) {
            ir::VReg tmp = fresh();
            b.cmp(tmp, op(0), op(1));
        } else b.opaque(m + " " + o);
    } else if (m == "adrp" || m == "adr") {
        if (ops.size() >= 2) b.mov(reg(0), op(1));
        else b.opaque(m + " " + o);
    } else if (m == "br" || m == "braa" || m == "brab") {
        b.opaque(m + " " + o);
    } else {
        b.opaque(m + (o.empty() ? "" : (" " + o)));
    }

    auto out = std::move(b).finish();
    for (auto& i : out) i.addr = insn.address;
    return out;
}

// ---------------------------------------------------------------------------
// ARM32 / AArch32 lifting - real lifter using shared operand parser.
//
// v0.0.3 coverage: mov/movw/movt, add/sub/rsb/mul/and/orr/eor/lsl/lsr/asr,
// ldr/ldr[b|h]/str/str[b|h], cmp/cmn/tst, b/bl/bx/blx, b.<cond>, cbz/cbnz,
// push/pop, mvn/rsb/neg, nop. Anything else falls back to Opaque.
// ---------------------------------------------------------------------------
std::vector<ir::Instruction> InstructionLifter::lift_arm32(const DecodedInstruction& insn) const {
    ir::Builder b;
    const std::string& m = insn.mnemonic;
    const std::string& o = insn.op_str;
    const auto ops = split_ops(o);

    auto op = [&](std::size_t i) -> ir::Operand {
        if (i >= ops.size()) return ir::Operand::imm(0);
        return parse_operand(ops[i], [this](const std::string& s){ return this->map_reg_by_name(s); });
    };
    auto reg = [&](std::size_t i) -> ir::VReg {
        if (i >= ops.size()) return ir::INVALID_VREG;
        std::string t = ops[i];
        while (!t.empty() && (t.front() == ' ' || t.front() == '\t' || t.front() == '#')) t.erase(0, 1);
        while (!t.empty() && (t.back()  == ' ' || t.back()  == '\t')) t.pop_back();
        if (t.empty() || is_imm_token(t) || is_mem_token(t)) return ir::INVALID_VREG;
        return map_reg_by_name(t);
    };
    auto fresh = [&]() -> ir::VReg {
        return map_reg(0xBEEF0000u + static_cast<std::uint32_t>(b.view().size()));
    };

    if (m == "nop" || m == "wfi" || m == "wfe") {
        b.nop();
    } else if (m == "bx" || m == "blx") {
        // Indirect branch/call through register. For blx we model as call;
        // for bx we treat as Opaque (can't resolve statically).
        if (m == "blx" && !ops.empty()) b.call(op(0));
        else                            b.opaque(m + " " + o);
    } else if (m == "ret") {
        b.ret();
    } else if (m == "b" || m == "bl") {
        if (auto t = insn.direct_target()) {
            if (m == "bl") b.call(ir::Operand::imm(static_cast<std::int64_t>(*t)));
            else           b.branch(*t);
        } else b.opaque(m + " " + o);
    } else if (m.rfind("b.", 0) == 0 || m == "cbz" || m == "cbnz") {
        if (auto t = insn.direct_target()) {
            ir::VReg cond = fresh();
            if (m == "cbz" || m == "cbnz") {
                const ir::VReg tested = reg(0);
                b.cmp(cond, ir::Operand::reg(tested), ir::Operand::imm(0));
            }
            b.branch_cond(ir::Operand::reg(cond), *t);
        } else b.opaque(m + " " + o);
    } else if (m == "mov" || m == "movw" || m == "movt" || m == "mvn" || m == "neg" || m == "rsb") {
        if (ops.size() >= 2) {
            if (m == "mvn")      b.binop(ir::OpCode::Xor, reg(0), op(1), ir::Operand::imm(-1));
            else if (m == "neg") b.binop(ir::OpCode::Sub, reg(0), ir::Operand::imm(0), op(1));
            else if (m == "rsb") b.binop(ir::OpCode::Sub, reg(0), op(1), ir::Operand::reg(reg(0)));
            else                 b.mov(reg(0), op(1));
        } else b.opaque(m + " " + o);
    } else if (m == "add" || m == "sub" || m == "mul" || m == "and" || m == "orr" || m == "eor"
            || m == "lsl" || m == "lsr" || m == "asr") {
        if (ops.size() >= 3) {
            ir::OpCode oc = ir::OpCode::Add;
            if (m == "sub") oc = ir::OpCode::Sub;
            else if (m == "mul") oc = ir::OpCode::Mul;
            else if (m == "and") oc = ir::OpCode::And;
            else if (m == "orr") oc = ir::OpCode::Or;
            else if (m == "eor") oc = ir::OpCode::Xor;
            else if (m == "lsl") oc = ir::OpCode::Shl;
            else if (m == "lsr") oc = ir::OpCode::Shr;
            else if (m == "asr") oc = ir::OpCode::Sar;
            b.binop(oc, reg(0), op(1), op(2));
        } else b.opaque(m + " " + o);
    } else if (m == "ldr" || m == "ldrb" || m == "ldrh" || m == "ldrd") {
        if (ops.size() >= 2) b.load(reg(0), op(1));
        else b.opaque(m + " " + o);
    } else if (m == "str" || m == "strb" || m == "strh" || m == "strd") {
        if (ops.size() >= 2) b.store(op(1), op(0));
        else b.opaque(m + " " + o);
    } else if (m == "cmp" || m == "cmn" || m == "tst") {
        if (ops.size() >= 2) {
            ir::VReg tmp = fresh();
            b.cmp(tmp, op(0), op(1));
        } else b.opaque(m + " " + o);
    } else if (m == "push" || m == "pop") {
        b.opaque(m + " " + o);
    } else {
        b.opaque(m + (o.empty() ? "" : (" " + o)));
    }

    auto out = std::move(b).finish();
    for (auto& i : out) i.addr = insn.address;
    return out;
}

// ---------------------------------------------------------------------------
// PowerPC 32/64 lifting - basic lifter using shared operand parser.
//
// v0.0.3 coverage: add/addi/addis, sub/subf/subfic, mullw/mulhw, and/andi/or
// /ori/xor/xori, slw/srw/sraw, lwz/lwzu/lhz/lbz/stw/stwu/sth/stb, cmp/cmpl,
// b/bl/bc/bclr, sc, nop. PPC register naming is r0..r31 (and f0..f31 for
// floats - treated as opaque). Anything else falls back to Opaque.
// ---------------------------------------------------------------------------
std::vector<ir::Instruction> InstructionLifter::lift_ppc(const DecodedInstruction& insn) const {
    ir::Builder b;
    const std::string& m = insn.mnemonic;
    const std::string& o = insn.op_str;
    const auto ops = split_ops(o);

    auto op = [&](std::size_t i) -> ir::Operand {
        if (i >= ops.size()) return ir::Operand::imm(0);
        return parse_operand(ops[i], [this](const std::string& s){ return this->map_reg_by_name(s); });
    };
    auto reg = [&](std::size_t i) -> ir::VReg {
        if (i >= ops.size()) return ir::INVALID_VREG;
        std::string t = ops[i];
        while (!t.empty() && (t.front() == ' ' || t.front() == '\t')) t.erase(0, 1);
        while (!t.empty() && (t.back()  == ' ' || t.back()  == '\t')) t.pop_back();
        if (t.empty() || is_imm_token(t) || is_mem_token(t)) return ir::INVALID_VREG;
        return map_reg_by_name(t);
    };
    auto fresh = [&]() -> ir::VReg {
        return map_reg(0xBEEF0000u + static_cast<std::uint32_t>(b.view().size()));
    };

    if (m == "nop" || m == "ori" || m == "or." || m == "ori.") {
        // ori rX, rY, 0 is the canonical PPC nop ("ori. 1,1,0" sometimes).
        if (m == "nop") b.nop();
        else if (ops.size() >= 3) b.binop(ir::OpCode::Or, reg(0), op(1), op(2));
        else b.opaque(m + " " + o);
    } else if (m == "b") {
        if (auto t = insn.direct_target()) b.branch(*t);
        else b.opaque("b " + o);
    } else if (m == "bl" || m == "bla") {
        if (auto t = insn.direct_target()) b.call(ir::Operand::imm(static_cast<std::int64_t>(*t)));
        else b.opaque(m + " " + o);
    } else if (m == "bc" || m == "bclr" || m == "bcctr") {
        // Conditional branch / branch-to-LR / branch-to-CTR. Targets are
        // not statically resolvable in the general case.
        b.opaque(m + " " + o);
    } else if (m == "blr" || m == "bclr") {
        b.ret();
    } else if (m == "sc") {
        b.opaque("sc");
    } else if (m == "add" || m == "add." || m == "addo" || m == "addc") {
        if (ops.size() >= 3) b.binop(ir::OpCode::Add, reg(0), op(1), op(2));
        else b.opaque(m + " " + o);
    } else if (m == "addi" || m == "addic" || m == "addis" || m == "li" || m == "lis") {
        // addi rD, rA, simm  (li = addi rD, 0, simm; lis = addis rD, 0, simm)
        if (ops.size() >= 3) {
            b.binop(ir::OpCode::Add, reg(0), op(1), op(2));
        } else if (ops.size() == 2) {
            // li rD, simm
            b.mov(reg(0), op(1));
        } else b.opaque(m + " " + o);
    } else if (m == "sub" || m == "subf" || m == "sub." || m == "subf.") {
        if (ops.size() >= 3) b.binop(ir::OpCode::Sub, reg(0), op(1), op(2));
        else b.opaque(m + " " + o);
    } else if (m == "subfic" || m == "subi") {
        if (ops.size() >= 3) b.binop(ir::OpCode::Sub, reg(0), op(1), op(2));
        else b.opaque(m + " " + o);
    } else if (m == "mullw" || m == "mulli" || m == "mulhw" || m == "mulhw.") {
        if (ops.size() >= 3) b.binop(ir::OpCode::Mul, reg(0), op(1), op(2));
        else b.opaque(m + " " + o);
    } else if (m == "divw" || m == "divwu" || m == "divd") {
        if (ops.size() >= 3) b.binop(ir::OpCode::Div, reg(0), op(1), op(2));
        else b.opaque(m + " " + o);
    } else if (m == "and" || m == "and." || m == "andc") {
        if (ops.size() >= 3) b.binop(ir::OpCode::And, reg(0), op(1), op(2));
        else b.opaque(m + " " + o);
    } else if (m == "or" || m == "or." || m == "mr" || m == "orc") {
        if (ops.size() >= 3)      b.binop(ir::OpCode::Or, reg(0), op(1), op(2));
        else if (ops.size() == 2) b.mov(reg(0), op(1));  // mr rD, rA
        else b.opaque(m + " " + o);
    } else if (m == "xor" || m == "xor." || m == "eqv") {
        if (ops.size() >= 3) b.binop(ir::OpCode::Xor, reg(0), op(1), op(2));
        else b.opaque(m + " " + o);
    } else if (m == "slw" || m == "slw." || m == "sld") {
        if (ops.size() >= 3) b.binop(ir::OpCode::Shl, reg(0), op(1), op(2));
        else b.opaque(m + " " + o);
    } else if (m == "srw" || m == "srw." || m == "srd") {
        if (ops.size() >= 3) b.binop(ir::OpCode::Shr, reg(0), op(1), op(2));
        else b.opaque(m + " " + o);
    } else if (m == "sraw" || m == "sraw." || m == "srad") {
        if (ops.size() >= 3) b.binop(ir::OpCode::Sar, reg(0), op(1), op(2));
        else b.opaque(m + " " + o);
    } else if (m == "lwz" || m == "lwzu" || m == "lhz" || m == "lhzu" || m == "lbz" || m == "lbzu"
            || m == "ld" || m == "ldu" || m == "lwa") {
        // lwz rD, disp(rA) - the shared parser treats "(rA)" / "[rA]" as
        // memory and "disp" as displacement.
        if (ops.size() >= 2) b.load(reg(0), op(1));
        else b.opaque(m + " " + o);
    } else if (m == "stw" || m == "stwu" || m == "sth" || m == "sthu" || m == "stb" || m == "stbu"
            || m == "std" || m == "stdu") {
        if (ops.size() >= 2) b.store(op(1), op(0));
        else b.opaque(m + " " + o);
    } else if (m == "cmp" || m == "cmpl" || m == "cmpw" || m == "cmplw" || m == "cmpi" || m == "cmpli") {
        // PPC cmp takes a crf field first; we skip it and compare the next
        // two operands. cmpw rA, rB / cmpi crf, rA, simm.
        std::size_t base = (ops.size() >= 3 && ops[0].rfind("cr", 0) == 0) ? 1 : 0;
        if (ops.size() >= base + 2) {
            ir::VReg tmp = fresh();
            b.cmp(tmp, op(base), op(base + 1));
        } else b.opaque(m + " " + o);
    } else if (m == "rlwinm" || m == "rlwinm." || m == "rlwnm" || m == "slwi" || m == "srwi") {
        // Rotate-and-mask instructions - approximate as a shift.
        if (ops.size() >= 3) {
            const auto shift_op = (m == "slwi") ? ir::OpCode::Shl : ir::OpCode::Shr;
            b.binop(shift_op, reg(0), op(1), op(2));
        } else b.opaque(m + " " + o);
    } else {
        b.opaque(m + (o.empty() ? "" : (" " + o)));
    }

    auto out = std::move(b).finish();
    for (auto& i : out) i.addr = insn.address;
    return out;
}

// ---------------------------------------------------------------------------
// MIPS 32/64 lifting - basic lifter using shared operand parser.
//
// v0.0.3 coverage: addiu/addi/addu/add, subu/sub, mult/multu/div/divu,
// and/andi/or/ori/xor/xori/nor, sll/sllv/srl/srlv/sra/srav, lw/lh/lb/lhu/lbu,
// sw/sh/sb, beq/bne/bgtz/blez/bltz/bgez, j/jal/jr/jalr, lui, nop, syscall.
// MIPS register naming is $0..$31 ($zero, $at, $v0..$v1, $a0..$a3, $t0..$t9,
// $s0..$s7, $k0..$k1, $gp, $sp, $fp, $ra). Anything else falls back to Opaque.
// ---------------------------------------------------------------------------
std::vector<ir::Instruction> InstructionLifter::lift_mips(const DecodedInstruction& insn) const {
    ir::Builder b;
    const std::string& m = insn.mnemonic;
    const std::string& o = insn.op_str;
    const auto ops = split_ops(o);

    auto op = [&](std::size_t i) -> ir::Operand {
        if (i >= ops.size()) return ir::Operand::imm(0);
        return parse_operand(ops[i], [this](const std::string& s){ return this->map_reg_by_name(s); });
    };
    auto reg = [&](std::size_t i) -> ir::VReg {
        if (i >= ops.size()) return ir::INVALID_VREG;
        std::string t = ops[i];
        while (!t.empty() && (t.front() == ' ' || t.front() == '\t')) t.erase(0, 1);
        while (!t.empty() && (t.back()  == ' ' || t.back()  == '\t')) t.pop_back();
        if (t.empty() || is_imm_token(t) || is_mem_token(t)) return ir::INVALID_VREG;
        return map_reg_by_name(t);
    };
    auto fresh = [&]() -> ir::VReg {
        return map_reg(0xBEEF0000u + static_cast<std::uint32_t>(b.view().size()));
    };

    if (m == "nop" || m == "ssnop") {
        b.nop();
    } else if (m == "syscall" || m == "break") {
        b.opaque(m + " " + o);
    } else if (m == "j") {
        if (auto t = insn.direct_target()) b.branch(*t);
        else b.opaque("j " + o);
    } else if (m == "jal" || m == "jalr") {
        if (m == "jal") {
            if (auto t = insn.direct_target()) b.call(ir::Operand::imm(static_cast<std::int64_t>(*t)));
            else b.opaque("jal " + o);
        } else {
            // jalr $rs  /  jalr $rd, $rs
            if (!ops.empty()) b.call(op(ops.size() - 1));
            else b.opaque("jalr " + o);
        }
    } else if (m == "jr") {
        // jr $ra is the canonical return; jr to any other reg is an indirect
        // branch we can't resolve.
        if (!ops.empty() && (ops[0] == "$ra" || ops[0] == "$31")) b.ret();
        else b.opaque("jr " + o);
    } else if (m == "beq" || m == "bne" || m == "bgtz" || m == "blez" || m == "bltz" || m == "bgez") {
        // beq $rs, $rt, offset - Capstone resolves offset to absolute.
        // direct_target() may fail for MIPS because the target is the 3rd
        // operand, not the first; fall back to parsing the last operand.
        std::optional<std::uint64_t> t = insn.direct_target();
        if (!t && !ops.empty()) {
            const std::string& last = ops.back();
            std::string bare = last;
            if (!bare.empty() && bare.front() == '#') bare.erase(0, 1);
            if (is_imm_token(bare)) t = static_cast<std::uint64_t>(parse_imm(bare));
        }
        if (t) {
            ir::VReg cond = fresh();
            if (m == "beq" || m == "bne") {
                if (ops.size() >= 2) b.cmp(cond, op(0), op(1));
            } else {
                // bgtz/blez/bltz/bgez compare against zero.
                if (!ops.empty()) b.cmp(cond, op(0), ir::Operand::imm(0));
            }
            b.branch_cond(ir::Operand::reg(cond), *t);
        } else b.opaque(m + " " + o);
    } else if (m == "movz" || m == "movn") {
        // Conditional move - approximate as a plain mov.
        if (ops.size() >= 2) b.mov(reg(0), op(1));
        else b.opaque(m + " " + o);
    } else if (m == "lui") {
        // lui $rt, imm  - load upper 16 bits. Approximate as a plain mov
        // (we lose the shift, but the value is still tracked).
        if (ops.size() >= 2) {
            // Multiply the imm by 0x10000 to reflect the upper-half load.
            if (ops[1].find("0x") == 0 || std::isdigit(static_cast<unsigned char>(ops[1][0]))) {
                const std::int64_t v = parse_imm(ops[1]) << 16;
                b.mov(reg(0), ir::Operand::imm(v));
            } else {
                b.mov(reg(0), op(1));
            }
        } else b.opaque("lui " + o);
    } else if (m == "addiu" || m == "addi" || m == "addu" || m == "add") {
        if (ops.size() >= 3) b.binop(ir::OpCode::Add, reg(0), op(1), op(2));
        else b.opaque(m + " " + o);
    } else if (m == "subu" || m == "sub") {
        if (ops.size() >= 3) b.binop(ir::OpCode::Sub, reg(0), op(1), op(2));
        else b.opaque(m + " " + o);
    } else if (m == "mult" || m == "multu" || m == "mul") {
        if (ops.size() >= 3)      b.binop(ir::OpCode::Mul, reg(0), op(1), op(2));  // mul rD, rS, rT
        else if (ops.size() == 2) b.binop(ir::OpCode::Mul, fresh(), op(0), op(1));  // mult rS, rT (HI/LO)
        else b.opaque(m + " " + o);
    } else if (m == "div" || m == "divu") {
        if (ops.size() >= 3)      b.binop(ir::OpCode::Div, reg(0), op(1), op(2));
        else if (ops.size() == 2) b.binop(ir::OpCode::Div, fresh(), op(0), op(1));
        else b.opaque(m + " " + o);
    } else if (m == "and" || m == "andi") {
        if (ops.size() >= 3) b.binop(ir::OpCode::And, reg(0), op(1), op(2));
        else b.opaque(m + " " + o);
    } else if (m == "or" || m == "ori") {
        if (ops.size() >= 3) b.binop(ir::OpCode::Or, reg(0), op(1), op(2));
        else b.opaque(m + " " + o);
    } else if (m == "xor" || m == "xori") {
        if (ops.size() >= 3) b.binop(ir::OpCode::Xor, reg(0), op(1), op(2));
        else b.opaque(m + " " + o);
    } else if (m == "nor") {
        if (ops.size() >= 3) {
            b.binop(ir::OpCode::Or, reg(0), op(1), op(2));
            b.binop(ir::OpCode::Xor, reg(0), ir::Operand::reg(reg(0)), ir::Operand::imm(-1));
        } else b.opaque("nor " + o);
    } else if (m == "sll" || m == "sllv") {
        if (ops.size() >= 3) b.binop(ir::OpCode::Shl, reg(0), op(1), op(2));
        else b.opaque(m + " " + o);
    } else if (m == "srl" || m == "srlv") {
        if (ops.size() >= 3) b.binop(ir::OpCode::Shr, reg(0), op(1), op(2));
        else b.opaque(m + " " + o);
    } else if (m == "sra" || m == "srav") {
        if (ops.size() >= 3) b.binop(ir::OpCode::Sar, reg(0), op(1), op(2));
        else b.opaque(m + " " + o);
    } else if (m == "lw" || m == "lh" || m == "lb" || m == "lhu" || m == "lbu" || m == "lwc1" || m == "ldc1") {
        if (ops.size() >= 2) b.load(reg(0), op(1));
        else b.opaque(m + " " + o);
    } else if (m == "sw" || m == "sh" || m == "sb" || m == "swc1" || m == "sdc1") {
        if (ops.size() >= 2) b.store(op(1), op(0));
        else b.opaque(m + " " + o);
    } else if (m == "slt" || m == "slti" || m == "sltu" || m == "sltiu") {
        // slt rD, rS, rT  -> rD = (rS < rT) ? 1 : 0. Model as Cmp.
        if (ops.size() >= 3) {
            ir::VReg tmp = reg(0);
            b.cmp(tmp, op(1), op(2));
        } else b.opaque(m + " " + o);
    } else {
        b.opaque(m + (o.empty() ? "" : (" " + o)));
    }

    auto out = std::move(b).finish();
    for (auto& i : out) i.addr = insn.address;
    return out;
}

std::vector<ir::Instruction> InstructionLifter::lift_generic(const DecodedInstruction& insn) const {
    ir::Builder b;
    if (insn.mnemonic == "nop") {
        b.nop();
    } else if (insn.is_ret()) {
        b.ret();
    } else if (insn.is_call()) {
        if (auto t = insn.direct_target()) {
            b.call(ir::Operand::imm(static_cast<std::int64_t>(*t)));
        } else {
            b.opaque(insn.mnemonic + " " + insn.op_str);
        }
    } else if (insn.is_jump()) {
        if (auto t = insn.direct_target()) {
            if (insn.is_conditional_branch()) {
                ir::VReg cond = map_reg(0xBEEF0000u + static_cast<std::uint32_t>(b.view().size()));
                b.branch_cond(ir::Operand::reg(cond), *t);
            } else {
                b.branch(*t);
            }
        } else {
            b.opaque(insn.mnemonic + " " + insn.op_str);
        }
    } else {
        b.opaque(insn.mnemonic + (insn.op_str.empty() ? "" : (" " + insn.op_str)));
    }
    auto out = std::move(b).finish();
    for (auto& i : out) i.addr = insn.address;
    return out;
}

std::vector<ir::Instruction> InstructionLifter::lift_one(const DecodedInstruction& insn) const {
    reg_map_.clear();
    next_vreg_ = 1;
    switch (arch_) {
        case Arch::X86:
        case Arch::X86_64:  return lift_x86(insn);
        case Arch::AARCH64: return lift_arm64(insn);
        case Arch::ARM:     return lift_arm32(insn);
        case Arch::PPC:
        case Arch::PPC64:   return lift_ppc(insn);
        case Arch::MIPS:
        case Arch::MIPS64:  return lift_mips(insn);
        default:            return lift_generic(insn);
    }
}

ir::Function InstructionLifter::lift_function(const std::vector<DecodedInstruction>& insns,
                                              std::string name,
                                              std::uint64_t forced_entry) const {
    reg_map_.clear();
    next_vreg_ = 1;
    std::vector<ir::Instruction> all;
    all.reserve(insns.size());
    for (const auto& i : insns) {
        std::vector<ir::Instruction> lifted;
        switch (arch_) {
            case Arch::X86:
            case Arch::X86_64:  lifted = lift_x86(i); break;
            case Arch::AARCH64: lifted = lift_arm64(i); break;
            case Arch::ARM:     lifted = lift_arm32(i); break;
            case Arch::PPC:
            case Arch::PPC64:   lifted = lift_ppc(i); break;
            case Arch::MIPS:
            case Arch::MIPS64:  lifted = lift_mips(i); break;
            default:            lifted = lift_generic(i); break;
        }
        for (auto& x : lifted) all.push_back(std::move(x));
    }
    return ir::CFGBuilder::build(std::move(all), forced_entry, std::move(name));
}

}  // namespace nyx
