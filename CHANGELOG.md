# Changelog

All notable changes to Nyx are documented in this file. The format is
loosely based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and the project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html)
once 1.0.0 is reached. Pre-1.0 versions may break the public API between
minor bumps.

## [0.0.1] - 2026-07-05 - "Erebus"

First public alpha. Establishes the project skeleton, the parser / lifter /
decompiler / output module split, the test infrastructure and the CI
workflow. Not feature-complete; the decompiler output is intentionally
conservative.

### Added

- **Core module** (`include/nyx/core/`): `ByteView`, `ByteBuffer` with
  file I/O, little/big-endian readers, `Arch` enum with Capstone mapping,
  `Error` exception type with categories, thread-safe `Logger`.
- **Binary format parsers**:
  - `ElfParser` - 32 / 64-bit, LE / BE. Reads section headers, program
    headers (NX + RELRO detection), symbol tables (`.symtab` and
    `.dynsym`).
  - `PeParser` - PE32 / PE32+. Reads COFF header, optional header,
    section table, export directory, import descriptor table. Detects
    PIE / NX / dynamic base via DLL characteristics.
  - `MachOParser` - 32 / 64-bit, LE / BE, fat archives. Walks
    `LC_SEGMENT` / `LC_SEGMENT_64`, `LC_SYMTAB`, `LC_MAIN`.
- **Disassembler**: Capstone v5.0.3 wrapper supporting x86, x86-64, ARM,
  AArch64, PowerPC (32/64), MIPS (32/64). The Capstone handle is stored
  as `std::uintptr_t` so the public header does not leak Capstone types.
- **Lifter**: 24-opcode SSA-style IR with `Opaque` fallback for any
  instruction the lifter does not yet model. x86 / x86-64 lifted via
  mnemonic + operand-string parsing (no Capstone detail dependency);
  ARM / AArch64 / PPC / MIPS use the conservative generic lifter.
- **CFG builder**: splits on terminators and branch targets, resolves
  fall-through successors.
- **Decompiler**: walks function symbols, lifts each, renders pseudo-C.
  Falls back to linear sweep when the binary has no symbol table.
- **Output writers**:
  - `json_writer` - stable schema `nyx.v0.0.1`, full binary metadata +
    decompiled function bodies.
  - `text_writer` - listing-style dump (header, sections, symbols,
    disassembly).
  - `pseudo_c_writer` - pseudo-C source with one statement per IR
    instruction.
- **CLI** (`nyx` binary): `--format`, `--output`, `--log-level`,
  `--arch`, `--format-hint`, `--version`, `--help`, `--quiet`.
- **Test suite**: 50 unit tests + 11 integration tests, all green under
  ASan + UBSan.
- **Build system**: CMake 3.20+, FetchContent for Capstone and doctest,
  sanitizer and coverage toggles, install rules.
- **CI**: GitHub Actions workflow with two jobs (debug + ASan, release).
- **Local verification**: `scripts/verify.sh` reproduces CI locally.
- **Documentation**: README, CONTRIBUTING, LICENSE (GPLv3), CHANGELOG.

### Known limitations

- The lifter does not yet model ARM / AArch64 / PPC / MIPS semantics;
  those instructions become `Opaque` nodes. This is honest about the
  current state and is the primary focus of v0.0.2.
- No DWARF / PDB parsing. Source-level attribution will land in v0.0.5.
- No jump-table detection; indirect branches are opaque.
- The pseudo-C output is one-statement-per-IR-instruction; no SSA
  deconstruction, no type recovery, no structured control-flow
  reconstruction yet.
- Pseudo-C functions are always emitted as `void f(void)`; arguments
  and return types will arrive with calling-convention-aware lifting
  in v0.2.0.

### Tested on

- Debian 13 (trixie), g++ 14.2.0, CMake 4.3.4, 2 cores, 4 GB RAM.
- Reference build time: ~90 seconds cold, ~10 seconds incremental.

[0.0.1]: https://github.com/Chapzoo/nyx/releases/tag/v0.0.1
