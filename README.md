# Nyx

> A headless, open-source decompilation engine written in C++20.

Nyx parses ELF, PE and Mach-O binaries, disassembles their executable
sections with Capstone, lifts the resulting machine instructions to a
small SSA-flavoured intermediate representation, reconstructs a
control-flow graph, and emits the result as JSON, plain text or
pseudo-C source. The project is intentionally **headless**: there is
no GUI, no IPC server, no daemon. Nyx is built to be embedded in
security-analysis pipelines, diffing tooling and research workflows
where a stable, scriptable decompiler is more valuable than an
interactive one.

- **Author:** Chapzoo
- **License:** GNU GPL v3.0 or later
- **Status:** v0.0.6 - early alpha, see the [Roadmap](#roadmap) below
- **Repository:** <https://github.com/Chapzoo/nyx>

---

## Table of contents

1. [Why another decompiler?](#why-another-decompiler)
2. [Features](#features)
3. [Supported architectures and formats](#supported-architectures-and-formats)
4. [Installation](#installation)
5. [Usage](#usage)
6. [Output formats](#output-formats)
7. [Architecture of the engine](#architecture-of-the-engine)
8. [Testing](#testing)
9. [Roadmap](#roadmap)
10. [Credits](#credits)
11. [License](#license)

---

## Why another decompiler?

The existing free-software decompiler landscape is dominated by a few
large projects (Ghidra, angr, radare2 / rizin, RetDec). Each has its
own strength, but they all share at least one of the following
drawbacks when you only want to script a "give me a structured view of
this binary" pipeline:

- A heavy UI / JVM dependency that is awkward to ship in containers.
- A Python-first scripting layer that is impractical when the rest of
  the pipeline is C++.
- A monolithic architecture that requires understanding thousands of
  files before you can extend a single opcode.
- A license that is permissive but not copyleft.

Nyx takes the opposite side on every axis: pure C++20, no UI, copyleft
(GPLv3+), and a deliberately small, modular codebase where the entire
parser / lifter / decompiler surface is reachable in well under a
thousand lines per module. The trade-off is feature completeness - we
do not aim to match Ghidra's decompiler quality any time soon. We aim
to be the right tool for the cases where you do not need Ghidra.

## Features

### v0.0.6 (current)

- **Three binary formats** parsed natively in C++20 (no libelf, no
  libpe, no libmacho dependency):
  - **ELF** - 32 and 64 bit, little and big endian. Symbol table
    (.symtab and .dynsym), section flags, NX / RELRO detection,
    BSS-aware (`SHT_NOBITS` sections marked `is_nobits`).
  - **PE / PE32+** - COFF header, optional header, section table,
    export directory, import descriptor table (with ordinal imports).
    PIE / NX detection via DLL characteristics.
  - **Mach-O** - 32 and 64 bit, little and big endian, fat/universal
    archives (all slices parsed, exposed via `BinaryInfo::slices`).
    LC_SEGMENT / LC_SEGMENT_64, LC_SYMTAB, LC_MAIN.
- **Six architectures** through Capstone v5.0.3, all with real lifters:
  - Intel x86 (32-bit) - mov/add/sub/cmp/test/jmp/jcc/call/ret/push/
    pop/lea/xor/and/or/shl/shr/imul/div/etc.
  - Intel x86-64 - same as x86 plus 64-bit register handling.
  - ARM (A32) - mov/add/sub/ldr/str/b/bl/bx/cmp/and/orr/eor/lsl/lsr/
    asr/push/pop/etc.
  - AArch64 - mov/movz/movk/add/sub/mul/ldr/ldr/str/stp/ldp/cmp/tst/
    b/bl/blr/b.cond/cbz/cbnz/ret/etc.
  - PowerPC (32-bit and 64-bit) - add/addi/sub/lwz/stw/b/bl/blr/cmp/
    mflr/stwu/rlwinm/etc.
  - MIPS (32-bit and 64-bit) - addiu/addu/subu/lw/sw/beq/bne/j/jal/jr/
    lui/slt/and/or/xor/nor/sll/srl/sra/etc.
- **Heuristic function detection** (`FunctionDetector`): scans stripped
  binaries for well-known prologue patterns. v0.0.4 adds
  `find_function_end` to bound function bodies at the next return.
- **Type-shape detection** (`TypeInferer`): infers primitive types
  (Int8/Int16/Int32/Int64/Ptr/Func) for virtual registers. Pseudo-C
  output emits typed variable declarations.
- **CFG analysis** (v0.0.5): dominator tree (Cooper-Harvey-Kennedy),
  natural loop detection, reachability pruning. The pseudo-C renderer
  emits `while (1) { ... }` with `continue;` for back edges and
  `break;` for loop exits.
- **Jump table detection** (v0.0.5 `JumpTableDetector`): identifies
  compiler-generated switch tables on x86 (`lea + jmp reg`) and ARM64
  (`ldr + br`), resolves table entries from the binary.
- **DWARF v4 debug info** (v0.0.6): pure C++20 parser for `.debug_line`,
  `.debug_info`, `.debug_abbrev`, `.debug_str` — no libdwarf. Extracts
  function names + address ranges, line tables (PC → source file:line:col),
  and type information (`DW_AT_type`). Auto-loaded when present; force with
  `--debug-info`.
- **Annotated disassembly** (v0.0.6 `--format annotated`): interleaves
  source lines from DWARF with disassembled instructions, similar to
  `objdump -S`.
- **DWARF-enhanced type inference** (v0.0.6): `TypeInferer` now resolves
  `DW_AT_type` DIE references (base types, pointers, typedefs) and uses
  them instead of size-based heuristics when available.
- **Indirect branch marking** (v0.0.5): `jmp reg` / `br xN` / `bx rN` /
  `jr $tN` are marked with `indirect = true` in the IR; pseudo-C emits
  `// indirect branch via <vreg>` comments.
- **Lifter** to a small SSA-style IR with 24 opcodes covering data
  movement, arithmetic, comparison, control flow and an `Opaque`
  fallback for any instruction the lifter does not yet model.
- **CFG builder** that splits instructions into basic blocks on
  terminators and on branch targets, with successor resolution.
- **Four output formats**: JSON (stable schema, machine-readable),
  plain text (human-readable listing), pseudo-C source with typed
  variable declarations, if/else hints, and `while`/`continue` for
  loops; DOT/Graphviz CFG rendering with dominator/loop colouring
  (`--format dot`).
- **CLI** with `--format`, `--output`, `--log-level`, `--arch`,
  `--format-hint`, `--version`, `--help`. Auto-detection logs the
  inferred format/arch at INFO level.
- **Test suite**: 163 unit tests + 40 integration tests, all green
  under ASan + UBSan.

## Supported architectures and formats

| Format \\ Arch | x86 | x86-64 | ARM | AArch64 | PPC | PPC64 | MIPS | MIPS64 |
|----------------|:---:|:------:|:---:|:-------:|:---:|:-----:|:----:|:------:|
| ELF            |  Y  |   Y    |  Y  |    Y    |  Y  |   Y   |  Y   |   Y    |
| PE / PE32+     |  Y  |   Y    |  Y  |    Y    |  Y  |       |  Y   |        |
| Mach-O         |  Y  |   Y    |  Y  |    Y    |  Y  |   Y   |      |        |

Endianness is honoured per-binary (ELF and Mach-O can be either; PE is
always little-endian).

## Installation

### System packages

On Debian 13 (trixie) and equivalent:

```bash
sudo apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    git \
    python3 \
    pkg-config
```

Nyx vendors Capstone v5.0.3 and doctest v2.4.12 via CMake
`FetchContent`, so no `libcapstone-dev` or `libelf-dev` packages are
required. This keeps the install surface minimal and the build
reproducible across distros.

### Build

```bash
git clone https://github.com/Chapzoo/nyx.git
cd nyx

# Debug build with sanitizers - the recommended default for development.
cmake -S . -B build \
    -DCMAKE_BUILD_TYPE=Debug \
    -DNYX_ENABLE_SANITIZERS=ON

cmake --build build -j"$(nproc)"
```

The first build downloads Capstone (~3 MB tarball) and doctest (~1 MB
tarball) and compiles Capstone as a static library. On a 2-core / 4 GB
host this takes about 90 seconds. Subsequent builds are incremental.

### CMake options

| Option                      | Default | Description                                   |
|-----------------------------|---------|-----------------------------------------------|
| `NYX_BUILD_TESTS`           | `ON`    | Build `nyx_unit_tests` and `nyx_integration_tests`. |
| `NYX_ENABLE_SANITIZERS`     | `OFF`   | Enable AddressSanitizer + UndefinedBehaviorSanitizer. |
| `NYX_ENABLE_COVERAGE`       | `OFF`   | Enable `--coverage` instrumentation.          |
| `NYX_WERROR`                | `OFF`   | Treat warnings as errors.                     |

### Install

```bash
sudo cmake --install build
```

Installs `nyx` to `${CMAKE_INSTALL_BINDIR}` (typically `/usr/local/bin`)
and the public headers to `${CMAKE_INSTALL_INCLUDEDIR}/nyx/`. The CMake
package config is exported as `nyxTargets` so downstream CMake projects
can link with `target_link_libraries(your_app PRIVATE nyx::nyx)`.

## Usage

### Quick start

```bash
# Get a JSON dump of a binary:
nyx --format json ./my-binary > out.json

# Pretty pseudo-C:
nyx --format pseudo-c ./my-binary | less

# Text listing (header + sections + disassembly):
nyx --format text ./my-binary > listing.txt

# Override auto-detection:
nyx --arch x86-64 --format-hint elf ./mystery-file
```

### Command-line reference

```
Usage: nyx [OPTIONS] <binary>

Options:
  -h, --help              Show this help and exit.
  -V, --version           Print the Nyx banner and exit.
  -f, --format <fmt>      Output format: json | text | pseudo-c (default: json).
  -o, --output <path>     Write output to <path> instead of stdout.
  -L, --log-level <lvl>   trace|debug|info|warn|error|critical (default: info).
  -q, --quiet             Alias for --log-level critical.
      --arch <name>       Override architecture detection (x86, x86-64, arm, ...).
      --format-hint <fmt> Override format detection (elf|pe|mach-o).
```

Exit codes:

| Code | Meaning                          |
|-----:|----------------------------------|
|    0 | success                          |
|    1 | invalid arguments                |
|    2 | I/O error                        |
|    3 | parse / decompile error          |

### Library use

Nyx is also usable as a static library. The public headers are
self-contained (no Capstone leak) so consumers don't need to add
Capstone to their include path.

```cpp
#include <nyx/parsers/binary_parser.hpp>
#include <nyx/decompiler/decompiler.hpp>
#include <nyx/output/json_writer.hpp>

int main() {
    nyx::BinaryInfo bin = nyx::BinaryParser::load_and_parse("/path/to/bin");
    nyx::Decompiler dec;
    auto funcs = dec.decompile(bin);
    nyx::output::write_json(std::cout, bin, funcs);
}
```

Link against `nyx::nyx` (CMake alias) or `libnyx.a` directly.

## Output formats

### JSON

Stable schema (additive-only across v0.0.x releases). Top-level keys:

```json
{
  "schema": "nyx.v0.0.1",
  "binary": { "path": "...", "format": "...", "arch": "...",
               "is_64bit": true, "is_pie": false, "has_nx": true,
               "entry_point": "0x...", "image_base": "0x...",
               "sections": [ { "name": ".text", ... } ],
               "symbols":  [ { "name": "main", ... } ] },
  "functions": [ { "name": "main", "entry": "0x401000",
                   "block_count": 3, "insn_count": 12,
                   "body": [ "void main(void) {", "    return;", "}" ] } ]
}
```

The JSON schema is the recommended integration surface for security
pipelines (SAST/DAST/research tooling). It is intentionally more
verbose than the text format so consumers don't have to re-parse
mnemonics.

### Pseudo-C

A best-effort translation of the IR into C-like syntax. v0.0.6 emits
one statement per IR instruction; type recovery, SSA deconstruction
and structured control flow are explicitly future work.

```c
// Function: add @ 0x401136
// 1 blocks, 4 instructions
void add(void) {
  L_401136:
    v1 = v1 - 0x8;
    v2 = *(void*)(v3 + 0x8);
    v4 = (v2 == v2);
    return;
}
```

### Plain text

Listing-style dump: header summary, section table, symbols and a
per-instruction listing of every executable section. Useful for
diffing two builds of the same binary.

```
================================================================================
 Nyx v0.0.6 - text dump of ./sample.elf
================================================================================
 Format     : elf
 Arch       : Intel x86-64 (x86-64)
 Endian     : little
 64-bit     : yes
 ...
--------------------------------------------------------------------------------
 Sections (27):
  name              vaddr            file_off   file_size  flags
  .text             0x401000         4096       4096       xrc
 ...
--------------------------------------------------------------------------------
 Disassembly [0] (123 instructions):
  0x0000000000401000  f30f1efa         endbr64
  0x0000000000401004  55               push   rbp
  ...
```

## Architecture of the engine

```
                      +-----------------+
                      |     main.cpp    |  CLI argument parsing, exit codes
                      +--------+--------+
                               |
                               v
            +------------------+------------------+
            |          BinaryParser::load_and_parse         |
            |  detect() routes by magic to the right parser |
            +-------+----------+-----------+----------+-----+
                    |          |           |          |
                +---+---+ +----+----+ +----+----+    |
                |  ELF  | |   PE    | | Mach-O  |    |
                +---+---+ +----+----+ +----+----+    |
                    |          |           |          |
                    +----------+-----------+----------+
                               |
                               v
                      +-----------------+
                      |   BinaryInfo    |  format-agnostic metadata
                      +--------+--------+
                               |
            +------------------+------------------+
            |                  |                  |
            v                  v                  v
   +-----------------+ +----------------+ +----------------+
   |  Disassembler   | | InstructionLifter | |  Decompiler    |
   |  (Capstone)     | |  -> IR           | |  orchestrates  |
   +--------+--------+ +--------+---------+ |  the pipeline  |
            |                   |           +--------+-------+
            |                   v                    |
            |          +--------+---------+          |
            |          |   CFGBuilder     |          |
            |          |   (basic blocks) |          |
            |          +--------+---------+          |
            |                   |                    |
            |                   v                    |
            |          +--------+---------+          |
            |          |   pseudo_c.cpp   |<---------+
            |          | (IR -> C source) |
            |          +------------------+
            |
            v
   +-----------------+  +----------------+  +--------------------+
   |  json_writer    |  |  text_writer   |  |  pseudo_c_writer   |
   +-----------------+  +----------------+  +--------------------+
```

### Modules

| Directory                | Responsibility                                               |
|--------------------------|--------------------------------------------------------------|
| `include/nyx/core/`      | Common types: `ByteView`, `ByteBuffer`, `Arch`, `Error`, `Logger`, `BinaryInfo`. |
| `include/nyx/parsers/`   | Binary format parsers and the Capstone-backed `Disassembler`. |
| `include/nyx/lifter/`    | The IR types (`ir.hpp`), the CFG builder and the per-instruction lifter. |
| `include/nyx/decompiler/`| The top-level pipeline that ties parser + lifter + renderer together, plus the pseudo-C renderer. |
| `include/nyx/output/`    | JSON / text / pseudo-C writers.                              |
| `src/`                   | Mirror of `include/`, one `.cpp` per public header.         |
| `tests/unit/`            | doctest unit tests.                                          |
| `tests/integration/`     | End-to-end tests against the fixtures under `tests/fixtures/`. |
| `tests/fixtures/`        | `sample.c`, `build.sh`, `gen_pe.py` - produce the test binaries. |

### IR design

The IR is intentionally small. There are 24 opcodes:

- Data movement: `Mov`, `Load`, `Store`
- Arithmetic: `Add`, `Sub`, `Mul`, `Div`, `Mod`, `And`, `Or`, `Xor`,
  `Shl`, `Shr`, `Sar`, `Neg`, `Not`
- Comparison: `Cmp`
- Control flow: `Branch`, `BranchCond`, `Call`, `Return`
- Misc: `Push`, `Pop`, `Nop`, `Opaque`

`Opaque` is the **safety net**: any machine instruction the lifter
does not model produces a single `Opaque` node that preserves the
original mnemonic as a string. This means the decompiler never lies
about coverage - if you see `// frobnicate rax, rbx` in the pseudo-C
output, you know exactly which instruction wasn't lifted.

The IR uses virtual registers (`VReg`) allocated per-function by the
lifter. v0.0.6 does not perform SSA deconstruction, dominance or
type recovery - every function is emitted as a single `void f(void)`
block with `vN` placeholders. This keeps the surface honest about
what is actually implemented vs. what is on the roadmap.

### Why no libelf / libpe / libmacho?

Three reasons:

1. **Resource budget.** The reference build host has 4-8 GB of RAM.
   Pulling in libelf + libpe + LIEF or similar would triple the
   dependency surface for very little gain.
2. **Format coverage.** We need PE, Mach-O and ELF; no single
   library covers all three well. libelf is ELF-only, libpe isn't
   packaged in Debian, libmacho doesn't exist as a standalone C
   library.
3. **Code clarity.** Each parser is under 500 lines of C++20 and
   fully readable. Contributors don't need to learn a third-party
   API to debug format issues.

The trade-off is parser maturity - the v0.0.6 parsers handle the
common cases but don't yet cover every edge of the specifications
(relocations, DWARF unwinding, CODE_SIGNATURE load commands, ...).
These are roadmap items.

## Testing

### Test layout

- `tests/unit/` - one file per public module, 50 cases total.
  Covers `ByteView` helpers, arch mapping, IR builder, CFG builder,
  ELF / PE / Mach-O parsers, the disassembler wrapper, the lifter,
  the decompiler, and all three output writers.
- `tests/integration/` - 11 cases that exercise the full
  parse -> disasm -> lift -> decompile -> output pipeline against
  real fixtures, plus CLI smoke tests via `popen()`.

### Running the tests

```bash
# 1. Build the fixtures (sample.elf, sample.pe, sample.macho).
( cd tests/fixtures && ./build.sh )

# 2. Run the test binaries.
./build/bin/nyx_unit_tests
./build/bin/nyx_integration_tests

# 3. Or run everything in one shot via verify.sh:
./scripts/verify.sh
```

### CI

GitHub Actions workflow at `.github/workflows/ci.yml` runs two jobs on
every push and pull request:

1. **build-and-test** - Debian 13 container, debug build, ASan + UBSan,
   full test suite, `-Werror`.
2. **build-release** - same container, Release build, no sanitizers,
   full test suite.

Both jobs rebuild the fixtures from source, so any environment drift
in the generated binaries is caught immediately.

### Sanitizers

The build is routinely exercised under ASan + UBSan. The CLI does not
leak memory on any of the three fixtures (verified with
`ASAN_OPTIONS=detect_leaks=1`). If a test fails under sanitizers, it's
treated as a release blocker.

## Roadmap

Nyx is developed incrementally. Each version is small, ships, gets
used, and informs the next one.

### v0.0.x - "Erebus" series (alpha)

Goal: a usable scaffold with a stable JSON schema and clean internal
APIs. No promise of decompiler quality.

- **v0.0.1** - Initial public release. ELF / PE / Mach-O parsing,
  multi-arch disassembly, conservative lifter with `Opaque` fallback,
  JSON / text / pseudo-C output, full test suite.
- **v0.0.2** - Robustness pass. Stricter PE import parsing (ordinal
  imports), Mach-O LC_DYSYMTAB, ELF BSS handling, ARM64 lifter real,
  fat archives multi-slice, structured pseudo-C if/else.
- **v0.0.3** - Lifter expansion. Real x86/x86-64, ARM32, PowerPC and
  MIPS lifters; heuristic function detection via prologue scanning;
  shared operand parser across all architectures; pseudo-C if/else
  diamond hints; 46 new tests.
- **v0.0.4** - Type-shape detection (`TypeInferer` with
  Int8/16/32/64/Ptr/Func lattice), typed pseudo-C declarations,
  DOT/Graphviz CFG output (`--format dot`), extended FunctionDetector
  (more prologues + `find_function_end`), critical CFG builder fix
  (blocks now split after terminators), 28 new tests.
- **v0.0.5** *(current)* - CFG analysis: dominator tree
  (Cooper-Harvey-Kennedy), natural loop detection, jump table
  detection, indirect branch marking, unreachable-block pruning,
  structured `while`/`continue` in pseudo-C, DOT output with
  dominator/loop colouring, 23 new tests.
- **v0.0.6** - DWARF v4 line-table parsing for source attribution in
  the JSON output.

### v0.x.0 - "Nyx" series (beta)

Goal: decompiler quality good enough to be useful for security
research, not just an IR dumper.

- **v0.1.0** - SSA deconstruction. The lifter stops emitting
  pseudo-registers and starts producing real SSA values with phi
  nodes. Pseudo-C output gains structured `if` / `while` / `for`
  reconstruction.
- **v0.2.0** - Calling-convention-aware lifting for x86-64
  (System V and Microsoft), ARM (AAPCS) and AArch64 (AAPCS64).
- **v0.3.0** - RISC-V support (RV32 / RV64) end-to-end.
- **v0.4.0** - Symbolic execution hooks (experimental, opt-in).
- **v0.5.0** - First class Python bindings via pybind11.
- **v0.6.0** - Differential decompilation mode (`nyx diff a.bin b.bin`).
- **v0.7.0** - Plugin ABI for third-party format parsers.
- **v0.8.0** - Yara-rule generator from decompiled functions.
- **v0.9.0** - Performance: parallel decompilation across functions,
  incremental rebuilds, ~10x throughput improvement.

### v1.0.0 - release candidate

- API freeze.
- Long-running fuzz targets (oss-fuzz integration).
- First stable JSON schema (`nyx.v1`).
- Windows and macOS binary releases.

### Beyond 1.0

- An optional TUI front-end (still headless-friendly - TUI is a
  separate binary, not a GUI dependency).
- Semantic diffing at the IR level.
- Plugin marketplace.
- Binary-to-source diffing against an existing source tree.

## GitHub integration

The repository is prepared for direct integration with the user's
GitHub account:

- `git init && git remote add origin git@github.com:Chapzoo/nyx.git`
- The first push will run the CI workflow defined in
  `.github/workflows/ci.yml` automatically.
- Releases are cut by tagging `vX.Y.Z` and creating a GitHub Release
  referencing the tag; the changelog is hand-curated from the merged
  PR list since the previous release.

Future iterations will automate repository creation and code push
through GitHub's REST API (the user already requested this for a
later session - the v0.0.6 codebase is structured so that no project
metadata needs to change once that automation is wired up).

## Credits

Nyx was conceived and is maintained by **Chapzoo**
&lt;<https://github.com/Chapzoo>&gt;.

The project stands on the shoulders of:

- **Capstone** &lt;https://www.capstone-engine.org/&gt; - disassembly
  engine, by Nguyen Anh Quynh and contributors. BSD-3-Clause.
- **doctest** &lt;https://github.com/doctest/doctest&gt; - the
  lightest C++ test framework that actually works. MIT.
- The wider reversing community whose format documentation
  (elf.bitbread.io, macho.refer.wiki, learn.microsoft.com PE spec)
  made writing these parsers in pure C++20 a tractable afternoon's
  work rather than a multi-month endeavour.

Bug reports, feature requests and pull requests are welcome at
&lt;<https://github.com/Chapzoo/nyx/issues>&gt;.

## License

Nyx is free software: you can redistribute it and/or modify it under
the terms of the **GNU General Public License as published by the Free
Software Foundation, either version 3 of the License, or (at your
option) any later version**.

Nyx is distributed in the hope that it will be useful, but **WITHOUT
ANY WARRANTY**; without even the implied warranty of MERCHANTABILITY
or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public
License for more details.

You should have received a copy of the GNU General Public License
along with Nyx. If not, see &lt;<https://www.gnu.org/licenses/>&gt;.

The full text of the license is in the [`LICENSE`](LICENSE) file at
the root of the repository.

The vendored dependencies retain their original licenses
(BSD-3-Clause for Capstone, MIT for doctest). Their source trees are
fetched on first configuration and are not distributed as part of the
Nyx source tarball.
