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
// x86 / x86-64 operand lifting
// ---------------------------------------------------------------------------
ir::Operand InstructionLifter::lift_operand_x86(const DecodedInstruction& insn, int op_index) const {
    // We piggy-back on Capstone's detail, but the public DecodedInstruction
    // doesn't carry detail anymore. Re-derive operand text from op_str by
    // splitting on commas - it's good enough for v0.0.1 (the decompiler is
    // intentionally conservative and falls back to Opaque for complex cases).
    (void)insn;
    (void)op_index;
    return ir::Operand::imm(0);  // sentinel; the lifter uses mnemonic + op_str instead
}

std::vector<ir::Instruction> InstructionLifter::lift_x86(const DecodedInstruction& insn) const {
    std::vector<ir::Instruction> out;
    ir::Builder b;

    const std::string& m = insn.mnemonic;
    const std::string& o = insn.op_str;

    // Helper to split operands on top-level commas.
    auto split_ops = [](const std::string& s) {
        std::vector<std::string> parts;
        std::string cur;
        int depth = 0;
        for (char c : s) {
            if (c == '[' || c == '(') ++depth;
            else if (c == ']' || c == ')') --depth;
            if (c == ',' && depth == 0) {
                // trim
                while (!cur.empty() && (cur.back() == ' ' || cur.back() == '\t')) cur.pop_back();
                parts.push_back(cur);
                cur.clear();
                continue;
            }
            cur.push_back(c);
        }
        while (!cur.empty() && (cur.back() == ' ' || cur.back() == '\t')) cur.pop_back();
        if (!cur.empty() || !parts.empty()) parts.push_back(cur);
        return parts;
    };

    auto is_imm = [](const std::string& s) -> bool {
        if (s.empty()) return false;
        std::size_t i = 0;
        if (s.size() >= 3 && s[0] == '-' && s[1] == '0' && (s[2] == 'x' || s[2] == 'X')) i = 3;
        else if (s.size() >= 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) i = 2;
        else { return std::all_of(s.begin(), s.end(), [](char c){ return (c >= '0' && c <= '9') || c == '-'; }); }
        if (i >= s.size()) return false;
        for (; i < s.size(); ++i) {
            const char c = s[i];
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return false;
        }
        return true;
    };

    auto parse_imm = [](const std::string& s) -> std::int64_t {
        // Use stoull to handle full 64-bit unsigned values like
        // 0xffffffffffffffff without throwing out_of_range; cast to int64_t.
        try {
            return static_cast<std::int64_t>(std::stoull(s, nullptr, 0));
        } catch (const std::exception&) {
            return 0;
        }
    };

    auto is_mem = [](const std::string& s) -> bool {
        return !s.empty() && s.front() == '[';
    };

    auto parse_mem = [&](const std::string& s) -> ir::Operand {
        // [base + index*scale + disp] - simplified parser.
        // Strip brackets and spaces.
        std::string inner = s;
        if (!inner.empty() && inner.front() == '[') inner.erase(0, 1);
        if (!inner.empty() && inner.back() == ']') inner.pop_back();
        while (!inner.empty() && inner.front() == ' ') inner.erase(0, 1);
        while (!inner.empty() && inner.back() == ' ') inner.pop_back();
        if (inner.empty()) return ir::Operand::mem(ir::INVALID_VREG, 0);

        // Tokenise on + and *, respecting order.
        std::string base_reg, index_reg;
        std::uint8_t scale = 1;
        std::int64_t disp = 0;

        // Walk tokens separated by +/-.
        std::string token;
        int sign = 1;
        auto flush = [&]() {
            // trim
            while (!token.empty() && token.front() == ' ') token.erase(0, 1);
            while (!token.empty() && token.back() == ' ') token.pop_back();
            if (token.empty()) { token.clear(); return; }
            if (token == "+") { sign = 1; token.clear(); return; }
            if (token == "-") { sign = -1; token.clear(); return; }
            // Size override "byte ptr", "dword ptr", "qword ptr"... discard.
            if (token == "byte" || token == "word" || token == "dword" || token == "qword"
                || token == "ptr" || token == "short" || token == "long" || token == "tbyte"
                || token.find("ptr") != std::string::npos) {
                token.clear(); return;
            }
            // scale*N form: token like "1*eax" -> index reg
            // Find '*' separator.
            if (auto star = token.find('*'); star != std::string::npos) {
                std::string left = token.substr(0, star);
                std::string right = token.substr(star + 1);
                // trim
                while (!left.empty() && left.back() == ' ') left.pop_back();
                while (!right.empty() && right.front() == ' ') right.erase(0, 1);
                if (left.find_first_not_of("0123456789") == std::string::npos) {
                    scale = static_cast<std::uint8_t>(std::stoul(left));
                    index_reg = right;
                } else {
                    index_reg = left;
                    scale = static_cast<std::uint8_t>(std::stoul(right));
                }
                token.clear();
                return;
            }
            // Number?
            if (is_imm(token)) {
                disp += sign * parse_imm(token);
                token.clear();
                return;
            }
            // Register - use mnemonic as the vreg key (hash the name).
            if (base_reg.empty()) base_reg = token;
            else if (index_reg.empty()) { index_reg = token; }
            token.clear();
        };

        for (char c : inner) {
            if (c == '+' || c == '-') {
                flush();
                sign = (c == '+') ? 1 : -1;
                // The next token accumulates with this sign.
                continue;
            }
            token.push_back(c);
        }
        flush();

        ir::Operand op{};
        op.kind = ir::Operand::Kind::Mem;
        op.mem_base   = base_reg.empty()  ? ir::INVALID_VREG : map_reg(static_cast<std::uint32_t>(std::hash<std::string>{}(base_reg)));
        op.mem_index  = index_reg.empty() ? ir::INVALID_VREG : map_reg(static_cast<std::uint32_t>(std::hash<std::string>{}(index_reg)));
        op.mem_scale  = scale;
        op.mem_disp   = disp;
        op.mem_size   = 0;
        return op;
    };

    auto parse_operand = [&](const std::string& s) -> ir::Operand {
        std::string t = s;
        while (!t.empty() && (t.front() == ' ' || t.front() == '\t')) t.erase(0, 1);
        while (!t.empty() && (t.back()  == ' ' || t.back()  == '\t')) t.pop_back();
        if (t.empty()) return ir::Operand::imm(0);
        if (is_imm(t)) return ir::Operand::imm(parse_imm(t));
        if (is_mem(t)) return parse_mem(t);
        // Otherwise it's a register name. Map to a vreg via a stable hash.
        // Strip size overrides like "byte ptr [...]":
        if (t.find("ptr") != std::string::npos) {
            // Already a memory operand with override - strip prefix.
            auto p = t.find('[');
            if (p != std::string::npos) return parse_mem(t.substr(p));
        }
        return ir::Operand::reg(map_reg(static_cast<std::uint32_t>(std::hash<std::string>{}(t))));
    };

    const auto parts = split_ops(o);
    const auto dst = parts.empty() ? std::string{} : parts[0];
    const auto src = parts.size() >= 2 ? parts[1] : std::string{};

    if (m == "nop" || m == "nopw" || m == "int3" || m == "hlt") {
        b.nop();
    } else if (m == "mov" || m == "movq" || m == "movl" || m == "movw" || m == "movb" || m == "movzx" || m == "movsx" || m == "movsxd") {
        if (!dst.empty() && !src.empty()) {
            const auto d = parse_operand(dst);
            const auto s = parse_operand(src);
            if (d.kind == ir::Operand::Kind::Register) {
                b.mov(d.vreg, s);
            } else if (d.kind == ir::Operand::Kind::Mem) {
                b.store(d, s);
            }
        }
    } else if (m == "lea") {
        if (!dst.empty() && !src.empty()) {
            const auto d = parse_operand(dst);
            const auto s = parse_operand(src);  // s is mem in lea
            if (d.kind == ir::Operand::Kind::Register && s.kind == ir::Operand::Kind::Mem) {
                // lea reg, [base+disp] => reg = base + disp (no deref)
                ir::Operand base_op = ir::Operand::imm(s.mem_disp);
                if (s.mem_base != ir::INVALID_VREG) {
                    // Approximate as: reg = base; reg += disp.
                    b.mov(d.vreg, ir::Operand::reg(s.mem_base));
                    if (s.mem_disp != 0) b.binop(ir::OpCode::Add, d.vreg, ir::Operand::reg(d.vreg), ir::Operand::imm(s.mem_disp));
                } else {
                    b.mov(d.vreg, base_op);
                }
            }
        }
    } else if (m == "add" || m == "sub" || m == "imul" || m == "and" || m == "or" || m == "xor"
               || m == "shl" || m == "shr" || m == "sar" || m == "mul" || m == "div" || m == "idiv") {
        if (!dst.empty() && !src.empty()) {
            auto d = parse_operand(dst);
            auto s = parse_operand(src);
            ir::OpCode op = ir::OpCode::Add;
            if (m == "sub") op = ir::OpCode::Sub;
            else if (m == "mul" || m == "imul") op = ir::OpCode::Mul;
            else if (m == "div" || m == "idiv") op = ir::OpCode::Div;
            else if (m == "and") op = ir::OpCode::And;
            else if (m == "or")  op = ir::OpCode::Or;
            else if (m == "xor") op = ir::OpCode::Xor;
            else if (m == "shl" || m == "sal") op = ir::OpCode::Shl;
            else if (m == "shr") op = ir::OpCode::Shr;
            else if (m == "sar") op = ir::OpCode::Sar;
            if (d.kind == ir::Operand::Kind::Register) {
                b.binop(op, d.vreg, ir::Operand::reg(d.vreg), s);
            } else if (d.kind == ir::Operand::Kind::Mem) {
                // mem op= src -> load, op, store
                ir::VReg tmp = map_reg(0xDEAD0000u + static_cast<std::uint32_t>(b.view().size()));
                b.load(tmp, d);
                b.binop(op, tmp, ir::Operand::reg(tmp), s);
                b.store(d, ir::Operand::reg(tmp));
            }
        }
    } else if (m == "inc" || m == "dec" || m == "neg" || m == "not") {
        if (!dst.empty()) {
            auto d = parse_operand(dst);
            if (d.kind == ir::Operand::Kind::Register) {
                if (m == "inc")      b.binop(ir::OpCode::Add, d.vreg, ir::Operand::reg(d.vreg), ir::Operand::imm(1));
                else if (m == "dec") b.binop(ir::OpCode::Sub, d.vreg, ir::Operand::reg(d.vreg), ir::Operand::imm(1));
                else if (m == "neg") b.binop(ir::OpCode::Sub, d.vreg, ir::Operand::imm(0), ir::Operand::reg(d.vreg));
                else                 b.binop(ir::OpCode::Xor, d.vreg, ir::Operand::reg(d.vreg), ir::Operand::imm(-1));
            }
        }
    } else if (m == "push") {
        if (!dst.empty()) b.push(parse_operand(dst));
        else b.nop();
        // Push opcode not in builder; use opaque fallback.
        b.opaque("push " + o);
    } else if (m == "pop") {
        b.opaque("pop " + o);
    } else if (m == "cmp" || m == "test") {
        if (!dst.empty() && !src.empty()) {
            auto d = parse_operand(dst);
            auto s = parse_operand(src);
            ir::VReg tmp = map_reg(0xBEEF0000u + static_cast<std::uint32_t>(b.view().size()));
            b.cmp(tmp, d, s);
        }
    } else if (m == "jmp") {
        if (auto t = insn.direct_target()) b.branch(*t);
        else b.opaque("jmp " + o);
    } else if (m.rfind("j", 0) == 0 && m != "jmp" && m.size() >= 2 && m[0] == 'j') {
        // Conditional jump: je, jne, jz, jnz, jl, jle, jg, jge, ja, jb, ...
        if (auto t = insn.direct_target()) {
            ir::VReg cond = map_reg(0xBEEF0000u + static_cast<std::uint32_t>(b.view().size()));
            b.branch_cond(ir::Operand::reg(cond), *t);
        } else {
            b.opaque(m + " " + o);
        }
    } else if (m == "call") {
        if (auto t = insn.direct_target()) b.call(ir::Operand::imm(static_cast<std::int64_t>(*t)));
        else if (!o.empty()) {
            // Indirect call through register/memory/symbol.
            if (o.find("0x") == 0) {
                b.call(ir::Operand::imm(parse_imm(o)));
            } else if (o.find('_') != std::string::npos || o.find('@') != std::string::npos) {
                b.call(ir::Operand::sym(o));
            } else {
                b.call(parse_operand(o));
            }
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

    out = std::move(b).finish();
    for (auto& i : out) i.addr = insn.address;
    return out;
}

// ---------------------------------------------------------------------------
// ARM64 / AArch64 lifting - real lifter for the common instruction set.
//
// v0.0.2 coverage: mov, movz, movk, add, sub, mul, and, or, eor, lsl, lsr,
// asr, ldr, ldp, str, stp, cmp, tst, b, bl, b.cond, cbz, cbnz, ret, nop.
// Anything else falls back to Opaque.
// ---------------------------------------------------------------------------
namespace {

/// Splits a Capstone op_str like "x0, x1, #0x10" into trimmed tokens,
/// respecting that memory operands `[x0, #0x10]!` must not be split on
/// the commas inside the brackets.
std::vector<std::string> split_arm64_ops(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    int bracket_depth = 0;
    for (char c : s) {
        if (c == '[') ++bracket_depth;
        else if (c == ']') --bracket_depth;
        if (c == ',' && bracket_depth == 0) {
            // trim
            while (!cur.empty() && (cur.front() == ' ' || cur.front() == '\t')) cur.erase(0, 1);
            while (!cur.empty() && (cur.back()  == ' ' || cur.back()  == '\t')) cur.pop_back();
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    while (!cur.empty() && (cur.front() == ' ' || cur.front() == '\t')) cur.erase(0, 1);
    while (!cur.empty() && (cur.back()  == ' ' || cur.back()  == '\t')) cur.pop_back();
    if (!cur.empty()) out.push_back(cur);
    return out;
}

/// Parses an ARM64 operand string into an IR Operand.
/// Supported forms:
///   "x0", "w0", "sp", "xzr", "wzr"          -> register vreg
///   "#0x10", "#16", "0x10", "16", "-1"       -> immediate
///   "[x0]", "[x0, #0x10]", "[x0, x1, lsl #2]" -> memory
///   "#0x40" with `force_imm`                 -> immediate even without '#'
ir::Operand parse_arm64_operand(const std::string& s, InstructionLifter::RegMapper map_reg) {
    std::string t = s;
    while (!t.empty() && (t.front() == ' ' || t.front() == '\t')) t.erase(0, 1);
    while (!t.empty() && (t.back()  == ' ' || t.back()  == '\t')) t.pop_back();
    if (t.empty()) return ir::Operand::imm(0);

    // Memory operand: starts with '['.
    if (t.front() == '[') {
        // Strip the brackets and any pre-index '!' marker.
        std::string inner = t;
        if (!inner.empty() && inner.front() == '[') inner.erase(0, 1);
        if (!inner.empty() && inner.back()  == ']') inner.pop_back();
        if (!inner.empty() && inner.back()  == '!') inner.pop_back();
        // Trim again.
        while (!inner.empty() && (inner.front() == ' ' || inner.front() == '\t')) inner.erase(0, 1);
        while (!inner.empty() && (inner.back()  == ' ' || inner.back()  == '\t')) inner.pop_back();

        // Split inner on commas at top level (no nested brackets in ARM64
        // memory operands, but be defensive).
        std::vector<std::string> parts;
        std::string cur;
        for (char c : inner) {
            if (c == ',') {
                while (!cur.empty() && (cur.back() == ' ' || cur.back() == '\t')) cur.pop_back();
                if (!cur.empty()) parts.push_back(cur);
                cur.clear();
            } else {
                cur.push_back(c);
            }
        }
        while (!cur.empty() && (cur.back() == ' ' || cur.back() == '\t')) cur.pop_back();
        if (!cur.empty()) parts.push_back(cur);

        ir::Operand op{};
        op.kind = ir::Operand::Kind::Mem;
        op.mem_base  = ir::INVALID_VREG;
        op.mem_index = ir::INVALID_VREG;
        op.mem_scale = 1;
        op.mem_disp  = 0;
        op.mem_size  = 0;

        for (const auto& p : parts) {
            std::string pp = p;
            while (!pp.empty() && (pp.front() == ' ' || pp.front() == '\t')) pp.erase(0, 1);
            if (pp.empty()) continue;

            // "lsl #N" or "lsl N" -> scale = 1 << N for shifted-index forms.
            if (pp.rfind("lsl", 0) == 0) {
                std::string num = pp.substr(3);
                while (!num.empty() && (num.front() == ' ' || num.front() == '#' || num.front() == '\t')) num.erase(0, 1);
                try {
                    const int shift = static_cast<int>(std::stoll(num, nullptr, 0));
                    if (shift >= 0 && shift < 8) op.mem_scale = static_cast<std::uint8_t>(1u << shift);
                } catch (...) {}
                continue;
            }
            // Immediate displacement: "#0x10" / "#16" / "0x10".
            if (pp.front() == '#' || std::isdigit(static_cast<unsigned char>(pp.front())) || pp.front() == '-' || pp.front() == '+') {
                std::string num = pp;
                if (!num.empty() && num.front() == '#') num.erase(0, 1);
                try { op.mem_disp = static_cast<std::int64_t>(std::stoll(num, nullptr, 0)); } catch (...) {}
                continue;
            }
            // Otherwise it's a register. First one wins as base; second as index.
            const auto v = map_reg(pp);
            if (op.mem_base == ir::INVALID_VREG) op.mem_base = v;
            else if (op.mem_index == ir::INVALID_VREG) op.mem_index = v;
        }
        return op;
    }

    // Immediate: '#' prefix or pure number.
    if (t.front() == '#' || std::isdigit(static_cast<unsigned char>(t.front())) || t.front() == '-' || t.front() == '+') {
        std::string num = t;
        if (!num.empty() && num.front() == '#') num.erase(0, 1);
        try {
            return ir::Operand::imm(static_cast<std::int64_t>(std::stoull(num, nullptr, 0)));
        } catch (...) {
            return ir::Operand::imm(0);
        }
    }

    // Otherwise: register. Map by name (stable hash).
    return ir::Operand::reg(map_reg(t));
}

}  // namespace

std::vector<ir::Instruction> InstructionLifter::lift_arm64(const DecodedInstruction& insn) const {
    ir::Builder b;
    const std::string& m = insn.mnemonic;
    const std::string& o = insn.op_str;
    const auto ops = split_arm64_ops(o);

    // Helper: parse operand i, or return an imm(0) sentinel if missing.
    auto op = [&](std::size_t i) -> ir::Operand {
        if (i >= ops.size()) return ir::Operand::imm(0);
        return parse_arm64_operand(ops[i], [this](const std::string& s){ return this->map_reg_by_name(s); });
    };
    auto reg = [&](std::size_t i) -> ir::VReg {
        if (i >= ops.size()) return ir::INVALID_VREG;
        return map_reg_by_name(ops[i]);
    };

    // Note: map_reg_by_name is a private helper added below; it parses
    // register names like "x0"/"w5"/"sp"/"xzr" and returns a stable VReg.

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
            // Indirect call through register: blr xN.
            b.call(op(0));
        } else {
            b.opaque(m + " " + o);
        }
    } else if (m.rfind("b.", 0) == 0 || m == "cbz" || m == "cbnz" || m == "tbz" || m == "tbnz") {
        // Conditional branch. Capstone already resolved the absolute target.
        if (auto t = insn.direct_target()) {
            // For cbz/cbnz/tbz/tbnz the first operand is the reg being tested;
            // for b.cond there is no test operand.
            ir::VReg cond = map_reg(0xBEEF0000u + static_cast<std::uint32_t>(b.view().size()));
            if (m == "cbz" || m == "cbnz" || m == "tbz" || m == "tbnz") {
                // Approximate: cond = (reg != 0) so the BranchCond reads it.
                const ir::VReg tested = reg(0);
                b.cmp(cond, ir::Operand::reg(tested), ir::Operand::imm(0));
            }
            b.branch_cond(ir::Operand::reg(cond), *t);
        } else {
            b.opaque(m + " " + o);
        }
    } else if (m == "mov" || m == "movz" || m == "orr" || m == "movn") {
        // mov dst, src   OR   movz dst, #imm   (treat the same for v0.0.2)
        if (ops.size() >= 2) {
            const ir::VReg d = reg(0);
            const ir::Operand s = op(1);
            b.mov(d, s);
        } else b.opaque(m + " " + o);
    } else if (m == "movk") {
        // movk shifts and inserts an immediate half-word; without bitfield
        // support in the IR, we approximate as a plain mov to keep the
        // destination live.
        if (ops.size() >= 2) b.mov(reg(0), op(1));
        else b.opaque(m + " " + o);
    } else if (m == "add" || m == "sub" || m == "mul" || m == "and" || m == "or" || m == "eor"
            || m == "lsl" || m == "lsr" || m == "asr" || m == "udiv" || m == "sdiv") {
        if (ops.size() >= 3) {
            ir::OpCode op_code = ir::OpCode::Add;
            if (m == "sub")  op_code = ir::OpCode::Sub;
            else if (m == "mul")  op_code = ir::OpCode::Mul;
            else if (m == "udiv" || m == "sdiv") op_code = ir::OpCode::Div;
            else if (m == "and")  op_code = ir::OpCode::And;
            else if (m == "or")   op_code = ir::OpCode::Or;
            else if (m == "eor")  op_code = ir::OpCode::Xor;
            else if (m == "lsl")  op_code = ir::OpCode::Shl;
            else if (m == "lsr")  op_code = ir::OpCode::Shr;
            else if (m == "asr")  op_code = ir::OpCode::Sar;
            b.binop(op_code, reg(0), op(1), op(2));
        } else b.opaque(m + " " + o);
    } else if (m == "neg" || m == "mvn" || m == "negs") {
        // neg dst, src  =>  dst = 0 - src
        if (ops.size() >= 2) {
            if (m == "mvn") {
                b.binop(ir::OpCode::Xor, reg(0), op(1), ir::Operand::imm(-1));
            } else {
                b.binop(ir::OpCode::Sub, reg(0), ir::Operand::imm(0), op(1));
            }
        } else b.opaque(m + " " + o);
    } else if (m == "ldr" || m == "ldrsw" || m == "ldrb" || m == "ldrh" || m == "ldur") {
        // ldr dst, [src{, #disp}]
        if (ops.size() >= 2) {
            const ir::VReg d = reg(0);
            const ir::Operand addr = op(1);
            b.load(d, addr);
        } else b.opaque(m + " " + o);
    } else if (m == "str" || m == "strb" || m == "strh" || m == "stur") {
        // str src, [dst{, #disp}]
        if (ops.size() >= 2) {
            const ir::Operand v = op(0);
            const ir::Operand addr = op(1);
            b.store(addr, v);
        } else b.opaque(m + " " + o);
    } else if (m == "ldp") {
        // ldp x0, x1, [x2]  =>  load both, second into a fresh vreg.
        if (ops.size() >= 3) {
            b.load(reg(0), op(2));
            // Approximate the second register by emitting a load with a
            // 8-byte offset, so the CFG/pseudo-C reflects both reads.
            ir::Operand addr2 = op(2);
            if (addr2.kind == ir::Operand::Kind::Mem) {
                addr2.mem_disp += 8;
                b.load(reg(1), addr2);
            } else {
                b.opaque(m + " " + o);
            }
        } else b.opaque(m + " " + o);
    } else if (m == "stp") {
        // stp x0, x1, [x2]  =>  store both.
        if (ops.size() >= 3) {
            b.store(op(2), op(0));
            ir::Operand addr2 = op(2);
            if (addr2.kind == ir::Operand::Kind::Mem) {
                addr2.mem_disp += 8;
                b.store(addr2, op(1));
            } else {
                b.opaque(m + " " + o);
            }
        } else b.opaque(m + " " + o);
    } else if (m == "cmp" || m == "cmn" || m == "tst") {
        if (ops.size() >= 2) {
            const ir::VReg tmp = map_reg(0xBEEF0000u + static_cast<std::uint32_t>(b.view().size()));
            // cmp = sub-with-no-result; cmn = add-with-no-result; tst = and-with-no-result.
            // We model all three as a Cmp node so the pseudo-C if/else can
            // pick the result up.
            b.cmp(tmp, op(0), op(1));
        } else b.opaque(m + " " + o);
    } else if (m == "adrp" || m == "adr") {
        // adrp dst, #page  - we don't model PC-relative addressing precisely.
        if (ops.size() >= 2) b.mov(reg(0), op(1));
        else b.opaque(m + " " + o);
    } else if (m == "br" || m == "braa" || m == "brab") {
        // Indirect branch through register - model as Branch to an Opaque
        // since we can't statically resolve the target.
        b.opaque(m + " " + o);
    } else {
        b.opaque(m + (o.empty() ? "" : (" " + o)));
    }

    auto out = std::move(b).finish();
    for (auto& i : out) i.addr = insn.address;
    return out;
}

std::vector<ir::Instruction> InstructionLifter::lift_arm32(const DecodedInstruction& insn) const {
    ir::Builder b;
    const std::string& m = insn.mnemonic;
    const std::string& o = insn.op_str;

    // ARMv7 has hundreds of forms; v0.0.2 keeps the conservative fallback
    // and only handles the control-flow mnemonics that Capstone emits in
    // a stable way.
    if (m == "nop" || m == "wfi" || m == "wfe") {
        b.nop();
    } else if (m == "bx" || m == "blx" || m == "pop" || m == "push") {
        b.opaque(m + " " + o);
    } else if (m == "ret") {
        b.ret();
    } else if (m == "b" || m == "bl" || m == "b.eq" || m == "b.ne" || m == "b.gt" || m == "b.lt"
            || m == "b.ge" || m == "b.le" || m == "cbz" || m == "cbnz") {
        if (auto t = insn.direct_target()) {
            if (m == "b" || m == "bl") {
                if (m == "bl") b.call(ir::Operand::imm(static_cast<std::int64_t>(*t)));
                else           b.branch(*t);
            } else {
                ir::VReg cond = map_reg(0xBEEF0000u + static_cast<std::uint32_t>(b.view().size()));
                b.branch_cond(ir::Operand::reg(cond), *t);
            }
        } else {
            b.opaque(m + " " + o);
        }
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
            default:            lifted = lift_generic(i); break;
        }
        for (auto& x : lifted) all.push_back(std::move(x));
    }
    return ir::CFGBuilder::build(std::move(all), forced_entry, std::move(name));
}

}  // namespace nyx
