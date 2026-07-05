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

/// Writes the decompiled pseudo-C source for every function. Functions are
/// emitted in entry-point order; each is preceded by a small banner comment
/// with its address and instruction count.
void write_pseudo_c(std::ostream& os,
                    const BinaryInfo& bin,
                    const std::vector<DecompiledFunction>& functions);

[[nodiscard]] std::string to_pseudo_c(const BinaryInfo& bin,
                                      const std::vector<DecompiledFunction>& functions);

}  // namespace nyx::output
