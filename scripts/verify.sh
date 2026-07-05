#!/usr/bin/env bash
# =============================================================================
# Nyx local verification script.
#
# Runs the same checks the CI workflow runs, but locally. Intended use:
#
#     ./scripts/verify.sh            # default: configure + build + tests
#     ./scripts/verify.sh --quick    # skip sanitizers (faster, less coverage)
#     ./scripts/verify.sh --clean    # remove build/ first
#
# Exits non-zero if any step fails. Used as a pre-commit hook by maintainers.
# =============================================================================
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

QUICK=0
CLEAN=0
for arg in "$@"; do
    case "$arg" in
        --quick) QUICK=1 ;;
        --clean) CLEAN=1 ;;
        *) echo "unknown flag: $arg" >&2; exit 1 ;;
    esac
done

if [[ $CLEAN -eq 1 ]]; then
    echo "[verify] removing build/"
    rm -rf build
fi

# Locate cmake. Prefer the venv install, fall back to PATH.
CMAKE="${CMAKE:-cmake}"
if ! command -v "$CMAKE" >/dev/null 2>&1; then
    CMAKE="/home/z/.venv/bin/cmake"
fi
if ! command -v "$CMAKE" >/dev/null 2>&1; then
    echo "[verify] ERROR: cmake not found. Install with 'pip install cmake' or apt." >&2
    exit 1
fi

BUILD_TYPE=Debug
SANITIZERS=ON
if [[ $QUICK -eq 1 ]]; then
    BUILD_TYPE=Release
    SANITIZERS=OFF
fi

echo "[verify] cmake = $CMAKE"
echo "[verify] configuring (build_type=$BUILD_TYPE, sanitizers=$SANITIZERS)"
"$CMAKE" -S . -B build \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DNYX_ENABLE_SANITIZERS="$SANITIZERS" \
    -DNYX_WERROR=OFF

echo "[verify] building"
"$CMAKE" --build build -j"$(nproc 2>/dev/null || echo 2)"

echo "[verify] rebuilding fixtures"
( cd tests/fixtures && ./build.sh )

echo "[verify] running unit tests"
./build/bin/nyx_unit_tests

echo "[verify] running integration tests"
./build/bin/nyx_integration_tests

echo "[verify] smoke test: --version"
./build/bin/nyx --version >/dev/null

echo "[verify] smoke test: JSON output on sample.elf"
./build/bin/nyx --format json --log-level error tests/fixtures/sample.elf >/dev/null

echo "[verify] smoke test: pseudo-C output on sample.elf"
./build/bin/nyx --format pseudo-c --log-level error tests/fixtures/sample.elf >/dev/null

echo "[verify] smoke test: text output on sample.macho"
./build/bin/nyx --format text --log-level error tests/fixtures/sample.macho >/dev/null

echo "[verify] ALL CHECKS PASSED"
