// =============================================================================
// Nyx - A headless decompilation engine
// Copyright (C) 2024-2026 Chapzoo  <https://github.com/Chapzoo>
// SPDX-License-Identifier: GPL-3.0-or-later
// =============================================================================
#pragma once

#include "nyx/core/arch.hpp"
#include "nyx/core/bytes.hpp"
#include "nyx/core/error.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace nyx {

/// One decoded machine instruction.
struct DecodedInstruction {
    std::uint64_t address   = 0;      // virtual address
    std::uint16_t size      = 0;      // in bytes
    std::uint32_t id        = 0;      // Capstone insn id (arch-specific)
    /// Capstone mnemonic + op_str (verbatim, ASCII).
    std::string   mnemonic;
    std::string   op_str;
    /// Raw bytes of the instruction.
    std::vector<std::uint8_t> bytes;

    /// Capstone groups the insn belongs to (jump, call, ret, ...).
    /// Stored as raw uint8 to avoid leaking capstone headers everywhere.
    std::vector<std::uint8_t> groups;

    /// True for any kind of control-flow transfer (call/jmp/ret/br/int).
    [[nodiscard]] bool is_control_flow() const noexcept;
    [[nodiscard]] bool is_call()  const noexcept;
    [[nodiscard]] bool is_ret()   const noexcept;
    [[nodiscard]] bool is_jump()  const noexcept;
    [[nodiscard]] bool is_conditional_branch() const noexcept;

    /// If this is a direct branch/call, returns the absolute target address.
    /// Otherwise returns nullopt.
    [[nodiscard]] std::optional<std::uint64_t> direct_target() const noexcept;
};

/// Disassembles a byte buffer with a single Capstone session.
/// One Disassembler object is cheap to construct, but reusing it across
/// many calls saves the open/close round-trip to Capstone.
class Disassembler {
public:
    Disassembler(Arch arch, Endian endian);
    ~Disassembler();
    Disassembler(const Disassembler&)            = delete;
    Disassembler& operator=(const Disassembler&) = delete;
    Disassembler(Disassembler&&) noexcept;
    Disassembler& operator=(Disassembler&&) noexcept;

    [[nodiscard]] Arch    arch()    const noexcept { return arch_; }
    [[nodiscard]] Endian  endian()  const noexcept { return endian_; }
    [[nodiscard]] bool    valid()   const noexcept { return handle_ != 0; }

    /// Decodes up to `max_count` instructions starting at `start_address`.
    /// Stops early if `data` is exhausted or Capstone reports an error.
    /// Returns at least one instruction on success; empty vector only if
    /// `data` is empty.
    [[nodiscard]] std::vector<DecodedInstruction> decode(
        ByteView data,
        std::uint64_t start_address,
        std::size_t max_count = SIZE_MAX) const;

    /// Decodes exactly one instruction at the beginning of `data`.
    /// Returns nullopt if the bytes do not form a valid instruction.
    [[nodiscard]] std::optional<DecodedInstruction> decode_one(
        ByteView data, std::uint64_t address) const;

private:
    Arch    arch_    = Arch::Unknown;
    Endian  endian_  = Endian::Little;
    // Opaque Capstone handle - we store it as uintptr_t so the public
    // header does not need to include <capstone/capstone.h>. The .cpp
    // casts it to `csh` (Capstone's size_t alias) before every call.
    std::uintptr_t handle_ = 0;
};

}  // namespace nyx
