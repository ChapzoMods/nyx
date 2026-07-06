// =============================================================================
// Nyx - A headless decompilation engine
// Copyright (C) 2024-2026 Chapzoo  <https://github.com/Chapzoo>
// SPDX-License-Identifier: GPL-3.0-or-later
// =============================================================================
#include "nyx/decompiler/jump_table_detector.hpp"

#include "nyx/core/bytes.hpp"
#include "nyx/core/logger.hpp"

#include <algorithm>
#include <string>

namespace nyx {

JumpTableDetector::JumpTableDetector(Arch arch, Endian endian, const BinaryInfo* bin)
    : arch_(arch), endian_(endian), bin_(bin) {}

std::optional<JumpTable> JumpTableDetector::detect_x86(
    const std::vector<DecodedInstruction>& insns,
    std::size_t idx, ByteView file_bytes) const {

    const auto& jmp = insns[idx];
    if (jmp.mnemonic != "jmp") return std::nullopt;
    // Indirect jmp: op_str is a register name (no "0x" prefix and no label).
    // We treat any jmp whose op_str is NOT a hex literal as indirect.
    if (jmp.op_str.find("0x") == 0) return std::nullopt;  // direct jmp
    if (jmp.op_str.empty()) return std::nullopt;

    // Walk backwards up to 6 instructions looking for:
    //   movzx / mov index_reg, ...
    //   lea base_reg, [rip + table_offset]   OR   mov base_reg, imm
    //   jmp [base_reg + index_reg*scale]
    // We extract the table base address from the lea/mov.
    std::uint64_t table_base = 0;
    bool found_base = false;
    const std::size_t look_back = std::min<std::size_t>(6, idx);
    for (std::size_t i = 0; i < look_back; ++i) {
        const auto& prev = insns[idx - 1 - i];
        // lea reg, [rip + 0xXXXX]  - PC-relative table base.
        if (prev.mnemonic == "lea" && prev.op_str.find("rip") != std::string::npos) {
            // Extract the displacement: "[rip + 0xXXXX]" or "[rip - 0xXXXX]".
            const auto& s = prev.op_str;
            auto plus = s.find('+');
            auto minus = s.find('-');
            std::size_t pos = std::string::npos;
            int sign = 1;
            if (plus != std::string::npos && (minus == std::string::npos || plus < minus)) {
                pos = plus; sign = 1;
            } else if (minus != std::string::npos) {
                pos = minus; sign = -1;
            }
            if (pos != std::string::npos) {
                // Parse hex after the sign.
                std::string num;
                std::size_t p = pos + 1;
                while (p < s.size() && (s[p] == ' ' || s[p] == '\t')) ++p;
                if (p + 2 < s.size() && s[p] == '0' && (s[p+1] == 'x' || s[p+1] == 'X')) {
                    p += 2;
                    while (p < s.size() && std::isxdigit(static_cast<unsigned char>(s[p]))) {
                        num.push_back(s[p]); ++p;
                    }
                    if (!num.empty()) {
                        try {
                            const std::int64_t disp = sign * static_cast<std::int64_t>(
                                std::stoull(num, nullptr, 16));
                            // RIP-relative: target = next_insn_addr + disp.
                            // next_insn_addr = prev.address + prev.size.
                            table_base = prev.address + prev.size + disp;
                            found_base = true;
                            break;
                        } catch (...) {}
                    }
                }
            }
        }
        // mov reg, 0xXXXX  - direct table base.
        if (prev.mnemonic == "mov") {
            const auto& s = prev.op_str;
            auto comma = s.find(',');
            if (comma != std::string::npos) {
                std::string src = s.substr(comma + 1);
                while (!src.empty() && src.front() == ' ') src.erase(0, 1);
                if (src.find("0x") == 0) {
                    try {
                        table_base = std::stoull(src, nullptr, 0);
                        found_base = true;
                        break;
                    } catch (...) {}
                }
            }
        }
    }

    if (!found_base) return std::nullopt;

    JumpTable jt;
    jt.branch_addr = jmp.address;
    jt.table_addr  = table_base;
    jt.entry_size  = (arch_ == Arch::X86_64) ? 8 : 4;

    // Try to read entries from the binary. We read up to 256 entries, stopping
    // when we hit an address that's clearly out of the code section range.
    if (bin_ && !file_bytes.empty()) {
        // Find the file offset for table_base.
        for (const auto& s : bin_->sections) {
            if (table_base >= s.vaddr && table_base < s.vaddr + s.file_size) {
                const std::size_t off = static_cast<std::size_t>(s.file_off + (table_base - s.vaddr));
                if (off >= file_bytes.size()) break;
                const std::size_t avail = file_bytes.size() - off;
                const std::size_t max_entries = std::min<std::size_t>(256, avail / jt.entry_size);
                for (std::size_t i = 0; i < max_entries; ++i) {
                    const std::size_t eoff = off + i * jt.entry_size;
                    std::uint64_t target = 0;
                    if (jt.entry_size == 8) {
                        target = read_u64_le(file_bytes.data() + eoff);
                    } else {
                        target = read_u32_le(file_bytes.data() + eoff);
                    }
                    // Heuristic: stop if the target is not in any executable section.
                    bool in_code = false;
                    for (const auto& cs : bin_->sections) {
                        if (cs.executable && target >= cs.vaddr && target < cs.vaddr + cs.file_size) {
                            in_code = true;
                            break;
                        }
                    }
                    if (!in_code) break;
                    jt.targets.push_back(target);
                }
                break;
            }
        }
    }

    jt.entry_count = jt.targets.size();
    NYX_INFO("JumpTableDetector: x86 table @ 0x" + to_hex(jt.table_addr, 0, false)
             + " with " + std::to_string(jt.entry_count) + " entries");
    return jt;
}

std::optional<JumpTable> JumpTableDetector::detect_arm64(
    const std::vector<DecodedInstruction>& insns,
    std::size_t idx, ByteView file_bytes) const {

    const auto& br = insns[idx];
    if (br.mnemonic != "br") return std::nullopt;

    // Walk back: look for  ldr xN, [xM, xK, lsl #3]  preceded by  adr/adrp xM, ...
    if (idx < 2) return std::nullopt;
    const auto& ldr = insns[idx - 1];
    if (ldr.mnemonic != "ldr") return std::nullopt;

    // We don't fully resolve ARM64 jump tables (the ADRP+ADD pair is complex);
    // we just report the presence of the table.
    JumpTable jt;
    jt.branch_addr = br.address;
    jt.table_addr  = 0;  // unknown without full ADRP+ADD resolution
    jt.entry_size  = 8;
    jt.entry_count = 0;
    NYX_INFO("JumpTableDetector: ARM64 indirect br at 0x" + to_hex(br.address, 0, false));
    return jt;
}

std::vector<JumpTable> JumpTableDetector::detect(
    const std::vector<DecodedInstruction>& insns, ByteView file_bytes) const {

    std::vector<JumpTable> out;
    for (std::size_t i = 0; i < insns.size(); ++i) {
        std::optional<JumpTable> jt;
        switch (arch_) {
            case Arch::X86:
            case Arch::X86_64:
                jt = detect_x86(insns, i, file_bytes);
                break;
            case Arch::AARCH64:
                jt = detect_arm64(insns, i, file_bytes);
                break;
            default:
                break;
        }
        if (jt) out.push_back(*jt);
    }
    return out;
}

}  // namespace nyx
