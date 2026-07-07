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
///   4. `return;` in a function declared as returning non-void. gcc 14+
///      rejects this with `-Werror=return-mismatch`. When
///      `non_void_return` is true we rewrite bare `return;` to `return 0;`
///      so the body compiles cleanly. The IR renderer only ever emits
///      bare `return;` (the lifter does not capture return values), so
///      this is a safe whole-function rewrite.
[[nodiscard]] std::string sanitize_c_line(std::string line, bool non_void_return) {
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

    // 4. Bare `return;` in a non-void function -> `return 0;` so gcc 14+
    //    accepts the body. The regex matches `return;` as a whole word so
    //    `return foo;` is left untouched.
    if (non_void_return) {
        static const std::regex bare_return_re(R"(\breturn;)");
        line = std::regex_replace(line, bare_return_re, "return 0;");
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
///   - `return;` is rewritten to `return 0;` when `non_void_return` is true
///     so gcc 14+ accepts the body (the IR renderer only emits bare
///     `return;` because the lifter does not capture return values).
void emit_function_body(std::ostream& os, const DecompiledFunction& f,
                        bool skip_first, bool non_void_return) {
    std::unordered_set<std::string> defined_labels;
    std::unordered_set<std::string> referenced_labels;
    std::vector<std::string> emitted;

    for (std::size_t i = 0; i < f.lines.size(); ++i) {
        if (skip_first && i == 0) continue;  // caller emits its own header comment
        std::string line = sanitize_c_line(f.lines[i], non_void_return);

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

/// v0.4.1: Real parameter identification for the C writer. The writer does
/// not have access to the IR Function (only the rendered pseudo-C lines),
/// so we re-derive the parameter count by scanning the body lines for
/// `vN` references that appear BEFORE any definition of the same vreg.
/// Each such vreg is treated as a parameter, capped at the calling
/// convention's `max_reg_args`. The signature line itself (e.g.
/// `int add(int param0, int param1) {`) is ignored because the regex only
/// matches `vN` identifiers, not `paramN`.
[[nodiscard]] int detect_param_count_from_lines(const std::vector<std::string>& lines,
                                                 const BinaryInfo& bin) {
    auto cc = default_calling_convention(bin.arch);
    std::unordered_set<std::uint32_t> defined;
    std::unordered_set<std::uint32_t> used_before_def;
    static const std::regex vre(R"(\bv([0-9]+)\b)");
    static const std::regex def_re(R"(^\s*v([0-9]+)\s*=)");

    std::size_t scanned = 0;
    for (const auto& line : lines) {
        if (scanned >= 12) break;
        ++scanned;
        // Skip the signature line - it ends with `{` and contains `(`.
        // The vN regex would not match paramN anyway, but skipping keeps
        // the heuristic stable when the renderer emits parameter names
        // that happen to contain `vN` substrings in the future.
        if (line.find(") {") != std::string::npos) continue;

        // Find every vN on the line and treat each as a use.
        for (std::sregex_iterator it(line.begin(), line.end(), vre), end; it != end; ++it) {
            try {
                const auto v = static_cast<std::uint32_t>(std::stoul((*it)[1].str()));
                if (!defined.count(v)) used_before_def.insert(v);
            } catch (...) {}
        }
        // If this line defines a vN (matches `  vN = ...`), mark it defined.
        std::smatch m;
        if (std::regex_search(line, m, def_re)) {
            try {
                const auto v = static_cast<std::uint32_t>(std::stoul(m[1].str()));
                defined.insert(v);
            } catch (...) {}
        }
    }
    int cap = (cc.max_reg_args > 0) ? cc.max_reg_args : 6;
    return std::min(static_cast<int>(used_before_def.size()), cap);
}

/// v0.1.0: Computes the C function signature for a decompiled function
/// based on the binary's calling convention and (if available) DWARF type
/// info. Returns {return_type, params_string}.
///
///   - Return type: DWARF-resolved type when available; otherwise `int`.
///     We default to `int` (rather than `void`) because most C functions
///     really do return int, and the IR renderer emits bare `return;`
///     statements which are valid in `int` functions (they return an
///     undefined value, but compile cleanly with `gcc -c`).
///   - Parameters: v0.4.1 derives the count by scanning the rendered body
///     for vregs that are used before being defined. Each such vreg is
///     treated as a parameter, capped at the calling convention's
///     max_reg_args. DWARF formal_parameter info is not yet exposed by
///     the parser, so the data-flow heuristic is the primary source.
struct Signature {
    std::string return_type;
    std::string params;
};

[[nodiscard]] Signature compute_signature(const BinaryInfo& bin,
                                          const std::string& fn_name,
                                          const std::vector<std::string>& body_lines) {
    // Determine return type. Default to `int` so bare `return;` statements
    // emitted by the IR renderer compile cleanly (a bare `return;` in an
    // `int` function returns an undefined value, which is legal C). When
    // DWARF is available and the function has a non-void type, use the
    // DWARF-resolved name.
    std::string ret_type = "int";
    if (bin.dwarf) {
        for (const auto& df : bin.dwarf->functions) {
            if (df.name == fn_name && df.type_offset != 0) {
                auto t = bin.dwarf->resolve_type_name(df.type_offset);
                if (!t.empty() && t != "void") {
                    // Bug 2: DWARF can encode a function's return type as a
                    // pointer-to-void (`void*`) when the type DIE is missing
                    // or unrecognised - `resolve_type_name` returns "void*"
                    // as a fallback for unknown offsets. The pseudo-C
                    // renderer emits bare `return;` statements (no value),
                    // so declaring such a function as `void*` makes gcc -c
                    // reject the body with `-Werror=return-mismatch`. Since
                    // no real function returns `void*` without an explicit
                    // cast in the body, collapse `void*` to `int` here.
                    ret_type = (t == "void*") ? "int" : t;
                } else if (t == "void") {
                    ret_type = "void";
                }
                break;
            }
        }
    }

    // Determine parameters. v0.4.1: scan the rendered body for vregs used
    // before defined, capped at the calling convention's max_reg_args.
    // When no vregs are detected (e.g. the function truly takes no args,
    // or the body is empty), we conservatively emit `void`, which is
    // always safe and never produces a mismatched-call error.
    std::string params = "void";
    const int nparams = detect_param_count_from_lines(body_lines, bin);
    if (nparams > 0) {
        params.clear();
        for (int i = 0; i < nparams; ++i) {
            if (i > 0) params += ", ";
            params += "int param" + std::to_string(i);
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
        const auto sig = compute_signature(bin, f.name, f.lines);
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

    // v0.3.1: Collect local_N references and declare them as char arrays
    // (they represent stack frame slots used as memory operands).
    {
        static const std::regex local_re(R"(\blocal_([0-9]+)\b)");
        std::unordered_set<std::uint32_t> locals;
        for (const auto& f : functions) {
            for (const auto& line : f.lines) {
                for (std::sregex_iterator it(line.begin(), line.end(), local_re), end; it != end; ++it) {
                    try {
                        locals.insert(static_cast<std::uint32_t>(std::stoul((*it)[1].str())));
                    } catch (...) {}
                }
            }
        }
        if (!locals.empty()) {
            std::vector<std::uint32_t> sorted_locals(locals.begin(), locals.end());
            std::sort(sorted_locals.begin(), sorted_locals.end());
            os << "// Stack frame locals (rendered as int for parseability).\n";
            os << "int ";
            for (std::size_t i = 0; i < sorted_locals.size(); ++i) {
                if (i) os << ", ";
                os << "local_" << sorted_locals[i];
            }
            os << ";\n\n";
        }
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
        const auto sig = compute_signature(bin, f.name, f.lines);
        os << sig.return_type << " " << f.name << "(" << sig.params << ") {\n";
        // v0.1.0: when the function returns non-void, the IR renderer's bare
        // `return;` statements must be rewritten to `return 0;` so the body
        // compiles with gcc 14+ (which treats -Wreturn-mismatch as an error).
        const bool non_void_return = (sig.return_type != "void");
        emit_function_body(os, f, /*skip_first=*/true, non_void_return);
        os << "\n";
    }
}

std::string to_c(const BinaryInfo& bin, const std::vector<DecompiledFunction>& functions) {
    std::ostringstream os;
    write_c(os, bin, functions);
    return os.str();
}

}  // namespace nyx::output
