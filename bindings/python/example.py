#!/usr/bin/env python3
"""Example: load a binary with Nyx and decompile it.

Usage:
    python3 example.py /path/to/binary [format]

Formats: json, text, pseudo-c, annotated, dot (default: pseudo-c)
"""
import sys

try:
    import nyx_python as nyx
except ImportError:
    print("Error: nyx_python module not found.")
    print("Build with: cmake -DNYX_BUILD_PYTHON=ON .. && make")
    sys.exit(1)

def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <binary> [format]")
        print(f"  Formats: json, text, pseudo-c, annotated, dot")
        sys.exit(1)

    path = sys.argv[1]
    fmt = sys.argv[2] if len(sys.argv) > 2 else "pseudo-c"

    # Load the binary.
    bin_info = nyx.load(path)
    print(f"Binary: {bin_info.path}")
    print(f"  Format: {bin_info.format}")
    print(f"  Arch:   {bin_info.arch}")
    print(f"  64-bit: {bin_info.is_64bit}")
    print(f"  PIE:    {bin_info.is_pie}")
    print(f"  NX:     {bin_info.has_nx}")
    print(f"  Sections: {len(bin_info.sections)}")
    print(f"  Symbols:  {len(bin_info.symbols)}")
    print()

    # Decompile.
    result = nyx.decompile_file(path, fmt)
    print(result)

if __name__ == "__main__":
    main()
