// =============================================================================
// Nyx - A headless decompilation engine
// Copyright (C) 2024-2026 Chapzoo  <https://github.com/Chapzoo>
// SPDX-License-Identifier: GPL-3.0-or-later
// =============================================================================
//
// nyx - command-line interface
//
// Examples:
//   nyx --version
//   nyx ./bin.elf
//   nyx --format json  ./bin.elf
//   nyx --format text  ./bin.elf -o out.txt
//   nyx --format pseudo-c ./bin.elf
//   nyx --arch x86-64 --format elf ./bin.unknown
// =============================================================================
#include "nyx/version.hpp"

#include "nyx/core/arch.hpp"
#include "nyx/core/bytes.hpp"
#include "nyx/core/error.hpp"
#include "nyx/core/logger.hpp"
#include "nyx/core/types.hpp"
#include "nyx/decompiler/decompiler.hpp"
#include "nyx/output/json_writer.hpp"
#include "nyx/output/pseudo_c_writer.hpp"
#include "nyx/output/text_writer.hpp"
#include "nyx/parsers/binary_parser.hpp"
#include "nyx/parsers/disassembler.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct CliArgs {
    std::string input_path;
    std::string output_path;            // empty => stdout
    std::string format      = "json";   // json | text | pseudo-c
    std::string log_level   = "info";
    bool        show_help    = false;
    bool        show_version = false;
    bool        quiet        = false;
    // Optional overrides for when detection fails.
    std::string force_arch;
    std::string force_format;
};

void print_help(std::ostream& os) {
    os << "Usage: nyx [OPTIONS] <binary>\n"
       << "\n"
       << "Nyx is a headless decompilation engine. It parses ELF, PE and Mach-O\n"
       << "binaries, disassembles their code sections with Capstone, lifts the\n"
       << "instructions to a small SSA-like IR, builds a control-flow graph and\n"
       << "renders the result as JSON, plain text or pseudo-C source.\n"
       << "\n"
       << "Options:\n"
       << "  -h, --help              Show this help and exit.\n"
       << "  -V, --version           Print the Nyx banner and exit.\n"
       << "  -f, --format <fmt>      Output format: json | text | pseudo-c (default: json).\n"
       << "  -o, --output <path>     Write output to <path> instead of stdout.\n"
       << "  -L, --log-level <lvl>   trace|debug|info|warn|error|critical (default: info).\n"
       << "  -q, --quiet             Alias for --log-level critical.\n"
       << "      --arch <name>       Override architecture detection (x86, x86-64, arm, ...).\n"
       << "      --format-hint <fmt> Override format detection (elf|pe|mach-o).\n"
       << "\n"
       << "Exit codes:\n"
       << "   0  success\n"
       << "   1  invalid arguments\n"
       << "   2  I/O error\n"
       << "   3  parse / decompile error\n"
       << "\n"
       << "Report bugs: <https://github.com/Chapzoo/nyx/issues>\n";
}

int parse_args(int argc, char** argv, CliArgs& out) {
    std::vector<std::string_view> args(argv + 1, argv + argc);
    for (std::size_t i = 0; i < args.size(); ++i) {
        const auto a = args[i];
        if (a == "-h" || a == "--help") {
            out.show_help = true;
        } else if (a == "-V" || a == "--version") {
            out.show_version = true;
        } else if (a == "-f" || a == "--format") {
            if (++i >= args.size()) { std::cerr << "error: " << a << " needs an argument\n"; return 1; }
            out.format = std::string(args[i]);
        } else if (a == "-o" || a == "--output") {
            if (++i >= args.size()) { std::cerr << "error: " << a << " needs an argument\n"; return 1; }
            out.output_path = std::string(args[i]);
        } else if (a == "-L" || a == "--log-level") {
            if (++i >= args.size()) { std::cerr << "error: " << a << " needs an argument\n"; return 1; }
            out.log_level = std::string(args[i]);
        } else if (a == "-q" || a == "--quiet") {
            out.quiet = true;
        } else if (a == "--arch") {
            if (++i >= args.size()) { std::cerr << "error: " << a << " needs an argument\n"; return 1; }
            out.force_arch = std::string(args[i]);
        } else if (a == "--format-hint") {
            if (++i >= args.size()) { std::cerr << "error: " << a << " needs an argument\n"; return 1; }
            out.force_format = std::string(args[i]);
        } else if (a.empty()) {
            // skip
        } else if (a[0] == '-') {
            std::cerr << "error: unknown option " << a << "\n";
            return 1;
        } else {
            if (!out.input_path.empty()) {
                std::cerr << "error: multiple input files are not supported (got '" << out.input_path << "' and '" << a << "')\n";
                return 1;
            }
            out.input_path = std::string(a);
        }
    }
    return 0;
}

int set_log_level(const std::string& s) {
    if (s == "trace")    nyx::Logger::instance().set_level(nyx::LogLevel::Trace);
    else if (s == "debug") nyx::Logger::instance().set_level(nyx::LogLevel::Debug);
    else if (s == "info")  nyx::Logger::instance().set_level(nyx::LogLevel::Info);
    else if (s == "warn")  nyx::Logger::instance().set_level(nyx::LogLevel::Warn);
    else if (s == "error") nyx::Logger::instance().set_level(nyx::LogLevel::Error);
    else if (s == "critical") nyx::Logger::instance().set_level(nyx::LogLevel::Critical);
    else {
        std::cerr << "error: invalid log level '" << s << "'\n";
        return 1;
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    CliArgs args;
    if (int rc = parse_args(argc, argv, args); rc != 0) return rc;

    if (args.show_help) {
        print_help(std::cout);
        return 0;
    }
    if (args.show_version) {
        std::cout << nyx::version_banner() << "\n";
        return 0;
    }
    if (args.input_path.empty()) {
        std::cerr << "error: no input binary given. Run 'nyx --help'.\n";
        return 1;
    }
    if (args.quiet) args.log_level = "critical";
    if (int rc = set_log_level(args.log_level); rc != 0) return rc;

    try {
        nyx::BinaryInfo bin = nyx::BinaryParser::load_and_parse(args.input_path);

        // Apply overrides if requested.
        if (!args.force_arch.empty()) {
            auto a = nyx::arch_from_name(args.force_arch);
            if (!a) {
                std::cerr << "error: unknown --arch value '" << args.force_arch << "'\n";
                return 1;
            }
            bin.arch = *a;
        }
        if (!args.force_format.empty()) {
            auto f = nyx::format_from_name(args.force_format);
            if (!f) {
                std::cerr << "error: unknown --format-hint value '" << args.force_format << "'\n";
                return 1;
            }
            bin.format = *f;
        }

        // Always collect disassembled sections - text output needs them and
        // the JSON writer can include them later. For v0.0.1 we include them
        // only in the text dump.
        std::vector<std::vector<nyx::DecodedInstruction>> disasm_sections;
        {
            nyx::Disassembler dis(bin.arch, bin.endian);
            auto file_buf = nyx::ByteBuffer::from_file(args.input_path);
            if (file_buf) {
                for (const auto& s : bin.sections) {
                    if (!s.executable) continue;
                    if (s.file_off >= file_buf->size()) continue;
                    const std::size_t avail = file_buf->size() - s.file_off;
                    const std::size_t n = std::min<std::size_t>(s.file_size, avail);
                    if (n == 0) continue;
                    nyx::ByteView bytes{file_buf->data() + s.file_off, n};
                    disasm_sections.push_back(dis.decode(bytes, s.vaddr));
                }
            }
        }

        // Decompile.
        nyx::Decompiler::Options opts;
        opts.linear_sweep_fallback = true;
        opts.max_insns_per_function = 5000;
        nyx::Decompiler dec(opts);
        auto functions = dec.decompile(bin);

        // Choose output sink.
        std::ofstream fout;
        std::ostream* out = &std::cout;
        if (!args.output_path.empty() && args.output_path != "-") {
            fout.open(args.output_path, std::ios::binary | std::ios::trunc);
            if (!fout) {
                std::cerr << "error: cannot open output '" << args.output_path << "'\n";
                return 2;
            }
            out = &fout;
        }

        if (args.format == "json") {
            nyx::output::write_json(*out, bin, functions);
        } else if (args.format == "text") {
            nyx::output::write_text(*out, bin, disasm_sections);
        } else if (args.format == "pseudo-c" || args.format == "pseudoc" || args.format == "c") {
            nyx::output::write_pseudo_c(*out, bin, functions);
        } else {
            std::cerr << "error: unknown --format '" << args.format << "'. Use json, text or pseudo-c.\n";
            return 1;
        }
        return 0;
    } catch (const nyx::Error& e) {
        std::cerr << "nyx: " << e.what() << "\n";
        return 3;
    } catch (const std::exception& e) {
        std::cerr << "nyx: " << e.what() << "\n";
        return 3;
    }
}
