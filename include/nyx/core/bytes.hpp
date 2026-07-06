// =============================================================================
// Nyx - A headless decompilation engine
// Copyright (C) 2024-2026 Chapzoo  <https://github.com/Chapzoo>
// SPDX-License-Identifier: GPL-3.0-or-later
// =============================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace nyx {

/// Lightweight non-owning view over a contiguous byte buffer.
/// Mirrors std::span<std::byte> but keeps the API minimal so we don't pull
/// in <span> everywhere (some toolchains still ship it imperfectly).
class ByteView {
public:
    using value_type      = std::uint8_t;
    using pointer         = const value_type*;
    using const_pointer   = const value_type*;
    using iterator        = const_pointer;
    using size_type       = std::size_t;

    constexpr ByteView() noexcept = default;
    constexpr ByteView(pointer data, size_type size) noexcept
        : data_(data), size_(size) {}

    template <std::size_t N>
    constexpr ByteView(const value_type (&arr)[N]) noexcept  // NOLINT
        : data_(arr), size_(N) {}

    template <typename Container>
        requires requires(const Container& c) {
            { c.data() } -> std::convertible_to<const_pointer>;
            { c.size() } -> std::convertible_to<size_type>;
        }
    constexpr ByteView(const Container& c) noexcept  // NOLINT
        : data_(reinterpret_cast<const_pointer>(c.data())), size_(c.size()) {}

    [[nodiscard]] constexpr pointer     data() const noexcept { return data_; }
    [[nodiscard]] constexpr size_type   size() const noexcept { return size_; }
    [[nodiscard]] constexpr bool        empty() const noexcept { return size_ == 0; }
    [[nodiscard]] constexpr iterator    begin() const noexcept { return data_; }
    [[nodiscard]] constexpr iterator    end()   const noexcept { return data_ + size_; }

    [[nodiscard]] constexpr value_type operator[](size_type i) const noexcept {
        return data_[i];
    }

    /// Returns a sub-view [start, start+len). Out-of-range returns nullopt.
    [[nodiscard]] constexpr std::optional<ByteView> sub(size_type start, size_type len) const noexcept {
        if (start > size_ || len > size_ - start) return std::nullopt;
        return ByteView{data_ + start, len};
    }

    /// Lexicographic compare - useful for tests.
    friend constexpr bool operator==(const ByteView& a, const ByteView& b) noexcept {
        if (a.size_ != b.size_) return false;
        for (size_type i = 0; i < a.size_; ++i)
            if (a.data_[i] != b.data_[i]) return false;
        return true;
    }

private:
    pointer   data_ = nullptr;
    size_type size_ = 0;
};

/// Owned mutable byte buffer - used when parsers need to materialise sections
/// or when the CLI loads a file from disk.
class ByteBuffer {
public:
    using value_type = std::uint8_t;

    ByteBuffer() = default;
    explicit ByteBuffer(std::size_t size) : data_(size, 0) {}

    ByteBuffer(std::uint8_t* p, std::size_t n)
        : data_(p, p + n) {}

    template <std::input_iterator It>
    ByteBuffer(It first, It last) : data_(first, last) {}

    [[nodiscard]] std::uint8_t*       data()       { return data_.data(); }
    [[nodiscard]] const std::uint8_t* data() const { return data_.data(); }
    [[nodiscard]] std::size_t         size() const { return data_.size(); }
    [[nodiscard]] bool                empty() const { return data_.empty(); }

    [[nodiscard]] std::uint8_t operator[](std::size_t i) const { return data_[i]; }
    [[nodiscard]] std::uint8_t& operator[](std::size_t i)      { return data_[i]; }

    void clear() { data_.clear(); }
    void resize(std::size_t n) { data_.resize(n); }

    [[nodiscard]] ByteView view() const noexcept { return ByteView{data_.data(), data_.size()}; }

    /// Load the entire contents of a path into the buffer.
    /// Returns false on I/O error (also logs via nyx::Logger).
    [[nodiscard]] static std::optional<ByteBuffer> from_file(const std::string& path);

private:
    std::vector<std::uint8_t> data_;
};

// ---------------------------------------------------------------------------
// Little-endian readers - every format Nyx parses is LE on disk at the
// magic level; we explicitly byte-swap when an ELF header marks BE.
// ---------------------------------------------------------------------------
template <typename T>
[[nodiscard]] T read_le(const std::uint8_t* p) noexcept {
    using UT = std::make_unsigned_t<T>;
    UT v = 0;
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        v |= static_cast<UT>(static_cast<UT>(p[i]) << (8u * i));
    }
    return static_cast<T>(v);
}

template <typename T>
[[nodiscard]] T read_be(const std::uint8_t* p) noexcept {
    using UT = std::make_unsigned_t<T>;
    UT v = 0;
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        v = static_cast<UT>(v << 8u) | static_cast<UT>(p[i]);
    }
    return static_cast<T>(v);
}

[[nodiscard]] inline std::uint16_t read_u16_le(const std::uint8_t* p) noexcept { return read_le<std::uint16_t>(p); }
[[nodiscard]] inline std::uint32_t read_u32_le(const std::uint8_t* p) noexcept { return read_le<std::uint32_t>(p); }
[[nodiscard]] inline std::uint64_t read_u64_le(const std::uint8_t* p) noexcept { return read_le<std::uint64_t>(p); }
[[nodiscard]] inline std::uint16_t read_u16_be(const std::uint8_t* p) noexcept { return read_be<std::uint16_t>(p); }
[[nodiscard]] inline std::uint32_t read_u32_be(const std::uint8_t* p) noexcept { return read_be<std::uint32_t>(p); }
[[nodiscard]] inline std::uint64_t read_u64_be(const std::uint8_t* p) noexcept { return read_be<std::uint64_t>(p); }

/// Hex helpers used by all output writers.
[[nodiscard]] std::string to_hex(std::uint64_t v, std::size_t min_width = 0, bool with_prefix = true);
[[nodiscard]] std::string to_hex(ByteView bytes, std::size_t bytes_per_group = 1);
[[nodiscard]] std::string to_hex_dump(ByteView bytes, std::size_t base_addr = 0, std::size_t width = 16);

}  // namespace nyx
