// =============================================================================
// Nyx - A headless decompilation engine
// Copyright (C) 2024-2026 Chapzoo  <https://github.com/Chapzoo>
// SPDX-License-Identifier: GPL-3.0-or-later
// =============================================================================
#include "nyx/output/json_writer.hpp"

#include "nyx/core/arch.hpp"
#include "nyx/core/bytes.hpp"

#include <iomanip>
#include <sstream>
#include <string>

namespace nyx::output {

namespace {

/// Escapes a string for JSON output. Handles control chars, " and \.
std::string escape_json(std::string_view s) {
    std::string out;
    out.reserve(s.size() + 4);
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

}  // namespace

void write_json(std::ostream& os,
                const BinaryInfo& bin,
                const std::vector<DecompiledFunction>& functions) {
    os << "{\n";
    os << "  \"schema\": \"nyx.v0.0.1\",\n";
    os << "  \"binary\": {\n";
    os << "    \"path\": \"" << escape_json(bin.path) << "\",\n";
    os << "    \"format\": \"" << to_string(bin.format) << "\",\n";
    os << "    \"arch\": \"" << arch_name(bin.arch) << "\",\n";
    os << "    \"arch_pretty\": \"" << arch_pretty(bin.arch) << "\",\n";
    os << "    \"endian\": \"" << (bin.endian == Endian::Little ? "little" : "big") << "\",\n";
    os << "    \"is_64bit\": " << (bin.is_64bit ? "true" : "false") << ",\n";
    os << "    \"is_pie\": "   << (bin.is_pie   ? "true" : "false") << ",\n";
    os << "    \"has_nx\": "   << (bin.has_nx   ? "true" : "false") << ",\n";
    os << "    \"has_relro\": "<< (bin.has_relro? "true" : "false") << ",\n";
    os << "    \"has_canary\": " << (bin.has_canary ? "true" : "false") << ",\n";
    os << "    \"stripped\": " << (bin.stripped ? "true" : "false") << ",\n";
    os << "    \"entry_point\": \"" << to_hex(bin.entry_point, 0, true) << "\",\n";
    os << "    \"image_base\": \""  << to_hex(bin.image_base,  0, true) << "\",\n";

    // sections
    os << "    \"sections\": [";
    for (std::size_t i = 0; i < bin.sections.size(); ++i) {
        const auto& s = bin.sections[i];
        if (i) os << ",";
        os << "\n      {\n";
        os << "        \"name\": \"" << escape_json(s.name) << "\",\n";
        os << "        \"vaddr\": \"" << to_hex(s.vaddr, 0, true) << "\",\n";
        os << "        \"file_off\": " << s.file_off << ",\n";
        os << "        \"file_size\": " << s.file_size << ",\n";
        os << "        \"mem_size\": "  << s.mem_size  << ",\n";
        os << "        \"executable\": " << (s.executable ? "true" : "false") << ",\n";
        os << "        \"writable\": "   << (s.writable   ? "true" : "false") << ",\n";
        os << "        \"readable\": "   << (s.readable   ? "true" : "false") << ",\n";
        os << "        \"is_code\": "    << (s.is_code    ? "true" : "false") << "\n";
        os << "      }";
    }
    os << "\n    ],\n";

    // symbols
    os << "    \"symbols\": [";
    for (std::size_t i = 0; i < bin.symbols.size(); ++i) {
        const auto& s = bin.symbols[i];
        if (i) os << ",";
        os << "\n      {\n";
        os << "        \"name\": \"" << escape_json(s.name) << "\",\n";
        os << "        \"value\": \"" << to_hex(s.value, 0, true) << "\",\n";
        os << "        \"size\": " << s.size << ",\n";
        os << "        \"kind\": \"";
        switch (s.kind) {
            case Symbol::Kind::Unknown:  os << "unknown";  break;
            case Symbol::Kind::Function: os << "function"; break;
            case Symbol::Kind::Object:   os << "object";   break;
            case Symbol::Kind::Section:  os << "section";  break;
            case Symbol::Kind::File:     os << "file";     break;
        }
        os << "\",\n";
        os << "        \"imported\": " << (s.imported ? "true" : "false") << ",\n";
        os << "        \"exported\": " << (s.exported ? "true" : "false") << "\n";
        os << "      }";
    }
    os << "\n    ]\n";
    os << "  },\n";

    // functions
    os << "  \"functions\": [";
    for (std::size_t i = 0; i < functions.size(); ++i) {
        const auto& f = functions[i];
        if (i) os << ",";
        os << "\n    {\n";
        os << "      \"name\": \"" << escape_json(f.name) << "\",\n";
        os << "      \"entry\": \"" << to_hex(f.entry, 0, true) << "\",\n";
        os << "      \"block_count\": " << f.block_count << ",\n";
        os << "      \"insn_count\": "  << f.insn_count  << ",\n";
        os << "      \"body\": [";
        for (std::size_t j = 0; j < f.lines.size(); ++j) {
            if (j) os << ", ";
            os << "\n        \"" << escape_json(f.lines[j]) << "\"";
        }
        os << (f.lines.empty() ? "]" : "\n      ]") << "\n";
        os << "    }";
    }
    os << "\n  ]\n";
    os << "}\n";
}

std::string to_json(const BinaryInfo& bin, const std::vector<DecompiledFunction>& functions) {
    std::ostringstream os;
    write_json(os, bin, functions);
    return os.str();
}

}  // namespace nyx::output
