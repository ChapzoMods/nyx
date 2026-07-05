// =============================================================================
// Nyx - A headless decompilation engine
// Copyright (C) 2024-2026 Chapzoo  <https://github.com/Chapzoo>
// SPDX-License-Identifier: GPL-3.0-or-later
// =============================================================================
#include "nyx/decompiler/decompiler.hpp"
#include "nyx/decompiler/pseudo_c.hpp"

#include "nyx/core/bytes.hpp"
#include "nyx/core/logger.hpp"

#include <algorithm>
#include <sstream>
#include <string>

namespace nyx {

Decompiler::Decompiler() : opts_{} {}
Decompiler::Decompiler(Options opts) : opts_(opts) {}

std::vector<DecompiledFunction> Decompiler::decompile(const BinaryInfo& bin) const {
    std::vector<DecompiledFunction> out;

    Disassembler dis(bin.arch, bin.endian);
    if (!dis.valid()) {
        NYX_WARN("decompile: no disassembler for arch " + std::string(arch_name(bin.arch)));
        return out;
    }

    // Collect candidate functions: every Function symbol first.
    std::vector<const Symbol*> funcs;
    for (const auto& s : bin.symbols) {
        if (s.kind == Symbol::Kind::Function && !s.imported) funcs.push_back(&s);
    }
    std::sort(funcs.begin(), funcs.end(),
              [](const Symbol* a, const Symbol* b){ return a->value < b->value; });

    InstructionLifter lifter(bin.arch, bin.endian);

    auto locate_bytes = [&](std::uint64_t addr) -> ByteView {
        for (const auto& s : bin.sections) {
            if (!s.executable) continue;
            if (addr >= s.vaddr && addr < s.vaddr + s.file_size) {
                // bytes are at file_off + (addr - vaddr)
                // We don't have the file bytes here, so we need them passed in.
                return {};
            }
        }
        return {};
    };
    (void)locate_bytes;

    // We need the raw file bytes to decode. Re-load them from `bin.path`.
    std::optional<ByteBuffer> file_buf;
    if (!bin.path.empty()) file_buf = ByteBuffer::from_file(bin.path);

    auto bytes_at = [&](std::uint64_t addr, std::size_t max) -> ByteView {
        if (!file_buf) return {};
        for (const auto& s : bin.sections) {
            if (!s.executable) continue;
            if (addr >= s.vaddr && addr < s.vaddr + s.file_size) {
                const std::size_t off = static_cast<std::size_t>(s.file_off + (addr - s.vaddr));
                if (off >= file_buf->size()) return {};
                const std::size_t avail = file_buf->size() - off;
                const std::size_t n = std::min(avail, max);
                return ByteView{file_buf->data() + off, n};
            }
        }
        return {};
    };

    for (const Symbol* sym : funcs) {
        if (sym->size == 0 && opts_.linear_sweep_fallback == false) continue;
        const std::size_t max_bytes = opts_.max_insns_per_function * 16;  // generous upper bound
        ByteView bytes = bytes_at(sym->value, max_bytes);
        if (bytes.empty()) continue;

        // Decode up to max_insns_per_function OR until ret/jmp out of section.
        // Wrap in try/catch so a single bad instruction doesn't kill the whole run.
        std::vector<DecodedInstruction> insns;
        ir::Function fn;
        bool ok = true;
        try {
            insns = dis.decode(bytes, sym->value, opts_.max_insns_per_function);
            if (insns.empty()) ok = false;
            else fn = lifter.lift_function(insns, sym->name, sym->value);
        } catch (const std::exception& e) {
            NYX_WARN(std::string("decompile: function ") + sym->name + " failed: " + e.what());
            ok = false;
        }
        if (!ok) continue;

        DecompiledFunction df;
        df.name        = fn.name;
        df.entry       = fn.entry;
        df.block_count = fn.block_count();
        df.insn_count  = fn.instruction_count();

        // Render as pseudo-C lines.
        std::string body = render_pseudo_c(fn);
        std::istringstream iss(body);
        std::string line;
        while (std::getline(iss, line)) df.lines.push_back(line);
        out.push_back(std::move(df));
    }

    // Linear sweep fallback when no function symbols were found.
    if (out.empty() && opts_.linear_sweep_fallback) {
        const Section* text = bin.code_section();
        if (text && file_buf) {
            const std::size_t off = static_cast<std::size_t>(text->file_off);
            if (off < file_buf->size()) {
                const std::size_t n = std::min(text->file_size, file_buf->size() - off);
                ByteView bytes{file_buf->data() + off, n};
                auto insns = dis.decode(bytes, text->vaddr, opts_.max_insns_per_function);
                if (!insns.empty()) {
                    ir::Function fn = lifter.lift_function(insns, "linear_sweep", text->vaddr);
                    DecompiledFunction df;
                    df.name        = fn.name;
                    df.entry       = fn.entry;
                    df.block_count = fn.block_count();
                    df.insn_count  = fn.instruction_count();
                    std::string body = render_pseudo_c(fn);
                    std::istringstream iss(body);
                    std::string line;
                    while (std::getline(iss, line)) df.lines.push_back(line);
                    out.push_back(std::move(df));
                }
            }
        }
    }

    return out;
}

DecompiledFunction Decompiler::decompile_range(
    const BinaryInfo& bin,
    std::uint64_t start_addr,
    ByteView bytes,
    std::string name) const {

    Disassembler dis(bin.arch, bin.endian);
    InstructionLifter lifter(bin.arch, bin.endian);

    auto insns = dis.decode(bytes, start_addr, opts_.max_insns_per_function);
    ir::Function fn = lifter.lift_function(insns, std::move(name), start_addr);

    DecompiledFunction df;
    df.name        = fn.name;
    df.entry       = fn.entry;
    df.block_count = fn.block_count();
    df.insn_count  = fn.instruction_count();
    std::string body = render_pseudo_c(fn);
    std::istringstream iss(body);
    std::string line;
    while (std::getline(iss, line)) df.lines.push_back(line);
    return df;
}

}  // namespace nyx
