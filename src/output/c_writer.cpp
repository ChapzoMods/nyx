// =============================================================================
// Nyx - A headless decompilation engine
// Copyright (C) 2024-2026 Chapzoo  <https://github.com/Chapzoo>
// SPDX-License-Identifier: GPL-3.0-or-later
// =============================================================================
#include "nyx/output/c_writer.hpp"

#include "nyx/core/arch.hpp"
#include "nyx/core/bytes.hpp"
#include "nyx/decompiler/calling_convention.hpp"
#include "nyx/decompiler/type_inferer.hpp"
#include "nyx/parsers/dwarf_parser.hpp"
#include "nyx/version.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <regex>
#include <sstream>
#include <string>
#include <unordered_set>

namespace nyx::output {

namespace {

/// Returns true if `name` is a valid C identifier (letter/underscore followed
/// by letters/digits/underscores). Used to skip emitting forward declarations
/// for synthetic names that would not parse (e.g. "sub_0x1000" is fine, but
/// names containing dots or dashes are not).
[[nodiscard]] bool is_valid_c_ident(std::string_view s) noexcept {
    if (s.empty()) return false;
    const auto first = s[0];
    if (!(std::isalpha(static_cast<unsigned char>(first)) || first == '_')) return false;
    for (char c : s) {
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_')) return false;
    }
    return true;
}

/// Scans the rendered function body lines for vreg references (`v<digits>`)
/// and returns the set of unique ids found. Used to emit global `int` decls
/// so the file parses cleanly without per-function type inference.
[[nodiscard]] std::unordered_set<std::uint32_t> collect_vregs(const std::vector<DecompiledFunction>& fns) {
    static const std::regex re(R"(\bv([0-9]+)\b)");
    std::unordered_set<std::uint32_t> vregs;
    for (const auto& f : fns) {
        for (const auto& line : f.lines) {
            for (std::sregex_iterator it(line.begin(), line.end(), re), end; it != end; ++it) {
                try {
                    vregs.insert(static_cast<std::uint32_t>(std::stoul((*it)[1].str())));
                } catch (const std::exception&) {
                    // ignore malformed match
                }
            }
        }
    }
    return vregs;
}

/// Sanitises a single rendered pseudo-C line so the resulting file parses
/// with `gcc -c`. The pseudo-C renderer emits a few constructs that are
/// informative for humans but not valid C:
///
///   1. `*(void*)(expr)`  - memory deref with `void*` size hint. We swap
///      `void*` for `u8*` so the dereference yields a concrete type.
///   2. `call(...);  // indirect` - indirect call. The renderer wraps the
///      operand in nested `*(...)` loads to model the indirect transfer.
///      We replace the whole call with `call(0)` so the line still parses
///      (the preceding `// indirect call via ...` comment preserves the
///      intent for human readers).
///   3. `goto *(...);  // indirect` - computed goto through an int vreg.
///      gcc would reject `goto *(int)`. Replace with a no-op `;` so the
///      file still parses; the preceding `// indirect branch via ...`
///      comment documents the intent.
[[nodiscard]] std::string sanitize_c_line(std::string line) {
    // 1. void* deref casts -> u8* (concrete type so the deref has a value).
    static const std::regex void_ptr_re(R"(\*\((void)\*\))");
    line = std::regex_replace(line, void_ptr_re, "*(u8*)");

    // 2. Indirect call - replace the entire `call(...)` with `call(0)`.
    //    Match the call keyword followed by an open paren and anything up
    //    to the trailing `);`. The renderer always emits a trailing
    //    `// indirect` comment on these lines.
    if (line.find("// indirect") != std::string::npos) {
        static const std::regex call_indirect_re(R"(call\([^;]*\);)");
        line = std::regex_replace(line, call_indirect_re, "call(0);");
        // 3. Indirect goto - replace with a no-op statement.
        static const std::regex goto_indirect_re(R"(goto \*[^;]*;)");
        line = std::regex_replace(line, goto_indirect_re, "; /* indirect goto */");
    }

    return line;
}

/// Matches a label definition line like "  L_401000:" and returns the label
/// name (without the colon). Returns empty string if the line is not a label
/// definition.
[[nodiscard]] std::string extract_label_def(const std::string& line) {
    static const std::regex re(R"(^\s*(L_[0-9a-fA-F]+):\s*$)");
    std::smatch m;
    if (std::regex_match(line, m, re)) return m[1].str();
    return {};
}

/// Matches a goto statement and returns the referenced label name.
[[nodiscard]] std::string extract_goto_target(const std::string& line) {
    static const std::regex re(R"(goto\s+(L_[0-9a-fA-F]+)\s*;)");
    std::smatch m;
    if (std::regex_search(line, m, re)) return m[1].str();
    return {};
}

/// Rewrites a single function body so it parses cleanly. Two issues need
/// fixing per function:
///   - duplicate label definitions (the structured CFG can render the same
///     block in multiple regions); subsequent occurrences are commented out.
///   - `goto L_xxx;` references whose target label was never emitted; we
///     collect them and append stub labels before the closing brace.
void emit_function_body(std::ostream& os, const DecompiledFunction& f, bool skip_first) {
    std::unordered_set<std::string> defined_labels;
    std::unordered_set<std::string> referenced_labels;
    std::vector<std::string> emitted;

    for (std::size_t i = 0; i < f.lines.size(); ++i) {
        if (skip_first && i == 0) continue;  // caller emits its own signature
        std::string line = sanitize_c_line(f.lines[i]);

        // Track label definitions, commenting out duplicates.
        const std::string lbl = extract_label_def(line);
        if (!lbl.empty()) {
            if (defined_labels.count(lbl)) {
                emitted.push_back("// " + line + "  // duplicate label");
                continue;
            }
            defined_labels.insert(lbl);
        }

        // Track goto targets so we can emit stubs for unresolved references.
        const std::string tgt = extract_goto_target(line);
        if (!tgt.empty()) referenced_labels.insert(tgt);

        emitted.push_back(line);
    }

    // Emit stubs for any referenced labels that were never defined inside
    // the function. Append them just before the closing brace so they're
    // still in scope for the goto statements.
    std::vector<std::string> undefined;
    for (const auto& r : referenced_labels) {
        if (!defined_labels.count(r)) undefined.push_back(r);
    }
    if (!undefined.empty()) {
        // Find the closing brace (last line ending with `}`) and insert
        // the stubs before it.
        std::sort(undefined.begin(), undefined.end());
        std::vector<std::string> stubs;
        stubs.reserve(undefined.size());
        for (const auto& lbl : undefined) {
            stubs.push_back("  " + lbl + ": ;  // stub for unresolved goto");
        }
        // Insert before the last `}` line (if any).
        if (!emitted.empty() && emitted.back().find("}") != std::string::npos) {
            std::string brace = emitted.back();
            emitted.pop_back();
            for (const auto& s : stubs) emitted.push_back(s);
            emitted.push_back(brace);
        } else {
            for (const auto& s : stubs) emitted.push_back(s);
        }
    }

    for (const auto& line : emitted) os << line << "\n";
}

/// v0.1.0: Computes the C function signature for a decompiled function
/// based on the binary's calling convention and (if available) DWARF type
/// info. Returns {return_type, params_string}.
///
///   - Return type: DWARF-resolved type when available; otherwise `int`
///     (the default C return type when none is known).
///   - Parameters: when the calling convention passes args in registers,
///     emits `int param1, int param2, ...` (capped at 4 to keep signatures
///     short). When the convention is stack-only or unknown, emits `void`.
struct Signature {
    std::string return_type;
    std::string params;
};

[[nodiscard]] Signature compute_signature(const BinaryInfo& bin, const std::string& fn_name) {
    // Determine return type from DWARF if present.
    std::string ret_type = "int";  // default - most C functions return int
    if (bin.dwarf) {
        for (const auto& df : bin.dwarf->functions) {
            if (df.name == fn_name && df.type_offset != 0) {
                auto t = bin.dwarf->resolve_type_name(df.type_offset);
                if (!t.empty() && t != "void") ret_type = t;
                break;
            }
        }
    }

    // Determine parameters from the calling convention.
    auto cc = default_calling_convention(bin.arch);
    std::string params;
    if (cc.max_reg_args > 0) {
        // Cap at 4 explicit params so the signatures stay readable.
        const int n = std::min<int>(cc.max_reg_args, 4);
        for (int p = 0; p < n; ++p) {
            if (p > 0) params += ", ";
            params += "int " + param_name(static_cast<std::uint8_t>(p));
        }
    } else {
        params = "void";
    }

    return {ret_type, params};
}

}  // namespace

void write_c(std::ostream& os,
             const BinaryInfo& bin,
             const std::vector<DecompiledFunction>& functions) {
    os << "// =============================================================================\n";
    os << "// Nyx v" << VERSION_STRING << " - C decompilation output\n";
    os << "// Binary: " << bin.path << "\n";
    os << "// Format: " << to_string(bin.format)
       << "  Arch: " << arch_name(bin.arch) << "\n";
    os << "// Generated by nyx --format c. This file is intended to parse with\n";
    os << "// `gcc -c -o /dev/null file.c` (it does not link).\n";
    os << "// =============================================================================\n\n";

    // Typedefs used by the memory operand renderer (u8/u16/u32/u64).
    os << "typedef unsigned char u8;\n";
    os << "typedef unsigned short u16;\n";
    os << "typedef unsigned int u32;\n";
    os << "typedef unsigned long long u64;\n\n";

    // Helper functions referenced by the rendered pseudo-C (call, push, pop).
    // K&R-style declarations so any argument count is accepted.
    os << "// Helpers used by the decompiler IR renderer.\n";
    os << "extern void call();\n";
    os << "extern void push();\n";
    os << "extern unsigned long long pop(void);\n\n";

    // Forward declarations of every decompiled function so callers inside the
    // bodies resolve. Skip names that are not valid C identifiers. Each
    // signature is derived from the binary's calling convention and (if
    // available) DWARF type info, so the declarations match the bodies.
    os << "// Forward declarations.\n";
    for (const auto& f : functions) {
        if (!is_valid_c_ident(f.name)) continue;
        const auto sig = compute_signature(bin, f.name);
        os << sig.return_type << " " << f.name << "(" << sig.params << ");\n";
    }
    os << "\n";

    if (functions.empty()) {
        os << "// No functions were discovered.\n";
        return;
    }

    // Collect every vreg referenced anywhere in the bodies and emit a single
    // global `int` declaration block. The pseudo-C renderer uses bare `vN`
    // identifiers without per-function declarations, so without this the file
    // would fail to parse with -Werror=implicit-int.
    auto vregs = collect_vregs(functions);
    if (!vregs.empty()) {
        std::vector<std::uint32_t> sorted(vregs.begin(), vregs.end());
        std::sort(sorted.begin(), sorted.end());
        os << "// Virtual register globals (rendered as `int` for parseability).\n";
        os << "int ";
        for (std::size_t i = 0; i < sorted.size(); ++i) {
            if (i) os << ", ";
            os << "v" << sorted[i];
        }
        os << ";\n\n";
    }

    // Function bodies. The DecompiledFunction::lines vector already contains
    // the rendered function signature line and the closing brace. We replace
    // the pre-rendered signature with one derived from the calling convention
    // (so the body matches its forward declaration) and pass the rest through
    // emit_function_body, which sanitises pseudo-C constructs that gcc would
    // reject and patches up duplicate/undefined labels produced by the
    // structured CFG renderer.
    for (const auto& f : functions) {
        os << "// ---- " << f.name << " @ " << to_hex(f.entry, 0, true) << " ----\n";
        const auto sig = compute_signature(bin, f.name);
        os << sig.return_type << " " << f.name << "(" << sig.params << ") {\n";
        emit_function_body(os, f, /*skip_first=*/true);
        os << "\n";
    }
}

std::string to_c(const BinaryInfo& bin, const std::vector<DecompiledFunction>& functions) {
    std::ostringstream os;
    write_c(os, bin, functions);
    return os.str();
}

}  // namespace nyx::output
