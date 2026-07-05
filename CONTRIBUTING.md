# Contributing to Nyx

Thanks for your interest in improving Nyx. This document describes how to
set up a working development environment and the conventions every change
is expected to follow.

## Project ground rules

* **Headless first.** Nyx must remain usable as a pure CLI / library with
  no GUI dependency. Do not introduce UI toolkits.
* **Conservative growth.** Each release should ship the smallest useful
  delta. Prefer multiple small versions over one big bang.
* **No silent drops.** Every machine instruction the disassembler accepts
  must produce *some* IR. Unknown semantics become `OpCode::Opaque` - they
  are never silently ignored.
* **Resource-aware.** The reference build host is Debian 13 with 4-8 GB
  of RAM. Avoid heavyweight dependencies (LLVM, Boost, Qt). When in doubt,
  write a small, focused parser in pure C++20.

## Development setup

### Required tools

| Tool       | Minimum | Notes                                                |
|------------|---------|------------------------------------------------------|
| C++ compiler | g++ 12+ or clang++ 14+ | C++20 features used across the codebase. |
| CMake      | 3.20+   | FetchContent pulls in Capstone and doctest.          |
| Python 3   | 3.8+    | Used by the test-fixture generator.                  |
| git        | any     |                                                      |

### Optional cross-compilers (for richer fixtures)

* `gcc-multilib` / `g++-m32` - generates 32-bit ELF fixtures.
* `arm-linux-gnueabihf-gcc` - ARM 32-bit ELF fixtures.
* `aarch64-linux-gnu-gcc` - AArch64 ELF fixtures.
* `x86_64-w64-mingw32-gcc` - real Windows PE fixtures (instead of the
  hand-crafted minimal one shipped by `gen_pe.py`).

Any missing cross-compiler is silently skipped by `tests/fixtures/build.sh`
- the corresponding integration test will not be enabled, but the rest of
  the suite runs normally.

### Building Nyx

```bash
git clone https://github.com/Chapzoo/nyx.git
cd nyx
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DNYX_ENABLE_SANITIZERS=ON
cmake --build build -j"$(nproc)"
```

The `nyx` binary, `nyx_unit_tests` and `nyx_integration_tests` end up in
`build/bin/`. Capstone and doctest are downloaded and compiled on first
configuration; subsequent builds reuse them.

### Running the test suite

```bash
# Build the fixtures (sample.elf, sample.pe, sample.macho, ...).
( cd tests/fixtures && ./build.sh )

# Run unit + integration tests.
./build/bin/nyx_unit_tests
./build/bin/nyx_integration_tests

# Or run everything in one shot, including smoke tests:
./scripts/verify.sh
```

`verify.sh` mirrors what CI does. Run it before pushing.

## Coding conventions

* **C++20**, no compiler extensions. `-Wall -Wextra -Wpedantic -Wshadow
  -Wconversion` are enabled by default; CI adds `-Werror`.
* Headers use `#pragma once`. Every header begins with the SPDX license
  header.
* Public headers live under `include/nyx/`. Implementation files live
  under `src/<module>/`.
* The public API never leaks Capstone types. The `Disassembler` class
  stores its Capstone handle as `std::uintptr_t` precisely so that
  consumers don't need to include `<capstone/capstone.h>`.
* Errors throw `nyx::Error` with a `Category`. Use the `NYX_THROW(cat, msg)`
  macro when you need file:line attribution.
* Logging goes through `nyx::Logger::instance()` or the `NYX_INFO` /
  `NYX_WARN` / `NYX_ERROR` / `NYX_CRIT` macros. Never write directly to
  `std::cerr` from library code.

## Tests

* Unit tests live in `tests/unit/`. They must not touch the filesystem
  except through `NYX_FIXTURES_DIR`.
* Integration tests live in `tests/integration/`. They may invoke the CLI
  binary via `popen()` and assert on its output.
* Every new public function or class should ship with at least one unit
  test covering the happy path and one covering an error path.

## Submitting changes

1. Open an issue describing what you intend to change and why. Wait for
   a brief ack from a maintainer if the change is non-trivial.
2. Fork the repo, create a branch named `<topic>/<short-description>`.
3. Make your changes in small, reviewable commits. Use conventional
   commit prefixes: `feat:`, `fix:`, `test:`, `docs:`, `refactor:`,
   `chore:`.
4. Run `./scripts/verify.sh` locally. The CI will reject PRs that fail
   any check.
5. Open a PR against `develop`. Reference the issue in the description.
6. By submitting the PR you agree to release your contributions under the
   GPLv3+ license that covers the project.

## Versioning policy

Nyx follows a `MAJOR.MINOR.PATCH` scheme:

* `PATCH` (0.0.x) - bug fixes, test additions, internal refactors. No
  API changes.
* `MINOR` (0.x.0) - new features, new output formats, new arch support.
  May add new public API but must not break existing API.
* `MAJOR` (x.0.0) - breaking API changes. Reserved for the eventual
  1.0.0 release once the decompiler reaches production-grade quality.

Each release gets a git tag `vX.Y.Z` and a GitHub Release with the
changelog.

## Roadmap and discussion

High-level roadmap lives in the main `README.md`. For design discussions,
open a "discussion" issue tagged `design`. We prefer written design notes
over ad-hoc chat - they become permanent record for future contributors.
