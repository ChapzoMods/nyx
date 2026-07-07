// =============================================================================
// Nyx - A headless decompilation engine
// Copyright (C) 2024-2026 Chapzoo  <https://github.com/Chapzoo>
// SPDX-License-Identifier: GPL-3.0-or-later
// =============================================================================
#pragma once
#include "nyx/core/types.hpp"
#include "nyx/decompiler/decompiler.hpp"
#include <ostream>
#include <string>

namespace nyx::output {

/// Writes the decompiled output as compilable C source. Unlike the
/// pseudo-C writer, this output is designed to pass `gcc -c` (parse only):
/// every function gets a forward declaration and a `void` parameter list so
/// the file at least compiles to an object file (it won't link without the
/// referenced symbols, but it must parse cleanly).
void write_c(std::ostream& os, const BinaryInfo& bin, const std::vector<DecompiledFunction>& functions);

/// Convenience wrapper - returns the same output as write_c in a string.
[[nodiscard]] std::string to_c(const BinaryInfo& bin, const std::vector<DecompiledFunction>& functions);

}  // namespace nyx::output
