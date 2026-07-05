// =============================================================================
// Minimal fixture source for Nyx integration tests.
// Compiled to ELF/PE/Mach-O at build time by tests/fixtures/build.sh
// =============================================================================
#include <stdio.h>

static int add(int a, int b) { return a + b; }
static int sub(int a, int b) { return a - b; }

int use_add_sub(int x) {
    int y = add(x, 1);
    int z = sub(y, 2);
    return y * z;
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    int v = use_add_sub(42);
    printf("%d\n", v);
    return 0;
}
