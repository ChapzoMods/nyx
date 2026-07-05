# Changelog

All notable changes to Nyx are documented in this file. The format is
loosely based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and the project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html)
once 1.0.0 is reached. Pre-1.0 versions may break the public API between
minor bumps.

## [0.0.2] - 2026-07-05

Robustness pass: real ARM64 lifter, multi-slice fat archives, BSS-aware ELF
parsing, ordinal imports in PE, structured pseudo-C, auto-detection
feedback, and 21 new tests (10 unit + 5 integration + 6 fixture-driven).

### Added

- **ARM64 lifter** (`InstructionLifter::lift_arm64`): real operand parser
  that translates the common AArch64 instruction set to the IR -
  `mov`, `movz`, `movk`, `movn`, `add`, `sub`, `mul`, `and`, `or`, `eor`,
  `lsl`, `lsr`, `asr`, `udiv`, `sdiv`, `neg`, `mvn`, `ldr`, `ldrsw`,
  `ldrb`, `ldrh`, `ldur`, `str`, `strb`, `strh`, `stur`, `ldp`, `stp`,
  `cmp`, `cmn`, `tst`, `b`, `bl`, `blr`, `b.<cond>`, `cbz`, `cbnz`,
  `tbz`, `tbnz`, `adrp`, `adr`, `ret`, `nop`. Anything else still falls
  back to `OpCode::Opaque`.
- **Mach-O fat archives**: the parser now parses EVERY slice in a
  universal binary and exposes them via `BinaryInfo::slices`. The primary
  `BinaryInfo` describes the first recognised slice (preserving
  backwards compatibility); the remaining slices are fully populated.
  `FAT_MAGIC_64` (64-bit fat archives) is now supported alongside
  `FAT_MAGIC`.
- **ELF BSS handling**: sections of type `SHT_NOBITS` (`.bss`, `.tbss`)
  are now marked `Section::is_nobits = true` and have `file_size = 0`.
  The decompiler and text dumper skip them when looking for code bytes,
  preventing out-of-bounds reads on binaries with large BSS segments.
- **PE ordinal imports**: import-by-ordinal entries now populate
  `Symbol::ordinal` (the low-16 ordinal value) and `Symbol::module`
  (the owning DLL name), so consumers can distinguish them from
  name-based imports.
- **Auto-detection feedback**: the CLI now logs the auto-detected
  format/arch at INFO level, so users see what Nyx inferred from the
  header when `--arch` is not passed.
- **Structured pseudo-C**: `render_pseudo_c` now emits `if (cond) goto L;`
  for `BranchCond` terminators and suppresses redundant `goto` when the
  target is the immediately-following block. This makes the output
  significantly more readable for functions with simple if/else CFGs.
- **AArch64 fixture**: `tests/fixtures/gen_arm64_macho.py` generates a
  minimal AArch64 Mach-O with `mov x0,#0; mov x1,#1; add x2,x0,x1; ret`.
- **21 new tests**: 10 unit tests for the ARM64 lifter
  (`test_arm64_lifter.cpp`), 2 integration tests for the ARM64 pipeline
  (`test_pipeline_arm64.cpp`), and 4 integration tests for v0.0.2
  features (`test_v002_features.cpp`: BSS, PE ordinals, fat archives,
  pseudo-C if/else). Test count: 60 unit + 15 integration (was 50 + 11).

### Changed

- `InstructionLifter::lift_arm` is now split into `lift_arm64` (real
  lifter) and `lift_arm32` (still conservative, control-flow only).
  ARMv7 gets the conservative path; AArch64 gets the full lifter.
- The `Operand::imm` and `Operand::label` member names were renamed to
  `imm_value` and `label_addr` in v0.0.1 to avoid clashing with the
  factory methods of the same name. v0.0.2 keeps that change.
- Output writers now pull the version string from `version.hpp` instead
  of hardcoding "v0.0.1", so future bumps don't require touching three
  source files.
- `Symbol` gained two new fields (`ordinal`, `module`); existing code
  that default-constructs `Symbol` is unaffected.

### Fixed

- **Bug**: Mach-O fixture generator had `sizeofcmds` patched at the wrong
  offset (16 instead of 20), producing a header where `ncmds` was
  accidentally set to the byte count of the load commands and
  `sizeofcmds` stayed 0. Fixed by patching at offset 20. The bug was
  masked in v0.0.1 because the x86-64 fixture's `ncmds=2` happened to
  coincide with the parser's tolerance; the AArch64 fixture exposed it.
- **Bug**: `InstructionLifter::parse_imm` used `std::stoll` which throws
  `std::out_of_range` on 64-bit immediates like `0xffffffffffffffff`.
  Switched to `std::stoull` with a try/catch, so the lifter no longer
  aborts on full-range immediates.
- **Bug**: `PeParser` had a duplicate `case IMAGE_FILE_MACHINE_MIPS:`
  that shared a label with `IMAGE_FILE_MACHINE_R4000` (same value).
  Removed the redundant case to silence the compiler warning.
- **Bug**: text writer left sticky `std::hex` formatting on the stream
  after printing raw bytes, corrupting the mnemonic column ("ret00000"
  instead of "ret   "). Reset to `std::dec` / `std::setfill(' ')`
  before the mnemonic.
- **Bug**: `lift_arm` (now `lift_arm64`) used the unsupported C++ syntax
  `if (m == "bl" && (auto t = ...))` which doesn't compile. Refactored
  to a clean `std::optional<std::uint64_t> t = ...` form.

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
