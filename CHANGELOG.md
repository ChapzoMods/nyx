# Changelog

All notable changes to Nyx are documented in this file. The format is
loosely based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and the project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html)
once 1.0.0 is reached. Pre-1.0 versions may break the public API between
minor bumps.

## [0.2.2] - 2026-07-07

Hotfix: function merging fix, WASM end-to-end, C calling conventions,
dot legend, region builder duplicate block fix, goto-to-break/continue
for BranchCond.

### Fixed

- **Bug C (CRITICAL): functions mixed together** — When a binary has a
  symbol table with known function sizes (ELF/PE), the decompiler now
  limits the bytes read to `sym->size` instead of a generous upper bound.
  This prevents blocks from consecutive functions (e.g. `main` leaking
  into `factorial`) from being included in the wrong function's IR.
  Applied to both `decompile()` and `decompile_ir()`.
- **Bug A (duplicate blocks in region builder)** — Added a final cleanup
  pass in `structure_cfg` that removes any `Block` children from the root
  sequence whose index is in the `assigned` set. Catches residual
  duplicates that slipped through the main walk.
- **Bug D (gotos inside structures)** — `render_region` now also handles
  `BranchCond` instructions targeting a loop header (→ `if (cond)
  continue;`) or post-loop address (→ `if (!(cond)) break;`), not just
  unconditional `Branch`.

### Added

- **Arch::Wasm and BinaryFormat::Wasm** — WASM is now a first-class
  architecture and format in the enum system. `WasmParser::parse()` sets
  `bin.arch = Arch::Wasm` and `bin.format = BinaryFormat::Wasm`. The
  `arch_name()` returns `"wasm"`, `arch_pretty()` returns `"WebAssembly"`.
  Capstone mapping returns `nullopt` (WASM uses its own lifter).
- **C writer with calling conventions** — `--format c` now emits function
  signatures with parameters derived from the calling convention
  (`int add(int param1, int param2, int param3, int param4);` instead of
  `void add(void);`). Return type is resolved from DWARF when available,
  defaulting to `int`.
- **Dot legend** — The extended DOT output now includes a legend node
  with a colour key (Entry, Loop header, Loop body, Unreachable, dashed =
  back edge).

### Changed

- Output writers and `nyx --version` now report `0.2.2`.
- WASM files now show `"format": "wasm"` and `"arch": "wasm"` in JSON
  output instead of `"unknown"`.

## [0.2.1] - 2026-07-07

Unified hotfix + 0.3.0 features: region builder bugs fixed and connected
to CLI, WASM parser connected to BinaryParser::detect(), symbol resolution
in calls, C output format, WASM lifter, else-if chains, and more.

### Added

- **C output format** (`--format c`): new writer (`c_writer.hpp/cpp`)
  that generates compilable C code with function declarations, global
  variable declarations, and sanitized bodies. Output compiles with
  `gcc -c` (warnings only, no errors).
- **WASM lifter** (`wasm_lifter.hpp/cpp`): stack-based lifter that
  translates WASM bytecodes (local.get/set, i32.const, i32.add/sub/mul,
  call, br, br_if, return, end, etc.) to Nyx IR. Unknown bytecodes
  become Opaque.
- **Else-if chains in region builder**: `structure_cfg` now detects
  chains of `if (...) { ... } else if (...) { ... } else { ... }` by
  recursively checking if the else-block itself contains a BranchCond
  diamond. Handles arbitrary-depth chains.
- **Symbol resolution in calls**: pseudo-C `render_instruction` now
  resolves call targets to symbol names via `BinaryInfo::symbols` when
  available (`call(add)` instead of `call(0x401126)`). Also applied to
  the text writer (`; add` annotation after `call` instructions).
- **WASM detection in BinaryParser::detect()**: WASM files (`\0asm`
  magic) are now auto-detected and processed by `WasmParser` without
  needing `--format-hint`.
- **12 new tests**: 7 WASM lifter unit tests, 4 C writer unit tests,
  1 else-if chain region test. Test count: 208 unit + 40 integration
  (was 196 + 40).

### Changed

- **Region builder connected to CLI**: pseudo-C output now uses
  `render_structured()` by default (not just with `-O1`), producing
  `if/else/while/do-while` instead of `goto`/labels. Falls back to
  `render_pseudo_c()` when structuring produces empty output.
- **`--format c`** is now a separate format from `pseudo-c` (previously
  `c` was an alias for pseudo-c).
- Output writers and `nyx --version` now report `0.2.1`.

### Fixed

- **Bug A (region builder duplicated blocks)**: blocks assigned to an
  if/else or loop region are now tracked in an `assigned` set and
  skipped in the main walk, preventing duplicate rendering.
- **Bug B (do-while with garbage vreg)**: infinite loops (back edge
  without condition) now emit `while (1)` instead of
  `while(v4294967295)`. A `render_condition()` helper returns `"1"`
  when the condition is `nullopt` or `INVALID_VREG`.
- **Bug D (gotos inside structures)**: `goto` instructions targeting
  a loop header are now rendered as `continue;`; those targeting the
  block after a loop are rendered as `break;`. A `LoopContext` is
  threaded through `render_region` to track these addresses.
- **If/else detector false positives**: the if/else detector no longer
  claims loop-header blocks as if/else children (guards added for
  `same_target` and `loop_header` successors).

## [0.2.0] - 2026-07-07

Region structuring, SSA optimizations, WebAssembly support, and
improvements to DWARF and dot output.

### Added

- **Region structuring** (`include/nyx/lifter/region_builder.hpp`): new
  module that transforms the CFG into a high-level region tree
  (if/else, while, do-while, switch, sequence). Uses a simplified
  interval-based approach leveraging the dominator tree and natural
  loop detection from v0.0.5. Irreducible regions fall back to `goto`.
  `render_structured()` produces pseudo-C without gotos where possible.
- **SSA optimizations** (`include/nyx/lifter/ssa_optimizer.hpp`): three
  safe optimization passes applied at fixpoint:
  - **Constant folding**: evaluates arithmetic on immediates
    (`2 + 3` → `5`).
  - **Expression simplification**: `v1 + 0` → `v1`, `v1 * 1` → `v1`,
    `v1 * 0` → `0`, `v1 & 0` → `0`, etc.
  - **Dead code elimination**: removes instructions whose result is
    never used and that have no side effects.
  Enabled with `-O1` / `--optimize` CLI flag.
- **WebAssembly (WASM) parser** (`include/nyx/parsers/wasm_parser.hpp`):
  pure C++20 parser for the WASM binary format. Supports:
  - Header (magic + version)
  - Type section (function signatures)
  - Function section (type index mapping)
  - Export section (function/table/memory/global exports)
  - Code section (function bodies with locals)
  - LEB128 decoding (unsigned and signed)
  Robust: truncated or corrupt sections are skipped without crashing.
- **CLI flag `-O1`**: enables SSA optimizations during decompilation.
- **17 new tests**: 7 SSA optimizer, 5 region builder, 5 WASM parser.
  Test count: 196 unit + 40 integration (was 179 + 40).

### Changed

- The CLI now supports `-O1` / `-O` / `--optimize` for SSA optimizations.
- When `-O1` is active, the decompiler runs `optimize()` on each IR
  function before rendering pseudo-C.
- Output writers and `nyx --version` now report `0.2.0`.
- `build.sh` generates `sample.wasm` via `gen_wasm.py`.

### Fixed

- **Bug**: WASM unit test had incorrect section size (5 instead of 6
  for the Type section), causing the function section to be skipped.
- **Bug**: SSA optimizer test expected `v1 = Mov(5)` to survive after
  DCE, but v1 was unused after v2 was eliminated. Fixed to check total
  instruction count reduction instead.

## [0.1.0] - 2026-07-07 - First beta

SSA deconstruction, calling convention detection, and Python bindings.
This is the first release in the v0.x.0 "Nyx" series (beta).

### Added

- **SSA deconstruction** (`include/nyx/lifter/ssa_builder.hpp`): new
  module that transforms the IR into Static Single Assignment form using
  the standard Cytron et al. algorithm:
  1. `compute_dominance_frontiers()` computes DF for every block from
     the dominator tree (precomputed by `DominatorAnalysis`).
  2. Phi instructions are inserted at the dominance frontier of every
     definition site, using a worklist algorithm.
  3. Variables are renamed via a depth-first walk of the dominator tree
     with per-variable version stacks.
  The result (`SSAResult`) contains the SSA-transformed function plus
  version/original mappings for debugging. Phi nodes are modeled as
  `OpCode::Opaque` with a `phi:` prefix in the raw_mnemonic.
- **Calling convention detection**
  (`include/nyx/decompiler/calling_convention.hpp`): new module
  identifying the ABI per architecture:
  - x86-64: System V AMD64 (rdi, rsi, rdx, rcx, r8, r9 → rax)
  - x86: Generic cdecl (all stack, eax)
  - AArch64: AAPCS (x0-x7 → x0)
  - ARM32: AAPCS (r0-r3 → r0)
  - MIPS32: O32 ($a0-$a3 → $v0)
  - MIPS64: N64 ($a0-$a7 → $v0)
  - PPC: SVR (r3-r10 → r3)
  `register_to_param_index()` maps register names to parameter indices;
  `param_name()` and `retval_name()` produce semantic names.
- **Python bindings** (`bindings/python/nyx_python.cpp`): optional
  pybind11 module built with `-DNYX_BUILD_PYTHON=ON`. Exposes:
  - Enums: `Arch`, `BinaryFormat`
  - Classes: `BinaryInfo`, `Section`, `Symbol`, `DecompiledFunction`
  - Functions: `load(path)` → BinaryInfo, `decompile_file(path, format)`
    → string output for json/text/pseudo-c/annotated/dot
  - `__version__` attribute = "0.1.0"
  Example script at `bindings/python/example.py`.
- **7 new tests**: 6 SSA builder unit tests (empty function, single
  block, diamond CFG with phi, dominance frontiers, original mapping,
  loop with phi at header) + 8 calling convention unit tests (SysV
  AMD64, AAPCS ARM64/ARM32, MIPS O32, PPC SVR, register_to_param_index,
  param_name, to_string, unknown arch). Test count: 179 unit + 40
  integration (was 164 + 40).

### Changed

- `ir::Instruction` gained an `indirect` flag (v0.0.5); the SSA builder
  preserves it during renaming.
- `ir::Builder` gained `branch_indirect()` and `call_indirect()`
  (v0.0.5); the SSA builder renames their register operands.
- CMake gained `NYX_BUILD_PYTHON` option (default OFF). When ON,
  pybind11 is fetched via FetchContent, and both the `nyx` library and
  Capstone are built with `POSITION_INDEPENDENT_CODE=ON` so the shared
  module links correctly.
- Output writers and `nyx --version` now report `0.1.0`.

### Fixed

- **Bug**: `build_ssa` crashed on empty functions (no blocks) because it
  tried to access `fn.blocks[0]` before checking for emptiness. Added
  an early return guard.
- The `register_to_param_index` function was initially a member of
  `CallConventionInfo` in the test; corrected to the free function
  declared in the header.

## [0.0.6] - 2026-07-06

DWARF v4 debug info parsing, annotated disassembly output, DWARF-enhanced
type inference, and robustness fixes.

### Added

- **DWARF v4 parser** (`include/nyx/parsers/dwarf_parser.hpp`): pure C++20
  parser for `.debug_line`, `.debug_info`, `.debug_abbrev`, `.debug_str`
  — no libdwarf dependency. Extracts:
  - Line tables: PC → (file, line, column) via the DWARF line number
    program state machine (DWARF v2-v4, DWARF32/DWARF64, both endiannesses).
  - Function names + address ranges (low_pc/high_pc) from `.debug_info`
    DIEs (DW_TAG_subprogram).
  - Type information: base types (DW_TAG_base_type), pointers
    (DW_TAG_pointer_type), typedefs (DW_TAG_typedef), structs, unions,
    enums — with DIE offset tracking for cross-references.
  - `DwarfInfo::lookup_address()` binary-searches the line table.
  - `DwarfInfo::function_name_at()` finds the containing function.
  - `DwarfInfo::resolve_type_name()` follows typedef/pointer chains.
- **`BinaryInfo::dwarf`**: `std::shared_ptr<DwarfInfo>` populated
  automatically by `BinaryParser::load_and_parse` when `.debug_*`
  sections are present. `BinaryParser::load_dwarf()` can be called
  explicitly with `--debug-info`.
- **Annotated output** (`--format annotated`): interleaves source lines
  from DWARF with disassembled instructions (`// main.c:42` before each
  line change), similar to `objdump -S`. Falls back to plain disassembly
  with `; :line` suffixes when no DWARF is available.
- **DWARF text output enrichment**: the `--format text` writer now
  appends `; file.c:line:col` to each instruction when DWARF is available.
- **DWARF type inference**: `TypeInferer::type_from_dwarf()` resolves
  `DW_AT_type` DIE references (base types → Int8/16/32/64, pointers →
  Ptr, typedefs followed to their target). `function_return_type()`
  returns the C return type of a named function from DWARF.
- **CLI flags**: `--debug-info` / `--dwarf` forces DWARF loading.
- **`sample.debug.elf` fixture**: built with `-gdwarf-4` for DWARF
  integration tests.
- **13 new tests**: 10 DWARF parser unit tests, 3 annotated writer unit
  tests, 6 v0.0.6 integration tests (DWARF loading, text annotations,
  annotated output, DWARF functions, type inference, CLI). Test count:
  164 unit + 40 integration (was 151 + 34).

### Changed

- `BinaryParser::load_and_parse` now auto-loads DWARF when `.debug_*`
  sections are present.
- `TypeInferer` gained `type_from_dwarf()` and `function_return_type()`.
- The text and annotated writers check `bin.dwarf` for source annotations.
- Output writers and `nyx --version` now report `0.0.6`.
- DWARF form `DW_FORM_flag` (0x19) is treated as `flag_present` (0 bytes)
  in DWARF v4+ to match modern compiler output.

### Fixed

- **Bug**: DWARF `.debug_line` parser used manual header field reading
  without trusting `header_length`, causing the line program to start at
  the wrong offset. Now uses `header_length` to position the reader
  precisely at the program start.
- **Bug**: Division by zero in the line program state machine when
  `line_range` or `max_ops_per_inst` was 0 (corrupt section). Added
  guards.
- **Bug**: `DW_FORM_flag` (0x19) was treated as 1-byte flag in DWARF v4+,
  but modern compilers emit `DW_FORM_flag_present` (same code, 0 bytes).
  This desynchronised the entire `.debug_info` parse after the first
  `DW_AT_external` attribute, causing most subprogram DIEs to be missed.
- **Bug**: DWARF DIEs with `has_children=true` were not tracked for depth,
  causing child DIEs to be misinterpreted as siblings and aborting the
  unit on unknown abbrev codes. Now tracks depth via null DIE consumption.
- **Bug**: Missing DWARF v4 forms (`DW_FORM_sec_offset` 0x17,
  `DW_FORM_exprloc` 0x18, `DW_FORM_strx` 0x1A, `DW_FORM_addrx` 0x1B,
  `DW_FORM_strx1-4`) caused the parser to abort on the first DIE using
  them. All are now handled.
- **Pre-existing warnings** in `macho_parser.cpp` (`r16` unused,
  `strtab_size` set-but-not-used, `vmaddr`/`vmsize`/`fileoff` unused)
  silenced.

## [0.0.5] - 2026-07-06

CFG analysis: dominator tree, natural loop detection, jump table
detection, indirect branch marking, unreachable-block pruning, and
structured `while`/`continue` in pseudo-C.

### Added

- **Dominator analysis** (`include/nyx/lifter/cfg_analysis.hpp`): new
  module computing the immediate-dominator tree using the iterative
  Cooper-Harvey-Kennedy algorithm (O(n²) worst case, but faster than
  Lengauer-Tarjan for the small functions Nyx handles). Exposes
  `DominatorAnalysis::idom`, `immediate_dominator()`, `dominates()`,
  and the reverse-postorder traversal `rpo`.
- **Natural loop detection** (`find_natural_loops`): identifies back
  edges (B -> H where H dominates B) and collects the loop body for
  each. Returns `NaturalLoop { header, latch, body }`.
- **Reachability pruning** (`reachable_blocks`): BFS from the entry
  returns the set of reachable block indices. The pseudo-C and DOT
  renderers use this to mark unreachable blocks as pruned.
- **Jump table detection** (`include/nyx/decompiler/jump_table_detector.hpp`):
  scans for compiler-generated switch tables. x86: detects `lea reg,
  [rip + disp]; jmp reg` patterns and resolves the table base from the
  RIP-relative displacement, then reads entries from the binary until
  a target falls outside any executable section. ARM64: detects `br xN`
  preceded by `ldr xN, [xM, xK, lsl #3]` (table base resolution is
  partial). Returns `JumpTable { branch_addr, table_addr, entry_count,
  entry_size, targets }`.
- **Indirect branch/call marking**: `ir::Instruction` gained an
  `indirect` flag. The lifters now emit `branch_indirect` / `call_indirect`
  for: x86 `jmp reg` / `call reg`, ARM64 `br xN` / `blr xN`, ARM32
  `bx rN` / `blx rN`, MIPS `jr $tN` / `jalr $tN`. The pseudo-C renderer
  emits `// indirect branch via <vreg>` comments and `goto *(...)` /
  `call(*(..))` syntax for these.
- **Structured loop output in pseudo-C**: `render_pseudo_c(fn, dom, loops)`
  wraps loop headers in `while (1) { ... }`, renders back edges as
  `continue;` (or `if (cond) continue;` for conditional back edges),
  and renders loop-exit branches as `if (!(cond)) break;`. The simple
  `render_pseudo_c(fn)` overload delegates with empty analysis.
- **Extended DOT output**: `write_dot(fn, dom, loops)` colours nodes:
  entry = lightgreen, loop header = lightyellow, loop body = lightblue,
  unreachable = lightgrey. Back edges are drawn dashed with label
  "back edge". Indirect branches emit a dotted edge to a virtual
  "indirect" node.
- **23 new tests**: 10 CFG analysis unit tests (dominators, loops,
  reachability), 9 jump table + indirect branch unit tests, 5 v0.0.5
  integration tests (dot with loops, pseudo-C while/continue,
  dominators on ARM64, entry colour, unreachable pruning). Test count:
  151 unit + 34 integration (was 133 + 29).

### Changed

- `ir::Instruction` gained a `bool indirect` field (default false).
- `ir::Builder` gained `branch_indirect(Operand)` and `call_indirect(Operand)`.
- All architecture lifters now emit indirect markers for register branches.
- `render_pseudo_c` has a new 3-argument overload accepting dominator
  and loop analysis; the Decompiler uses it for all function rendering.
- The CLI `--format dot` now computes dominators + loops per function
  and uses the extended DOT writer with colours.
- `bx lr` on ARM32 is now correctly lifted as `Return` (was `Opaque`).
- Output writers and `nyx --version` now report `0.0.5`.

### Fixed

- **Bug**: `bx lr` (ARM32 return) was lifted as `Opaque` in v0.0.3/v0.0.4.
  Now correctly lifted as `OpCode::Return`. The `test_arm32_lifter`
  expectation was updated.
- **Bug**: ARM64 `br xN` (indirect branch) was lifted as `Opaque`. Now
  lifted as `OpCode::Branch` with `indirect = true`.
- **Bug**: MIPS `jr $tN` (indirect branch to non-`$ra`) was lifted as
  `Opaque`. Now lifted as `OpCode::Branch` with `indirect = true`.
- **Bug**: ARM32 `bx rN` (indirect branch to non-`lr`) was lifted as
  `Opaque`. Now lifted as `OpCode::Branch` with `indirect = true`.

## [0.0.4] - 2026-07-06

Type-shape detection, Graphviz DOT output, extended FunctionDetector, and
a critical CFG builder fix.

### Added

- **TypeInferer** (`include/nyx/decompiler/type_inferer.hpp`): a new module
  that walks an IR function and populates `Function::vreg_types` with
  inferred primitive types. Strategy:
  - `Mov` from an immediate infers `Int32` (or `Int64` if the value needs
    more than 32 bits).
  - `Mov` from a symbol infers from the symbol's size (1/2/4/8 -> Int8/
    Int16/Int32/Int64); function symbols -> Func.
  - `Load` uses the memory operand's `mem_size` hint; if absent and the
    displacement matches a known symbol's address, infers from that
    symbol's size.
  - `BinOp` results take the wider of the two operand types.
  - `Cmp` results are `Int32` (C int).
  - `Pop` results are `Ptr` (pointer-sized).
  - `Call` results default to `Int32`.
  The inferer is conservative: vregs it can't classify stay `Unknown`
  (renders as `void*`). It never asserts a wrong type.
- **IR Type lattice** (`ir::Type`): `Unknown`, `Int8`, `Int16`, `Int32`,
  `Int64`, `Ptr`, `Func`. Helpers `type_name`, `type_c_decl`,
  `type_size` for rendering and sizing. `Function::vreg_types` is a
  `std::unordered_map<VReg, Type>` populated by TypeInferer.
- **Typed pseudo-C declarations**: `render_pseudo_c` now emits a block of
  variable declarations at the top of every function, one per vreg,
  using the inferred C type (`int v1;`, `char v2;`, `long long v3;`,
  `void* v4;`). VRegs without an inferred type default to `void*`.
- **DOT/Graphviz output** (`--format dot`): new output writer that
  renders a function's CFG as a `digraph`. Each basic block is a node
  whose label lists its IR instructions; edges are labelled `cond <vreg>`
  for conditional branches, `fall-through` for implicit edges, or
  unlabelled for unconditional jumps. Output is pipe-friendly to
  `dot -Tpng` / `dot -Tsvg`. `nyx::output::write_dot` accepts a single
  `ir::Function` or a vector (multiple functions wrap into one digraph).
- **`Decompiler::decompile_ir`**: new method that returns the raw
  `std::vector<ir::Function>` instead of pre-rendered pseudo-C lines.
  Used by the DOT writer and available for downstream tooling.
- **Extended FunctionDetector prologues**:
  - ARM32: `stmfd sp!, {lr}`, `str lr, [sp, #-imm]!`, `add ip, sp, #imm`
    (PIC prologue).
  - PowerPC: `stwu` with any base register, `stfd` (FPR save).
  - MIPS: `sw $gp, imm($sp)` (PIC), `lui $gp, %hi(...)` (PIC setup),
    `sd $ra, imm($sp)` (MIPS64 doubleword save).
- **Function end detection** (`FunctionDetector::find_function_end`):
  walks forward from a candidate until it hits a return instruction
  (`ret`, `blr`, `jr $ra`, `bx lr`, `pop {... pc}`) or the next prologue.
  The Decompiler now uses this to bound function bodies in heuristic
  mode, so padding / next-function code no longer leaks into the current
  function's IR.
- **28 new tests**: 7 TypeInferer unit tests, 5 DOT writer unit tests,
  13 FunctionDetector v0.0.4 unit tests (new prologues + end detection),
  5 v0.0.4 integration tests (dot on ARM64/ELF, type inference
  end-to-end, typed pseudo-C, CLI `--format dot`). Test count:
  133 unit + 29 integration (was 106 + 24).

### Changed

- `ir::Function` gained a `vreg_types` map; the Decompiler calls
  `TypeInferer::infer` after every `lift_function`.
- `render_pseudo_c` now emits a variable-declaration block before the
  basic blocks.
- The CLI help text lists `dot` as a valid `--format`.
- Output writers and `nyx --version` now report `0.0.4`.

### Fixed

- **Critical bug**: `CFGBuilder::build` did not split basic blocks after
  terminator instructions. A sequence like `cmp; BranchCond; mov; Branch;
  ret` was being packed into a single basic block, which produced
  nonsensical CFGs (no edges, wrong block boundaries) and made the DOT
  output empty. The builder now marks the instruction after every
  terminator (Branch / BranchCond / Return) as a leader, so each
  terminator correctly ends its block. This was the single biggest
  correctness issue in v0.0.3 and affects every architecture's CFG.
- **Bug**: `FunctionDetector::is_x86_prologue` flagged `mov rbp, rsp`
  as a prologue start; that instruction is the *second* instruction of
  the canonical x86 prologue. Already fixed in v0.0.3; the v0.0.4
  find_function_end logic additionally stops at the next prologue, so
  even if a false positive slipped through, the function body would be
  bounded correctly.

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
