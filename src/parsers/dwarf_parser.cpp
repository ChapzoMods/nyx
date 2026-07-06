// =============================================================================
// Nyx - A headless decompilation engine
// Copyright (C) 2024-2026 Chapzoo  <https://github.com/Chapzoo>
// SPDX-License-Identifier: GPL-3.0-or-later
// =============================================================================
//
// DWARF v4 parser - pure C++20, no libdwarf.
//
// Covers:
//   .debug_line  - line number program state machine (DWARF v4)
//   .debug_info  - DIE tree (subprograms, base types, pointers, typedefs)
//   .debug_abbrev - abbreviation tables
//   .debug_str   - .debug_str string table
//
// The parser is conservative: corrupt or truncated sections are skipped
// (logged via NYX_WARN) and the rest are still parsed.
// =============================================================================
#include "nyx/parsers/dwarf_parser.hpp"

#include "nyx/core/bytes.hpp"
#include "nyx/core/logger.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace nyx {

namespace {

// ---------------------------------------------------------------------------
// LEB128 decoders (unsigned and signed).
// ---------------------------------------------------------------------------
std::uint64_t read_uleb128(const std::uint8_t* p, std::size_t max, std::size_t& consumed) {
    std::uint64_t result = 0;
    std::size_t shift = 0;
    consumed = 0;
    for (std::size_t i = 0; i < max && i < 10; ++i) {
        const std::uint8_t byte = p[i];
        result |= static_cast<std::uint64_t>(byte & 0x7F) << shift;
        ++consumed;
        if ((byte & 0x80) == 0) break;
        shift += 7;
    }
    return result;
}

std::int64_t read_sleb128(const std::uint8_t* p, std::size_t max, std::size_t& consumed) {
    std::int64_t result = 0;
    std::size_t shift = 0;
    consumed = 0;
    std::uint8_t byte = 0;
    for (std::size_t i = 0; i < max && i < 10; ++i) {
        byte = p[i];
        result |= static_cast<std::int64_t>(byte & 0x7F) << shift;
        ++consumed;
        shift += 7;
        if ((byte & 0x80) == 0) break;
    }
    // Sign-extend if the sign bit is set.
    if (shift < 64 && (byte & 0x40)) {
        result |= -(static_cast<std::int64_t>(1) << shift);
    }
    return result;
}

// ---------------------------------------------------------------------------
// DWARF section reader: tracks position, endianness and bounds.
// ---------------------------------------------------------------------------
struct DwarfReader {
    const std::uint8_t* data;
    std::size_t size;
    std::size_t pos = 0;
    bool little_endian;

    DwarfReader(const std::uint8_t* d, std::size_t s, bool le)
        : data(d), size(s), little_endian(le) {}

    [[nodiscard]] bool eof() const noexcept { return pos >= size; }
    [[nodiscard]] bool ok(std::size_t need) const noexcept { return pos + need <= size; }

    [[nodiscard]] std::uint8_t u8() {
        if (!ok(1)) return 0;
        return data[pos++];
    }
    [[nodiscard]] std::uint16_t u16() {
        if (!ok(2)) { pos = size; return 0; }
        const std::uint16_t v = little_endian ? read_u16_le(data + pos) : read_u16_be(data + pos);
        pos += 2; return v;
    }
    [[nodiscard]] std::uint32_t u32() {
        if (!ok(4)) { pos = size; return 0; }
        const std::uint32_t v = little_endian ? read_u32_le(data + pos) : read_u32_be(data + pos);
        pos += 4; return v;
    }
    [[nodiscard]] std::uint64_t u64() {
        if (!ok(8)) { pos = size; return 0; }
        const std::uint64_t v = little_endian ? read_u64_le(data + pos) : read_u64_be(data + pos);
        pos += 8; return v;
    }
    /// Reads a "length" field: if the first 4 bytes are 0xFFFFFFFF, the
    /// next 8 bytes are the 64-bit length (DWARF64). Returns the length
    /// and sets `is_dwarf64` accordingly. The returned length does NOT
    /// include the initial 4 or 12 bytes of the length field itself.
    [[nodiscard]] std::uint64_t length(bool& is_dwarf64) {
        is_dwarf64 = false;
        if (!ok(4)) { pos = size; return 0; }
        const std::uint32_t len32 = u32();
        if (len32 == 0xFFFFFFFFu) {
            is_dwarf64 = true;
            return u64();
        }
        return len32;
    }
    [[nodiscard]] std::uint64_t offset(bool dwarf64) {
        return dwarf64 ? u64() : static_cast<std::uint64_t>(u32());
    }
    [[nodiscard]] std::uint64_t uleb() {
        std::size_t c; const auto v = read_uleb128(data + pos, size - pos, c); pos += c; return v;
    }
    [[nodiscard]] std::int64_t sleb() {
        std::size_t c; const auto v = read_sleb128(data + pos, size - pos, c); pos += c; return v;
    }
    /// Reads a null-terminated string starting at `pos`. Advances pos
    /// past the null.
    [[nodiscard]] std::string cstr() {
        std::string s;
        while (pos < size && data[pos] != 0) { s.push_back(static_cast<char>(data[pos])); ++pos; }
        if (pos < size) ++pos;  // skip null
        return s;
    }
    /// Reads a string from .debug_str at the given offset.
    [[nodiscard]] std::string str_at(std::uint64_t off, ByteView debug_str) const {
        if (off >= debug_str.size()) return {};
        const char* base = reinterpret_cast<const char*>(debug_str.data());
        std::string s;
        for (std::size_t i = off; i < debug_str.size() && base[i] != '\0'; ++i) {
            s.push_back(base[i]);
        }
        return s;
    }
};

// ---------------------------------------------------------------------------
// .debug_abbrev parsing
// ---------------------------------------------------------------------------
struct AbbrevEntry {
    std::uint64_t code = 0;
    std::uint64_t tag = 0;
    bool has_children = false;
    std::vector<std::pair<std::uint64_t, std::uint64_t>> attrs;  // (name, form)
};

std::vector<AbbrevEntry> parse_abbrev_table(ByteView data) {
    std::vector<AbbrevEntry> result;
    if (data.empty()) return result;
    DwarfReader r(data.data(), data.size(), true);  // abbrev is always endian-agnostic
    while (!r.eof()) {
        AbbrevEntry e;
        e.code = r.uleb();
        if (e.code == 0) break;
        e.tag = r.uleb();
        e.has_children = (r.u8() != 0);
        while (true) {
            const auto name = r.uleb();
            const auto form = r.uleb();
            if (name == 0 && form == 0) break;
            e.attrs.emplace_back(name, form);
        }
        result.push_back(std::move(e));
    }
    return result;
}

// DWARF tags
constexpr std::uint64_t DW_TAG_compile_unit     = 0x11;
constexpr std::uint64_t DW_TAG_subprogram       = 0x2E;
constexpr std::uint64_t DW_TAG_base_type        = 0x24;
constexpr std::uint64_t DW_TAG_pointer_type     = 0x0F;
constexpr std::uint64_t DW_TAG_typedef          = 0x16;
constexpr std::uint64_t DW_TAG_structure_type   = 0x13;
constexpr std::uint64_t DW_TAG_union_type       = 0x17;
constexpr std::uint64_t DW_TAG_enumeration_type = 0x04;

// DWARF attributes
constexpr std::uint64_t DW_AT_name        = 0x03;
constexpr std::uint64_t DW_AT_low_pc      = 0x11;
constexpr std::uint64_t DW_AT_high_pc     = 0x12;
constexpr std::uint64_t DW_AT_type        = 0x49;
constexpr std::uint64_t DW_AT_byte_size   = 0x0B;
constexpr std::uint64_t DW_AT_encoding    = 0x3E;
constexpr std::uint64_t DW_AT_comp_dir    = 0x1B;
constexpr std::uint64_t DW_AT_stmt_list   = 0x10;

// DWARF forms
constexpr std::uint64_t DW_FORM_addr      = 0x01;
constexpr std::uint64_t DW_FORM_data1     = 0x0B;
constexpr std::uint64_t DW_FORM_data2     = 0x05;
constexpr std::uint64_t DW_FORM_data4     = 0x06;
constexpr std::uint64_t DW_FORM_data8     = 0x07;
constexpr std::uint64_t DW_FORM_string    = 0x08;
constexpr std::uint64_t DW_FORM_strp      = 0x0E;
constexpr std::uint64_t DW_FORM_udata     = 0x0F;
constexpr std::uint64_t DW_FORM_sdata     = 0x0D;
constexpr std::uint64_t DW_FORM_ref1      = 0x11;
constexpr std::uint64_t DW_FORM_ref2      = 0x12;
constexpr std::uint64_t DW_FORM_ref4      = 0x13;
constexpr std::uint64_t DW_FORM_ref8      = 0x14;
constexpr std::uint64_t DW_FORM_ref_udata = 0x15;
constexpr std::uint64_t DW_FORM_flag      = 0x19;
constexpr std::uint64_t DW_FORM_block1    = 0x0A;
constexpr std::uint64_t DW_FORM_block2    = 0x03;
constexpr std::uint64_t DW_FORM_block4    = 0x04;
constexpr std::uint64_t DW_FORM_block     = 0x09;
constexpr std::uint64_t DW_FORM_implicit_const = 0x21;
constexpr std::uint64_t DW_FORM_sec_offset     = 0x17;
constexpr std::uint64_t DW_FORM_exprloc        = 0x18;
constexpr std::uint64_t DW_FORM_strx           = 0x1A;
constexpr std::uint64_t DW_FORM_addrx          = 0x1B;
constexpr std::uint64_t DW_FORM_ref_sup4       = 0x1C;
constexpr std::uint64_t DW_FORM_strx1          = 0x25;
constexpr std::uint64_t DW_FORM_strx2          = 0x26;
constexpr std::uint64_t DW_FORM_strx3          = 0x27;
constexpr std::uint64_t DW_FORM_strx4          = 0x28;

/// Reads a form value and returns it as a 64-bit integer (for data
/// forms) or stores the string offset in `str_offset`. For forms that
/// don't fit (blocks, etc.), advances the reader past the value.
struct FormValue {
    std::uint64_t u = 0;
    std::int64_t s = 0;
    std::string str;
    std::uint64_t str_offset = 0;
    bool is_string = false;     // FORM_string or FORM_strp
    bool is_signed = false;
};

FormValue read_form(DwarfReader& r, std::uint64_t form, std::uint64_t implicit_const,
                    bool dwarf64, std::uint8_t addr_size, ByteView debug_str) {
    FormValue v;
    switch (form) {
        case DW_FORM_addr:
            if (addr_size == 8) v.u = r.u64();
            else if (addr_size == 4) v.u = r.u32();
            else v.u = r.u16();
            break;
        case DW_FORM_data1: v.u = r.u8(); break;
        case DW_FORM_data2: v.u = r.u16(); break;
        case DW_FORM_data4: v.u = r.u32(); break;
        case DW_FORM_data8: v.u = r.u64(); break;
        case DW_FORM_udata: v.u = r.uleb(); break;
        case DW_FORM_sdata: v.s = r.sleb(); v.is_signed = true; break;
        case DW_FORM_string: v.str = r.cstr(); v.is_string = true; break;
        case DW_FORM_strp: v.str_offset = r.offset(dwarf64); v.is_string = true;
            v.str = r.str_at(v.str_offset, debug_str); break;
        case DW_FORM_ref1:  v.u = r.u8(); break;
        case DW_FORM_ref2:  v.u = r.u16(); break;
        case DW_FORM_ref4:  v.u = r.u32(); break;
        case DW_FORM_ref8:  v.u = r.u64(); break;
        case DW_FORM_ref_udata: v.u = r.uleb(); break;
        // DW_FORM_flag (0x19) is DW_FORM_flag_present in DWARF v4+:
        // implicitly true, consumes no bytes. We treat it as such since
        // modern compilers emit flag_present, not the 1-byte flag.
        case DW_FORM_flag: v.u = 1; break;
        case DW_FORM_implicit_const: v.u = implicit_const; break;
        case DW_FORM_sec_offset: v.u = r.offset(dwarf64); break;
        case DW_FORM_exprloc: { const auto n = r.uleb(); r.pos += n; break; }
        case DW_FORM_strx: v.u = r.uleb(); break;
        case DW_FORM_addrx: v.u = r.uleb(); break;
        case DW_FORM_ref_sup4: v.u = r.u32(); break;
        case DW_FORM_strx1: v.u = r.u8(); break;
        case DW_FORM_strx2: v.u = r.u16(); break;
        case DW_FORM_strx3: { // 3-byte value
            if (r.ok(3)) {
                v.u = static_cast<std::uint64_t>(r.data[r.pos])
                    | (static_cast<std::uint64_t>(r.data[r.pos+1]) << 8)
                    | (static_cast<std::uint64_t>(r.data[r.pos+2]) << 16);
                r.pos += 3;
            } else r.pos = r.size;
            break; }
        case DW_FORM_strx4: v.u = r.u32(); break;
        case DW_FORM_block1: { const auto n = r.u8(); r.pos += n; break; }
        case DW_FORM_block2: { const auto n = r.u16(); r.pos += n; break; }
        case DW_FORM_block4: { const auto n = r.u32(); r.pos += n; break; }
        case DW_FORM_block:  { const auto n = r.uleb(); r.pos += n; break; }
        default:
            // Unknown form: we can't safely skip it, so abort this CU.
            NYX_WARN("DWARF: unhandled form 0x" + to_hex(form, 0, false));
            r.pos = r.size;  // force end
            return v;
    }
    return v;
}

// ---------------------------------------------------------------------------
// .debug_line parsing (line number program state machine, DWARF v4)
// ---------------------------------------------------------------------------
struct LineProgramHeader {
    std::uint8_t min_inst_length = 0;
    std::uint8_t max_ops_per_inst = 1;
    std::uint8_t default_is_stmt = 0;
    std::int8_t line_base = 0;
    std::uint8_t line_range = 0;
    std::uint8_t opcode_base = 0;
    std::vector<std::uint8_t> standard_opcode_lengths;
    std::vector<std::string> include_directories;
    std::vector<std::string> file_names;
};

LineProgramHeader parse_line_header(DwarfReader& r, bool& is_dwarf64) {
    LineProgramHeader h;
    // unit_length
    [[maybe_unused]] auto unit_len = r.length(is_dwarf64);
    // version (uhalf)
    const std::uint16_t version = r.u16();
    if (version < 2 || version > 4) {
        // version 0 typically means we hit end-of-section padding.
        if (version != 0) {
            NYX_WARN("DWARF: .debug_line version " + std::to_string(version) + " not supported");
        }
        // Set sane defaults to avoid FPE downstream.
        h.line_range = 1;
        h.opcode_base = 0;  // signals "skip this unit"
        return h;
    }
    // header_length (word for DWARF32, u64 for DWARF64)
    [[maybe_unused]] auto header_len = r.offset(is_dwarf64);
    h.min_inst_length = r.u8();
    if (version >= 4) h.max_ops_per_inst = r.u8();
    h.default_is_stmt = r.u8();
    h.line_base = static_cast<std::int8_t>(r.u8());
    h.line_range = r.u8();
    if (h.line_range == 0) h.line_range = 1;  // guard against corrupt input
    h.opcode_base = r.u8();
    if (h.opcode_base == 0) h.opcode_base = 1;
    // standard_opcode_lengths[opcode_base - 1]
    h.standard_opcode_lengths.resize(h.opcode_base > 0 ? h.opcode_base - 1 : 0);
    for (std::size_t i = 0; i < h.standard_opcode_lengths.size(); ++i) {
        h.standard_opcode_lengths[i] = r.u8();
    }
    // include_directories (sequence of null-terminated strings, terminated by empty)
    while (true) {
        std::string s = r.cstr();
        if (s.empty()) break;
        h.include_directories.push_back(std::move(s));
    }
    // file_names (sequence of name + dir_index + mtime + size, terminated by empty)
    while (true) {
        std::string name = r.cstr();
        if (name.empty()) break;
        [[maybe_unused]] auto dir_idx = r.uleb();
        [[maybe_unused]] auto mtime = r.uleb();
        [[maybe_unused]] auto fsize = r.uleb();
        h.file_names.push_back(std::move(name));
    }
    return h;
}

void run_line_program(DwarfReader& r, const LineProgramHeader& h,
                      std::vector<DwarfLineEntry>& out) {
    // State machine registers.
    std::uint64_t address = 0;
    std::uint32_t file = 1;
    std::uint32_t line = 1;
    std::uint32_t column = 0;
    bool is_stmt = h.default_is_stmt != 0;

    while (!r.eof()) {
        const std::uint8_t op = r.u8();
        if (op == 0) {
            // Extended opcode.
            const auto len = r.uleb();
            const std::size_t end_pos = r.pos + len;
            if (end_pos > r.size) break;
            const std::uint8_t sub = r.u8();
            switch (sub) {
                case 0x01: // DW_LNE_end_sequence
                    { DwarfLineEntry e; e.address = address; e.file = file;
                      e.line = line; e.column = column; e.is_stmt = is_stmt;
                      e.end_seq = true; out.push_back(e); }
                    // Reset.
                    address = 0; file = 1; line = 1; column = 0;
                    is_stmt = h.default_is_stmt != 0;
                    break;
                case 0x02: // DW_LNE_set_address
                    if (len - 1 >= 8) address = r.u64();
                    else if (len - 1 >= 4) address = r.u32();
                    else address = r.u16();
                    break;
                case 0x03: // DW_LNE_define_file
                    // name + dir + mtime + size; skip
                    break;
                default:
                    break;
            }
            r.pos = end_pos;
        } else if (op < h.opcode_base) {
            // Standard opcode.
            switch (op) {
                case 0x01: // DW_LNS_copy
                    { DwarfLineEntry e; e.address = address; e.file = file;
                      e.line = line; e.column = column; e.is_stmt = is_stmt;
                      out.push_back(e); }
                    break;
                case 0x02: // DW_LNS_advance_pc
                    address += r.uleb() * h.min_inst_length; break;
                case 0x03: // DW_LNS_advance_line
                    line += static_cast<std::uint32_t>(r.sleb()); break;
                case 0x04: // DW_LNS_set_file
                    file = static_cast<std::uint32_t>(r.uleb()); break;
                case 0x05: // DW_LNS_set_column
                    column = static_cast<std::uint32_t>(r.uleb()); break;
                case 0x06: // DW_LNS_negate_stmt
                    is_stmt = !is_stmt; break;
                case 0x07: // DW_LNS_set_basic_block
                    break;
                case 0x08: // DW_LNS_const_add_pc
                    address += (255 - h.opcode_base) * h.min_inst_length; break;
                case 0x09: // DW_LNS_fixed_advance_pc
                    address += r.u16(); break;
                default:
                    // Skip unknown standard opcodes by consuming their operands.
                    for (std::size_t i = 0; i < h.standard_opcode_lengths[op - 1]; ++i) {
                        (void)r.uleb();
                    }
                    break;
            }
        } else {
            // Special opcode.
            const std::uint8_t adjusted = op - h.opcode_base;
            const std::uint32_t op_adv = (h.line_range > 0) ? (adjusted / h.line_range) : 0;
            const std::uint32_t ops_per = (h.max_ops_per_inst > 0) ? h.max_ops_per_inst : 1;
            address += (op_adv / ops_per) * h.min_inst_length;
            line += h.line_base + (adjusted % h.line_range);
            { DwarfLineEntry e; e.address = address; e.file = file;
              e.line = line; e.column = column; e.is_stmt = is_stmt;
              out.push_back(e); }
        }
    }
}

// ---------------------------------------------------------------------------
// .debug_info parsing (DIEs)
// ---------------------------------------------------------------------------
void parse_info_unit(DwarfReader& r, ByteView debug_abbrev, ByteView debug_str,
                     std::vector<DwarfFunction>& funcs,
                     std::unordered_map<std::uint64_t, DwarfType>& types) {
    bool is_dwarf64 = false;
    const auto unit_len = r.length(is_dwarf64);
    if (unit_len == 0) return;
    const std::size_t unit_end = r.pos + unit_len;
    if (unit_end > r.size) { r.pos = r.size; return; }

    const std::uint16_t version = r.u16();
    if (version < 2 || version > 4) {
        r.pos = unit_end; return;
    }
    const auto abbrev_offset = r.offset(is_dwarf64);
    const std::uint8_t addr_size = r.u8();

    // Parse the abbreviation table at abbrev_offset.
    ByteView abbrev_slice{};
    if (abbrev_offset < debug_abbrev.size()) {
        abbrev_slice = debug_abbrev.sub(abbrev_offset, debug_abbrev.size() - abbrev_offset).value_or(ByteView{});
    }
    auto abbrevs = parse_abbrev_table(abbrev_slice);

    // Build a code -> AbbrevEntry map.
    std::unordered_map<std::uint64_t, const AbbrevEntry*> abbrev_map;
    for (const auto& a : abbrevs) {
        abbrev_map[a.code] = &a;
    }

    // Walk DIEs. We process them flatly but track depth: when a DIE has
    // has_children, its children follow until a null DIE (code 0). We
    // don't recurse, but we do consume the null DIEs that terminate
    // sibling lists.
    int depth = 0;
    while (r.pos < unit_end) {
        const std::uint64_t die_offset = r.pos;
        const auto code = r.uleb();
        if (code == 0) {
            // Null DIE: terminates a sibling list (end of children).
            if (depth > 0) --depth;
            continue;
        }
        auto it = abbrev_map.find(code);
        if (it == abbrev_map.end()) {
            NYX_WARN("DWARF: unknown abbrev code " + std::to_string(code));
            r.pos = unit_end;
            return;
        }
        const auto& entry = *it->second;

        // Read attributes.
        std::string name;
        std::uint64_t low_pc = 0, high_pc = 0;
        bool has_low = false, has_high = false;
        std::uint64_t type_offset = 0;
        bool has_type = false;
        std::uint8_t byte_size = 0;
        bool has_byte_size = false;
        std::uint64_t encoding = 0;
        (void)encoding;

        for (const auto& [attr_name, form] : entry.attrs) {
            const auto val = read_form(r, form, 0, is_dwarf64, addr_size, debug_str);
            switch (attr_name) {
                case DW_AT_name:
                    if (val.is_string) name = val.str;
                    break;
                case DW_AT_low_pc:
                    low_pc = val.u; has_low = true;
                    break;
                case DW_AT_high_pc:
                    // high_pc can be an address (FORM_addr) or a constant
                    // (FORM_data*). For constants, it's an offset from low_pc.
                    high_pc = val.u; has_high = true;
                    break;
                case DW_AT_type:
                    type_offset = die_offset + val.u; has_type = true;
                    break;
                case DW_AT_byte_size:
                    byte_size = static_cast<std::uint8_t>(val.u); has_byte_size = true;
                    break;
                case DW_AT_encoding:
                    encoding = val.u;
                    break;
                default:
                    break;
            }
        }

        // Classify by tag.
        switch (entry.tag) {
            case DW_TAG_subprogram:
                if (has_low) {
                    DwarfFunction f;
                    f.name = name;
                    f.low_pc = low_pc;
                    if (has_high) {
                        // If high_pc is small (< 0x10000), treat as offset.
                        if (high_pc < 0x10000) f.high_pc = low_pc + high_pc;
                        else f.high_pc = high_pc;
                        f.has_range = true;
                    }
                    if (has_type) f.type_offset = type_offset;
                    funcs.push_back(std::move(f));
                }
                break;
            case DW_TAG_base_type:
                { DwarfType t; t.offset = die_offset; t.name = name;
                  t.kind = DwarfType::Kind::Base; t.byte_size = byte_size;
                  types[die_offset] = std::move(t); }
                break;
            case DW_TAG_pointer_type:
                { DwarfType t; t.offset = die_offset; t.name = name;
                  t.kind = DwarfType::Kind::Pointer; t.byte_size = byte_size;
                  if (has_type) t.pointee_offset = type_offset;
                  types[die_offset] = std::move(t); }
                break;
            case DW_TAG_typedef:
                { DwarfType t; t.offset = die_offset; t.name = name;
                  t.kind = DwarfType::Kind::Typedef;
                  if (has_type) t.pointee_offset = type_offset;
                  types[die_offset] = std::move(t); }
                break;
            case DW_TAG_structure_type:
                { DwarfType t; t.offset = die_offset; t.name = name;
                  t.kind = DwarfType::Kind::Struct;
                  if (has_byte_size) t.byte_size = byte_size;
                  types[die_offset] = std::move(t); }
                break;
            case DW_TAG_union_type:
                { DwarfType t; t.offset = die_offset; t.name = name;
                  t.kind = DwarfType::Kind::Union;
                  if (has_byte_size) t.byte_size = byte_size;
                  types[die_offset] = std::move(t); }
                break;
            case DW_TAG_enumeration_type:
                { DwarfType t; t.offset = die_offset; t.name = name;
                  t.kind = DwarfType::Kind::Enum;
                  if (has_byte_size) t.byte_size = byte_size;
                  types[die_offset] = std::move(t); }
                break;
            default:
                break;
        }

        (void)encoding;

        // If this DIE has children, the following DIEs (until a null DIE)
        // are its children. We track depth so the null DIEs are consumed.
        if (entry.has_children) {
            ++depth;
        }
    }
    r.pos = unit_end;
}

}  // namespace

// ---------------------------------------------------------------------------
// DwarfInfo queries
// ---------------------------------------------------------------------------
std::optional<DwarfLineEntry> DwarfInfo::lookup_address(std::uint64_t addr) const noexcept {
    if (lines.empty()) return std::nullopt;
    // Binary search for the largest entry with entry.address <= addr.
    auto it = std::upper_bound(lines.begin(), lines.end(), addr,
        [](std::uint64_t a, const DwarfLineEntry& e){ return a < e.address; });
    if (it == lines.begin()) return std::nullopt;
    --it;
    if (it->end_seq) return std::nullopt;
    return *it;
}

std::optional<std::string> DwarfInfo::function_name_at(std::uint64_t addr) const noexcept {
    for (const auto& f : functions) {
        if (!f.has_range) continue;
        if (addr >= f.low_pc && addr < f.high_pc) return f.name;
    }
    return std::nullopt;
}

std::string DwarfInfo::file_name(std::uint32_t idx) const noexcept {
    if (idx == 0 || idx > file_names.size()) return {};
    return file_names[idx - 1];
}

std::string DwarfInfo::resolve_type_name(std::uint64_t offset) const noexcept {
    if (offset == 0) return "void";
    // Guard against infinite cycles.
    for (std::size_t i = 0; i < 32; ++i) {
        auto it = types.find(offset);
        if (it == types.end()) return "void*";
        const auto& t = it->second;
        switch (t.kind) {
            case DwarfType::Kind::Base:
                return t.name.empty() ? "int" : t.name;
            case DwarfType::Kind::Pointer:
                return resolve_type_name(t.pointee_offset) + "*";
            case DwarfType::Kind::Typedef:
                offset = t.pointee_offset;
                continue;
            case DwarfType::Kind::Struct:
                return "struct " + (t.name.empty() ? std::string("anon") : t.name);
            case DwarfType::Kind::Union:
                return "union " + (t.name.empty() ? std::string("anon") : t.name);
            case DwarfType::Kind::Enum:
                return "enum " + (t.name.empty() ? std::string("anon") : t.name);
            case DwarfType::Kind::Function:
                return "void (*)(void)";
            case DwarfType::Kind::Unknown:
            default:
                return "void*";
        }
    }
    return "void*";
}

// ---------------------------------------------------------------------------
// parse_dwarf: locate sections, dispatch to parsers
// ---------------------------------------------------------------------------
DwarfInfo parse_dwarf(const BinaryInfo& bin, ByteView data) {
    DwarfInfo info;

    // Locate the four DWARF sections.
    ByteView debug_line{}, debug_info{}, debug_abbrev{}, debug_str{};
    for (const auto& s : bin.sections) {
        if (s.name == ".debug_line" && !s.is_nobits) {
            if (s.file_off + s.file_size <= data.size()) {
                debug_line = ByteView{data.data() + s.file_off, static_cast<std::size_t>(s.file_size)};
            }
        } else if (s.name == ".debug_info" && !s.is_nobits) {
            if (s.file_off + s.file_size <= data.size()) {
                debug_info = ByteView{data.data() + s.file_off, static_cast<std::size_t>(s.file_size)};
            }
        } else if (s.name == ".debug_abbrev" && !s.is_nobits) {
            if (s.file_off + s.file_size <= data.size()) {
                debug_abbrev = ByteView{data.data() + s.file_off, static_cast<std::size_t>(s.file_size)};
            }
        } else if (s.name == ".debug_str" && !s.is_nobits) {
            if (s.file_off + s.file_size <= data.size()) {
                debug_str = ByteView{data.data() + s.file_off, static_cast<std::size_t>(s.file_size)};
            }
        }
    }

    if (debug_line.empty() && debug_info.empty()) {
        return info;  // no DWARF
    }

    const bool le = (bin.endian == Endian::Little);

    // Parse .debug_line.
    if (!debug_line.empty()) {
        try {
            DwarfReader r(debug_line.data(), debug_line.size(), le);
            while (!r.eof()) {
                bool is_dwarf64 = false;
                const auto unit_len = r.length(is_dwarf64);
                if (unit_len == 0) break;
                const std::size_t unit_end = r.pos + unit_len;
                if (unit_end > r.size) break;
                // Save position before header_length so we can skip to the
                // line program using the declared header_length.
                const std::size_t after_unit_len = r.pos;
                const std::uint16_t version = r.u16();
                if (version < 2 || version > 4) {
                    if (version != 0) {
                        NYX_WARN("DWARF: .debug_line version " + std::to_string(version) + " not supported");
                    }
                    r.pos = unit_end;
                    continue;
                }
                const std::size_t header_len = r.offset(is_dwarf64);
                const std::size_t program_start = r.pos + header_len;
                if (program_start > unit_end) { r.pos = unit_end; continue; }
                // Parse header fields for file_names and opcode config.
                LineProgramHeader h;
                h.min_inst_length = r.u8();
                if (version >= 4) h.max_ops_per_inst = r.u8();
                h.default_is_stmt = r.u8();
                h.line_base = static_cast<std::int8_t>(r.u8());
                h.line_range = r.u8();
                if (h.line_range == 0) h.line_range = 1;
                h.opcode_base = r.u8();
                if (h.opcode_base == 0) h.opcode_base = 1;
                h.standard_opcode_lengths.resize(h.opcode_base > 0 ? h.opcode_base - 1 : 0);
                for (std::size_t i = 0; i < h.standard_opcode_lengths.size(); ++i) {
                    h.standard_opcode_lengths[i] = r.u8();
                }
                // include_directories
                while (r.pos < program_start) {
                    std::string s = r.cstr();
                    if (s.empty()) break;
                    h.include_directories.push_back(std::move(s));
                }
                // file_names
                while (r.pos < program_start) {
                    std::string name = r.cstr();
                    if (name.empty()) break;
                    (void)r.uleb(); (void)r.uleb(); (void)r.uleb();
                    h.file_names.push_back(std::move(name));
                }
                if (info.file_names.empty() && !h.file_names.empty()) {
                    info.file_names = h.file_names;
                }
                // Position the reader at the start of the line program,
                // using the declared header_length (trusts the compiler).
                r.pos = program_start;
                // Run the line number program until unit_end.
                if (r.pos < unit_end) {
                    DwarfReader sub(debug_line.data() + r.pos, unit_end - r.pos, le);
                    run_line_program(sub, h, info.lines);
                }
                r.pos = unit_end;
            }
            std::sort(info.lines.begin(), info.lines.end(),
                [](const DwarfLineEntry& a, const DwarfLineEntry& b){ return a.address < b.address; });
            info.has_info = true;
            NYX_INFO("DWARF: parsed " + std::to_string(info.lines.size()) + " line entries");
        } catch (const std::exception& e) {
            NYX_WARN(std::string("DWARF: .debug_line parse failed: ") + e.what());
        }
    }

    // Parse .debug_info.
    if (!debug_info.empty() && !debug_abbrev.empty()) {
        try {
            DwarfReader r(debug_info.data(), debug_info.size(), le);
            while (!r.eof()) {
                parse_info_unit(r, debug_abbrev, debug_str, info.functions, info.types);
            }
            info.has_info = true;
            NYX_INFO("DWARF: parsed " + std::to_string(info.functions.size()) + " functions, "
                     + std::to_string(info.types.size()) + " types");
        } catch (const std::exception& e) {
            NYX_WARN(std::string("DWARF: .debug_info parse failed: ") + e.what());
        }
    }

    return info;
}

}  // namespace nyx
