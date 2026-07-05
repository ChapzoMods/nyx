# Changelog

All notable changes to Nyx are documented in this file. The format is
loosely based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and the project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html)
once 1.0.0 is reached. Pre-1.0 versions may break the public API between
minor bumps.

## [0.0.3] - 2026-07-06

Lifter expansion: real x86/x86-64, ARM32, PowerPC and MIPS lifters;
heuristic function detection via prologue scanning; structured pseudo-C
if/else hints; shared operand parser across all architectures.

### Added

- **x86/x86-64 lifter rewrite** (`InstructionLifter::lift_x86`): the ad-hoc
  inline operand parser from v0.0.1 has been replaced with a clean
  implementation built on the new shared `parse_operand` / `parse_mem_operand`
  helpers. Coverage now includes `mov`/`movzx`/`movsx`/`movsxd` (with
  reg-reg, reg-imm, reg-mem and mem-reg forms), `lea`, `add`/`sub`/`imul`/
  `mul`/`div`/`idiv`, `and`/`or`/`xor`/`shl`/`sal`/`shr`/`sar`, `inc`/
  `dec`/`neg`/`not`, `push`/`pop`, `cmp`/`test`, `jmp`/`jcc`, `call`,
  `ret`/`retn`/`retq`, `leave`, `syscall`/`int`, plus all size variants
  (`movb`/`movw`/`movl`/`movq`). Memory operands with size overrides
  (`dword ptr [...]`) are now handled correctly. `mov reg, [mem]` is
  correctly lifted as `OpCode::Load` instead of `OpCode::Mov`.
- **ARM32 lifter rewrite** (`InstructionLifter::lift_arm32`): real operand
  parser covering `mov`/`movw`/`movt`/`mvn`/`neg`/`rsb`, `add`/`sub`/`mul`/
  `and`/`orr`/`eor`/`lsl`/`lsr`/`asr`, `ldr`/`ldrb`/`ldrh`/`ldrd`, `str`/
  `strb`/`strh`/`strd`, `cmp`/`cmn`/`tst`, `b`/`bl`/`bx`/`blx`, `b.<cond>`,
  `cbz`/`cbnz`, `push`/`pop`, `nop`. Anything else falls back to `Opaque`.
- **PowerPC 32/64 lifter** (`InstructionLifter::lift_ppc`): basic coverage of
  `add`/`addi`/`addis`/`li`/`lis`, `sub`/`subf`/`subfic`, `mullw`/`mulli`/
  `mulhw`, `divw`/`divwu`/`divd`, `and`/`andc`, `or`/`mr`/`orc`, `xor`/`eqv`,
  `slw`/`sld`, `srw`/`srd`, `sraw`/`srad`, `lwz`/`lhz`/`lbz`/`ld`/`lwa`,
  `stw`/`sth`/`stb`/`std`, `cmp`/`cmpl`/`cmpw`/`cmplw`/`cmpi`/`cmpli`,
  `b`/`bl`/`bla`, `bc`/`bclr`/`bcctr`, `blr` (return), `sc`, `nop`, `mflr`,
  `rlwinm`/`slwi`/`srwi` (approximated as shifts). PPC `crf` field in `cmp`
  is skipped automatically.
- **MIPS 32/64 lifter** (`InstructionLifter::lift_mips`): basic coverage of
  `addiu`/`addi`/`addu`/`add`, `subu`/`sub`, `mult`/`multu`/`mul`, `div`/
  `divu`, `and`/`andi`, `or`/`ori`, `xor`/`xori`, `nor`, `sll`/`sllv`/
  `srl`/`srlv`/`sra`/`srav`, `lw`/`lh`/`lb`/`lhu`/`lbu`/`lwc1`/`ldc1`,
  `sw`/`sh`/`sb`/`swc1`/`sdc1`, `beq`/`bne`/`bgtz`/`blez`/`bltz`/`bgez`,
  `j`/`jal`/`jr`/`jalr`, `lui` (with 16-bit shift), `slt`/`slti`/`sltu`/
  `sltiu`, `movz`/`movn`, `nop`/`ssnop`, `syscall`/`break`. `jr $ra` is
  correctly lifted as `OpCode::Return`; `jr` to any other register stays
  `Opaque`. `beq`/`bne` conditional branches extract the target from the
  last operand (Capstone places it third) instead of relying on
  `direct_target()` which expects the target first.
- **FunctionDetector** (`include/nyx/decompiler/function_detector.hpp`): a
  new module that scans a linear-sweep disassembly looking for well-known
  function prologue patterns. Supported prologues:
  - x86/x86-64: `endbr64; push rbp`, `push rbp`/`push ebp`/`push rbx`,
    `sub rsp, imm`
  - AArch64: `stp x29, x30, [sp, ...]`, `stp fp, lr, [sp, ...]`,
    `sub sp, sp, #imm`, `mov x29, sp`
  - ARM32: `push {... lr}`, `sub sp, sp, #imm`, `mov fp, sp`, `stmfd sp!, {... lr}`
  - PowerPC: `stwu r1, -imm(r1)`, `mflr r0`, `stw r0, imm(r1)`
  - MIPS: `addiu $sp, $sp, -imm`, `sw $ra, imm($sp)`, `sw $fp, imm($sp)`,
    `daddiu $sp, $sp, -imm`
  The Decompiler now uses FunctionDetector when no function symbols exist:
  each prologue candidate becomes a function whose body extends to the next
  candidate. Functions are named `sub_<hex_addr>`.
- **Shared operand parser**: `split_ops`, `parse_imm`, `is_imm_token`,
  `is_mem_token`, `parse_mem_operand` and `parse_operand` are now free
  functions in an anonymous namespace, reused by all five architecture
  lifters. This eliminates ~200 lines of duplicated code and ensures
  consistent operand handling across architectures. The parser now
  recognises MIPS/PPC `disp(reg)` memory syntax in addition to x86/ARM
  `[base+disp]` syntax.
- **Structured pseudo-C if/else hints**: `render_pseudo_c` now detects the
  classic if/else diamond pattern (BranchCond whose target also branches
  to the same join block as the fall-through) and annotates the BranchCond
  with a `// else-branch` comment so readers can spot the structure.
  Full structured `if/else` reconstruction (without gotos) is still on
  the v0.1.0 roadmap.
- **MIPS32 and PPC32 ELF fixtures**: `tests/fixtures/gen_mips_elf.py` and
  `gen_ppc_elf.py` generate minimal big-endian ELF executables with a tiny
  `.text` section. The MIPS fixture has 7 instructions (`addiu`/`sw`/`jal`/
  `nop`/`lw`/`jr`/`nop`); the PPC fixture has 8 (`stwu`/`mflr`/`stw`/`addi`/
  `lwz`/`mtlr`/`addi`/`blr`).
- **46 new tests**: 15 unit tests for the x86 lifter, 10 for ARM32, 10 for
  PPC, 11 for MIPS, plus 7 integration tests covering the MIPS/PPC ELF
  pipelines, the FunctionDetector on every supported architecture, and the
  pseudo-C if/else pattern. Test count: 106 unit + 24 integration
  (was 60 + 17).

### Changed

- `InstructionLifter::lift_arm` (v0.0.2) is now split into `lift_arm64`
  (real lifter, unchanged behaviour) and `lift_arm32` (real lifter, was
  conservative). Both use the shared operand parser.
- `lift_operand_x86` (the sentinel stub from v0.0.1) has been removed.
- The Decompiler's linear-sweep fallback now disassembles the entire `.text`
  section (capped at 20 000 instructions) and runs FunctionDetector over
  the result, instead of treating the whole section as a single function.
- `parse_mem_operand` now handles the `disp(reg)` form used by MIPS and
  PowerPC, in addition to the `[base+disp]` form used by x86 and ARM.
- Output writers and `nyx --version` now report `0.0.3`.

### Fixed

- **Bug**: `mov reg, [mem]` on x86/x86-64 was lifted as `OpCode::Mov` with
  a memory source, which produced nonsensical pseudo-C (`v1 = *(...)(...)`
  instead of `v1 = *(...)(...)` via Load). Now correctly lifted as
  `OpCode::Load`.
- **Bug**: `parse_mem_operand` did not recognise memory operands where the
  displacement precedes the register in parentheses (`0x10($sp)` /
  `0x14(r1)`), causing MIPS `sw`/`lw` and PPC `lwz`/`stw` to be lifted as
  Opaque. The parser now splits on `(` and treats the prefix as the
  displacement.
- **Bug**: `parse_mem_operand` did not strip the `#` prefix from ARM/MIPS
  immediate displacements inside brackets (`[x1, #0x10]`), so the
  displacement was treated as a register name. Now correctly parsed.
- **Bug**: MIPS `beq`/`bne` conditional branches were always lifted as
  `Opaque` because `DecodedInstruction::direct_target()` requires the
  target to be the first operand (it is the third for MIPS). The lifter
  now falls back to parsing the last operand as the target.
- **Bug**: `FunctionDetector::is_x86_prologue` flagged `mov rbp, rsp` as a
  function start, but that instruction is the *second* instruction of the
  canonical x86 prologue, not the first. This caused false positives in the
  middle of functions. The detector no longer flags standalone
  `mov rbp, rsp`.
- **Bug**: `parse_operand` did not recognise `disp(reg)` as a memory
  operand (only `[...]` and leading `(...)`), so MIPS/PPC load/store
  operands were treated as registers.

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
