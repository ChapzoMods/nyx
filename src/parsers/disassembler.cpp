// =============================================================================
// Nyx - A headless decompilation engine
// Copyright (C) 2024-2026 Chapzoo  <https://github.com/Chapzoo>
// SPDX-License-Identifier: GPL-3.0-or-later
// =============================================================================
#include "nyx/parsers/disassembler.hpp"

#include "nyx/core/arch.hpp"
#include "nyx/core/error.hpp"
#include "nyx/core/logger.hpp"

#include <capstone/capstone.h>

#include <algorithm>
#include <cstring>
#include <vector>

namespace nyx {

namespace {

// Group ids we care about. We translate them to a small enum to avoid
// leaking capstone headers into the rest of the codebase.
enum class InsnGroup : std::uint8_t {
    Invalid = 0,
    Jump,
    Call,
    Ret,
    Int,
    BranchRelative,
};

InsnGroup capstone_group_to_nyx(unsigned int g) {
    // Capstone group ids are arch-specific. We only care about the generic
    // jump/call/ret/int set which is identical across architectures.
    switch (g) {
        case CS_GRP_JUMP:           return InsnGroup::Jump;
        case CS_GRP_CALL:           return InsnGroup::Call;
        case CS_GRP_RET:            return InsnGroup::Ret;
        case CS_GRP_INT:            return InsnGroup::Int;
        case CS_GRP_IRET:           return InsnGroup::Ret;
        case CS_GRP_BRANCH_RELATIVE:return InsnGroup::BranchRelative;
        default:                    return InsnGroup::Invalid;
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// DecodedInstruction
// ---------------------------------------------------------------------------
bool DecodedInstruction::is_call() const noexcept {
    return std::any_of(groups.begin(), groups.end(),
                       [](std::uint8_t g){ return static_cast<InsnGroup>(g) == InsnGroup::Call; });
}
bool DecodedInstruction::is_ret() const noexcept {
    return std::any_of(groups.begin(), groups.end(),
                       [](std::uint8_t g){ return static_cast<InsnGroup>(g) == InsnGroup::Ret; });
}
bool DecodedInstruction::is_jump() const noexcept {
    return std::any_of(groups.begin(), groups.end(),
                       [](std::uint8_t g){ return static_cast<InsnGroup>(g) == InsnGroup::Jump; });
}
bool DecodedInstruction::is_control_flow() const noexcept {
    return is_call() || is_ret() || is_jump();
}
bool DecodedInstruction::is_conditional_branch() const noexcept {
    if (!is_jump()) return false;
    // Capstone marks conditional branches via the operand string starting
    // with a 'j' followed by something other than 'mp'. Heuristic but stable.
    if (mnemonic.size() < 2) return false;
    if (mnemonic[0] != 'j') return false;
    if (mnemonic == "jmp") return false;
    return true;
}

std::optional<std::uint64_t> DecodedInstruction::direct_target() const noexcept {
    if (!is_call() && !is_jump()) return std::nullopt;
    // A direct branch has an op_str that is a pure hex/decimal address
    // (Capstone already resolves relative->absolute in detail mode).
    // We accept patterns like "0x...", "0x... <symbol>", or "#0x...".
    if (op_str.empty()) return std::nullopt;

    std::size_t i = 0;
    // skip leading '#' or whitespace
    while (i < op_str.size() && (op_str[i] == '#' || op_str[i] == ' ' || op_str[i] == '\t')) ++i;
    if (i + 2 >= op_str.size()) return std::nullopt;
    if (op_str[i] != '0' || (op_str[i + 1] != 'x' && op_str[i + 1] != 'X')) return std::nullopt;
    i += 2;

    std::uint64_t v = 0;
    bool any = false;
    while (i < op_str.size()) {
        const char c = op_str[i];
        if (c >= '0' && c <= '9') { v = (v << 4) | static_cast<std::uint64_t>(c - '0'); any = true; }
        else if (c >= 'a' && c <= 'f') { v = (v << 4) | static_cast<std::uint64_t>(c - 'a' + 10); any = true; }
        else if (c >= 'A' && c <= 'F') { v = (v << 4) | static_cast<std::uint64_t>(c - 'A' + 10); any = true; }
        else break;
        ++i;
    }
    if (!any) return std::nullopt;
    // Tail: optional " <symbol>" - we ignore it.
    return v;
}

// ---------------------------------------------------------------------------
// Disassembler
// ---------------------------------------------------------------------------
Disassembler::Disassembler(Arch arch, Endian endian)
    : arch_(arch), endian_(endian) {
    const auto cfg = capstone_map::for_arch(arch, endian);
    if (!cfg) {
        NYX_WARN("Disassembler: unsupported arch/endian combo");
        return;
    }
    csh h = 0;
    const cs_err err = cs_open(static_cast<cs_arch>(cfg->cs_arch),
                                static_cast<cs_mode>(cfg->cs_mode),
                                &h);
    if (err != CS_ERR_OK || h == 0) {
        NYX_WARN("Disassembler: cs_open failed: " + std::string(cs_strerror(err)));
        return;
    }
    // Request detail mode - we need groups + operands.
    cs_option(h, CS_OPT_DETAIL, CS_OPT_ON);
    handle_ = static_cast<std::uintptr_t>(h);
}

Disassembler::~Disassembler() {
    if (handle_) {
        csh h = static_cast<csh>(handle_);
        cs_close(&h);
        handle_ = 0;
    }
}

Disassembler::Disassembler(Disassembler&& o) noexcept
    : arch_(o.arch_), endian_(o.endian_), handle_(o.handle_) {
    o.handle_ = 0;
}

Disassembler& Disassembler::operator=(Disassembler&& o) noexcept {
    if (this != &o) {
        if (handle_) {
            csh h = static_cast<csh>(handle_);
            cs_close(&h);
        }
        arch_   = o.arch_;
        endian_ = o.endian_;
        handle_ = o.handle_;
        o.handle_ = 0;
    }
    return *this;
}

std::vector<DecodedInstruction> Disassembler::decode(
    ByteView data, std::uint64_t start_address, std::size_t max_count) const {

    std::vector<DecodedInstruction> out;
    if (!handle_ || data.empty() || max_count == 0) return out;

    cs_insn* insns = nullptr;
    const csh h = static_cast<csh>(handle_);
    // Capstone v5 cs_disasm signature is (handle, code, code_size, address, count, **insn).
    const std::size_t decoded = cs_disasm(
        h,
        data.data(),
        data.size(),
        start_address,
        max_count == SIZE_MAX ? 0 : max_count,
        &insns);

    out.reserve(decoded);
    for (std::size_t i = 0; i < decoded; ++i) {
        const cs_insn& in = insns[i];
        DecodedInstruction d{};
        d.address = in.address;
        d.size    = static_cast<std::uint16_t>(in.size);
        d.id      = in.id;
        d.mnemonic.assign(in.mnemonic);
        d.op_str.assign(in.op_str);
        d.bytes.assign(in.bytes, in.bytes + in.size);

        if (in.detail) {
            const auto& grps = in.detail->groups;
            for (std::size_t g = 0; g < in.detail->groups_count; ++g) {
                const auto ng = capstone_group_to_nyx(static_cast<unsigned int>(grps[g]));
                if (ng != InsnGroup::Invalid) {
                    d.groups.push_back(static_cast<std::uint8_t>(ng));
                }
            }
        }
        out.push_back(std::move(d));
    }
    if (insns) cs_free(insns, decoded);
    return out;
}

std::optional<DecodedInstruction> Disassembler::decode_one(ByteView data, std::uint64_t address) const {
    if (!handle_ || data.empty()) return std::nullopt;
    const csh h = static_cast<csh>(handle_);
    cs_insn* in = cs_malloc(h);
    if (!in) return std::nullopt;

    const uint8_t* code = data.data();
    std::size_t code_size = data.size();
    std::uint64_t addr = address;

    DecodedInstruction out;
    if (cs_disasm_iter(h, &code, &code_size, &addr, in)) {
        out.address = in->address;
        out.size    = static_cast<std::uint16_t>(in->size);
        out.id      = in->id;
        out.mnemonic.assign(in->mnemonic);
        out.op_str.assign(in->op_str);
        out.bytes.assign(in->bytes, in->bytes + in->size);
        if (in->detail) {
            const auto& grps = in->detail->groups;
            for (std::size_t g = 0; g < in->detail->groups_count; ++g) {
                const auto ng = capstone_group_to_nyx(static_cast<unsigned int>(grps[g]));
                if (ng != InsnGroup::Invalid) {
                    out.groups.push_back(static_cast<std::uint8_t>(ng));
                }
            }
        }
        cs_free(in, 1);
        return out;
    }
    cs_free(in, 1);
    return std::nullopt;
}

}  // namespace nyx
