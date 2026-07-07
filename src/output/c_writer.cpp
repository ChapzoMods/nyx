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

/// Returns true if `line` looks like a function signature line emitted by
/// the pseudo-C renderer (e.g. `void add(void) {`). We detect this by
/// checking that the line ends with `{` (after trimming trailing whitespace)
/// and contains the function's name. The C writer emits its own calling-
/// convention-derived signature, so these renderer lines must be skipped to
/// avoid a double declaration and the resulting unbalanced braces.
[[nodiscard]] bool is_signature_line(const std::string& line, const std::string& fn_name) {
    if (fn_name.empty()) return false;
    const auto last_non_ws = line.find_last_not_of(" \t\r\n");
    if (last_non_ws == std::string::npos) return false;
    if (line[last_non_ws] != '{') return false;
    return line.find(fn_name) != std::string::npos;
}

/// Returns true if `line` is the function-level closing brace emitted by
/// the pseudo-C renderer. The renderer emits this as `}` with NO leading
/// whitespace (e.g. `os << "}\n";`), so we only match unindented brace
/// lines. Nested closing braces (e.g. `  }` from an if/while block) are
/// indented and must be preserved so the body's internal braces stay
/// balanced. The C writer emits its own closing brace at the end of every
/// function body, so this top-level brace must be skipped.
[[nodiscard]] bool is_function_closing_brace_line(const std::string& line) {
    if (line.empty() || line[0] != '}') return false;
    for (std::size_t i = 1; i < line.size(); ++i) {
        if (!std::isspace(static_cast<unsigned char>(line[i]))) return false;
    }
    return true;
}

/// Rewrites a single function body so it parses cleanly. Issues handled:
///   - The renderer emits its own `void name(void) {` signature line which
///     would create a double declaration alongside the writer's calling-
///     convention signature; we skip such lines.
///   - The renderer's trailing `}` would pair with the writer's `{` but the
///     writer emits its own closing brace, so we skip the renderer's and
///     add one ourselves to guarantee balanced braces.
///   - duplicate label definitions (the structured CFG can render the same
///     block in multiple regions); subsequent occurrences are commented out.
///   - `goto L_xxx;` references whose target label was never emitted; we
///     collect them and append stub labels before the closing brace.
void emit_function_body(std::ostream& os, const DecompiledFunction& f, bool skip_first) {
    std::unordered_set<std::string> defined_labels;
    std::unordered_set<std::string> referenced_labels;
    std::vector<std::string> emitted;

    for (std::size_t i = 0; i < f.lines.size(); ++i) {
        if (skip_first && i == 0) continue;  // caller emits its own header comment
        std::string line = sanitize_c_line(f.lines[i]);

        // Bug 1: skip the renderer's own signature line - the C writer
        // emits its own (calling-convention-derived) signature just before
        // calling emit_function_body. Skipping avoids double declarations
        // and the unbalanced braces that follow.
        if (is_signature_line(line, f.name)) continue;
        // Skip the renderer's function-level closing brace (unindented `}`)
        // - we emit one at the end. Only the top-level brace is skipped;
        // nested indented braces (`  }`) are preserved.
        if (is_function_closing_brace_line(line)) continue;

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
        std::sort(undefined.begin(), undefined.end());
        for (const auto& lbl : undefined) {
            emitted.push_back("  " + lbl + ": ;  // stub for unresolved goto");
        }
    }

    for (const auto& line : emitted) os << line << "\n";
    // Always emit the closing brace ourselves so the braces balance even
    // when the renderer's body lacked one (defensive).
    os << "}\n";
}

/// v0.1.0: Computes the C function signature for a decompiled function
/// based on the binary's calling convention and (if available) DWARF type
/// info. Returns {return_type, params_string}.
///
///   - Return type: DWARF-resolved type when available; otherwise `void`.
///     We default to `void` (rather than `int`) because the IR renderer
///     emits bare `return;` statements - declaring the function as `int`
///     would make `gcc -c` reject every body with `-Werror=return-mismatch`.
///   - Parameters: when DWARF provides formal_parameter info we use it;
///     otherwise we emit `void`. The previous behaviour of always guessing
///     `int param1..4` produced uncompilable output whenever the real
///     function took a different number of arguments.
struct Signature {
    std::string return_type;
    std::string params;
};

[[nodiscard]] Signature compute_signature(const BinaryInfo& bin, const std::string& fn_name) {
    // Determine return type. Default to `void` so bare `return;` statements
    // emitted by the IR renderer compile cleanly. When DWARF is available
    // and the function has a non-void type, use the DWARF-resolved name.
    std::string ret_type = "void";
    if (bin.dwarf) {
        for (const auto& df : bin.dwarf->functions) {
            if (df.name == fn_name && df.type_offset != 0) {
                auto t = bin.dwarf->resolve_type_name(df.type_offset);
                if (!t.empty() && t != "void") ret_type = t;
                break;
            }
        }
    }

    // Determine parameters. The previous behaviour unconditionally emitted
    // up to `cc.max_reg_args` (capped at 4) explicit `int paramN` arguments,
    // which produced incorrect signatures for functions that took fewer (or
    // no) arguments and made the generated file fail to compile with
    // `gcc -c` whenever a call site passed a different argument count.
    //
    // Bug 3: prefer DWARF type info when available. The current DWARF
    // parser does not yet expose formal_parameter children, so we cannot
    // build the real parameter list - in that case we conservatively emit
    // `void`, which is always safe and never produces a mismatched-call
    // error. When parameter info is eventually added, this branch can be
    // extended to emit the real list.
    std::string params = "void";
    if (bin.dwarf) {
        for (const auto& df : bin.dwarf->functions) {
            if (df.name == fn_name) {
                // Future: derive the parameter count from the DWARF
                // subroutine_type's formal_parameter children. For now we
                // fall through to `void`.
                break;
            }
        }
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
