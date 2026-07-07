#!/usr/bin/env bash
# Regression smoke test: runs every output format against every fixture and
# reports crashes / non-compilable C output. Exits with the failure count so
# CI can fail the build when something regresses.
set -eu

NYX="${1:-./build/bin/nyx}"
FIXTURES="tests/fixtures"
PASS=0
FAIL=0

for fmt in json text pseudo-c dot annotated c; do
    for fixture in sample.elf sample.debug.elf sample.pe sample.macho sample.arm64.macho sample.wasm; do
        if [ ! -f "$FIXTURES/$fixture" ]; then continue; fi
        OUT=$("$NYX" --format "$fmt" --log-level error "$FIXTURES/$fixture" 2>&1) || true
        if echo "$OUT" | grep -q "runtime error\|ASAN\|ABORTING\|SEGV"; then
            echo "FAIL: $fmt $fixture (crash)"
            FAIL=$((FAIL+1))
            continue
        fi
        if [ "$fmt" = "c" ]; then
            echo "$OUT" > /tmp/regression_test.c
            if ! gcc -c -o /dev/null /tmp/regression_test.c 2>/dev/null; then
                echo "FAIL: $fmt $fixture (gcc -c failed)"
                FAIL=$((FAIL+1))
                continue
            fi
        fi
        echo "PASS: $fmt $fixture"
        PASS=$((PASS+1))
    done
done
echo "=== $PASS passed, $FAIL failed ==="
exit $FAIL
