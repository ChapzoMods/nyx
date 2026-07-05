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

/// Writes a complete JSON document describing the binary metadata, the
/// disassembled sections and the decompiled functions. The schema is
/// stable across v0.0.x releases (additive only).
void write_json(std::ostream& os,
                const BinaryInfo& bin,
                const std::vector<DecompiledFunction>& functions);

/// Convenience: serialise to a string.
[[nodiscard]] std::string to_json(const BinaryInfo& bin,
                                  const std::vector<DecompiledFunction>& functions);

}  // namespace nyx::output
