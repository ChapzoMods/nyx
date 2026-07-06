// =============================================================================
// Nyx - A headless decompilation engine
// Copyright (C) 2024-2026 Chapzoo  <https://github.com/Chapzoo>
// SPDX-License-Identifier: GPL-3.0-or-later
// =============================================================================
#include "nyx/version.hpp"

#include <sstream>

namespace nyx {

std::string version_banner() {
    std::ostringstream os;
    os << "Nyx " << VERSION_STRING << " (" << VERSION_CODENAME << ")\n"
       << "A headless decompilation engine\n"
       << "Copyright (C) 2024-2026 " << AUTHOR << "\n"
       << "License GPLv3+: GNU GPL version 3 or later <https://gnu.org/licenses/gpl.html>\n"
       << "This is free software: you are free to change and redistribute it.\n"
       << "There is NO WARRANTY, to the extent permitted by law.\n"
       << "Homepage: " << HOMEPAGE;
    return os.str();
}

}  // namespace nyx
