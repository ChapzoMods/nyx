// =============================================================================
// Nyx - A headless decompilation engine
// Copyright (C) 2024-2026 Chapzoo  <https://github.com/Chapzoo>
// SPDX-License-Identifier: GPL-3.0-or-later
// =============================================================================
#include "nyx/core/bytes.hpp"

#include "nyx/core/error.hpp"
#include "nyx/core/logger.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>

namespace nyx {

std::optional<ByteBuffer> ByteBuffer::from_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) {
        NYX_ERROR("cannot open '" + path + "'");
        return std::nullopt;
    }
    const std::streamoff size = f.tellg();
    if (size <= 0) {
        NYX_ERROR("empty or unreadable file '" + path + "'");
        return std::nullopt;
    }
    f.seekg(0, std::ios::beg);

    ByteBuffer buf(static_cast<std::size_t>(size));
    if (!f.read(reinterpret_cast<char*>(buf.data()), size)) {
        NYX_ERROR("short read on '" + path + "'");
        return std::nullopt;
    }
    return buf;
}

std::string to_hex(std::uint64_t v, std::size_t min_width, bool with_prefix) {
    std::ostringstream os;
    if (with_prefix) os << "0x";
    os << std::setfill('0') << std::hex << std::setw(static_cast<int>(min_width)) << v;
    return os.str();
}

std::string to_hex(ByteView bytes, std::size_t bytes_per_group) {
    if (bytes.empty()) return {};
    std::ostringstream os;
    os << std::setfill('0') << std::hex;
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        if (i > 0 && bytes_per_group > 0 && (i % bytes_per_group) == 0) os << ' ';
        os << std::setw(2) << static_cast<unsigned>(bytes[i]);
    }
    return os.str();
}

std::string to_hex_dump(ByteView bytes, std::size_t base_addr, std::size_t width) {
    if (width == 0) width = 16;
    std::ostringstream os;
    os << std::setfill('0') << std::hex;

    for (std::size_t off = 0; off < bytes.size(); off += width) {
        const std::size_t n = std::min(width, bytes.size() - off);
        os << std::setw(16) << (base_addr + off) << "  ";

        // hex part
        for (std::size_t i = 0; i < width; ++i) {
            if (i < n) os << std::setw(2) << static_cast<unsigned>(bytes[off + i]);
            else       os << "  ";
            if (i == 7) os << ' ';
            os << ' ';
        }
        os << " |";
        for (std::size_t i = 0; i < n; ++i) {
            const unsigned char c = bytes[off + i];
            os << static_cast<char>((c >= 32 && c < 127) ? c : '.');
        }
        os << "|\n";
    }
    return os.str();
}

}  // namespace nyx
