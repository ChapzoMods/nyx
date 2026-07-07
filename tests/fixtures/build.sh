#!/usr/bin/env bash
# Builds the test fixtures used by Nyx's integration tests.
# - sample.elf        : x86-64 ELF
# - sample.elf32      : i386 ELF
# - sample.pe         : x86-64 PE (via MinGW if available)
# - sample.macho      : Mach-O (only built when a Mach-O cross-compiler is present)
#
# Failures are non-fatal: missing cross-compilers simply skip that fixture.
set -u
cd "$(dirname "$0")"

CC="${CC:-gcc}"
OUT_DIR="${OUT_DIR:-.}"

# Native ELF (x86-64)
if "$CC" -fno-pie -no-pie -O0 -o "$OUT_DIR/sample.elf" sample.c 2>/dev/null; then
    echo "[fixtures] built sample.elf (x86-64)"
else
    echo "[fixtures] WARN: could not build sample.elf"
fi

# v0.0.6: debug ELF with DWARF v4 line tables.
if "$CC" -gdwarf-4 -O0 -o "$OUT_DIR/sample.debug.elf" sample.c 2>/dev/null; then
    echo "[fixtures] built sample.debug.elf (x86-64, DWARF v4)"
else
    echo "[fixtures] WARN: could not build sample.debug.elf"
fi

# Native ELF (i386) - requires gcc -m32
if "$CC" -m32 -fno-pie -no-pie -O0 -o "$OUT_DIR/sample.elf32" sample.c 2>/dev/null; then
    echo "[fixtures] built sample.elf32 (i386)"
else
    echo "[fixtures] SKIP: i386 cross-compile unavailable"
fi

# ARM ELF - requires arm-linux-gnueabihf-gcc
if command -v arm-linux-gnueabihf-gcc >/dev/null 2>&1; then
    if arm-linux-gnueabihf-gcc -fno-pie -no-pie -O0 -static -o "$OUT_DIR/sample.elf.arm" sample.c 2>/dev/null; then
        echo "[fixtures] built sample.elf.arm"
    fi
else
    echo "[fixtures] SKIP: arm-linux-gnueabihf-gcc unavailable"
fi

# AArch64 ELF
if command -v aarch64-linux-gnu-gcc >/dev/null 2>&1; then
    if aarch64-linux-gnu-gcc -fno-pie -no-pie -O0 -static -o "$OUT_DIR/sample.elf.aarch64" sample.c 2>/dev/null; then
        echo "[fixtures] built sample.elf.aarch64"
    fi
else
    echo "[fixtures] SKIP: aarch64-linux-gnu-gcc unavailable"
fi

# PE via MinGW
if command -v x86_64-w64-mingw32-gcc >/dev/null 2>&1; then
    if x86_64-w64-mingw32-gcc -O0 -o "$OUT_DIR/sample.pe" sample.c 2>/dev/null; then
        echo "[fixtures] built sample.pe (x86-64 PE)"
    fi
else
    echo "[fixtures] SKIP: x86_64-w64-mingw32-gcc unavailable - generating minimal PE via gen_pe.py"
    python3 gen_pe.py > "$OUT_DIR/sample.pe" && echo "[fixtures] built sample.pe (hand-crafted, x86-64)"
fi

# Mach-O: not produced on Debian unless osxcross is installed.
if command -v x86_64-apple-darwin15-clang >/dev/null 2>&1; then
    if x86_64-apple-darwin15-clang -O0 -o "$OUT_DIR/sample.macho" sample.c 2>/dev/null; then
        echo "[fixtures] built sample.macho"
    fi
else
    echo "[fixtures] SKIP: osxcross unavailable (sample.macho not built)"
fi

# Always build a hand-crafted minimal Mach-O 64-bit fixture: a stripped
# Mach-O with a single __TEXT,__text section containing a single `ret`.
# This guarantees we always have something for the Mach-O parser tests.
python3 -c "
import struct, sys
# Mach-O 64-bit, x86_64, FILETYPE=2 (MH_EXECUTE), minimal load commands:
#   LC_SEGMENT_64 with one section __TEXT,__text
#   LC_MAIN pointing to the entry (file offset 0 of __text)
mh_magic_64 = 0xFEEDFACF
cpu_x86_64 = 0x01000007
cpu_subtype_all = 3
filetype_execute = 2
ncmds = 2
flags = 0x85  # MH_NOMULTIDEFS|MH_PIE|MH_NO_HEAP_EXECUTION (approximate)
header = struct.pack('<IIIIIIII', mh_magic_64, cpu_x86_64, cpu_subtype_all, filetype_execute, ncmds, 0, flags, 0)
# LC_SEGMENT_64 = 0x19, cmdsize = 72 + 80 = 152? actually segment_64 = 72, section_64 = 80
# With 1 section: cmdsize = 72 + 80 = 152
lc_segment_64 = 0x19
segname = b'__TEXT\x00' + b'\x00'*9
vmaddr = 0x100000000
vmsize = 0x1000
fileoff = 0
filesize = 0x1000  # whole file padded
maxprot = 7  # rwx
initprot = 5  # r-x
nsects = 1
flags_seg = 0
segment_cmd = struct.pack('<II', lc_segment_64, 152) + segname + struct.pack('<QQQQIIII', vmaddr, vmsize, fileoff, filesize, maxprot, initprot, nsects, flags_seg)
sectname = b'__text\x00' + b'\x00'*9
sect_segname = b'__TEXT\x00' + b'\x00'*9
sect_addr = vmaddr
sect_size = 1
sect_offset = 0x1000 - 1  # one byte at end of __TEXT
sect_align = 0
sect_reloff = 0
sect_nreloc = 0
sect_flags = 0x80000400  # S_ATTR_PURE_INSTRUCTIONS | S_ATTR_SOME_INSTRUCTIONS
section = sectname + sect_segname + struct.pack('<QQIIIIIIII', sect_addr, sect_size, sect_offset, sect_align, sect_reloff, sect_nreloc, sect_flags, 0, 0, 0)
# LC_MAIN = 0x80000028, cmdsize=24, entryoff=sect_offset, stacksize=0
lc_main = 0x80000028
main_cmd = struct.pack('<IIQQ', lc_main, 24, sect_offset, 0)
macho = header + segment_cmd + section + main_cmd
# Patch sizeofcmds (offset 20 in the Mach-O header).
cmds = segment_cmd + section + main_cmd
header = header[:20] + struct.pack('<I', len(cmds)) + header[24:]
macho = header + cmds
# pad to filesize
macho += b'\x00' * (sect_offset - len(macho))
macho += b'\xc3'  # ret
macho += b'\x00' * (filesize - len(macho))
sys.stdout.buffer.write(macho)
" > "$OUT_DIR/sample.macho" 2>/dev/null && echo "[fixtures] built sample.macho (hand-crafted, x86-64)"

# v0.0.2: AArch64 Mach-O fixture (4 instructions: mov/add/ret).
python3 gen_arm64_macho.py > "$OUT_DIR/sample.arm64.macho" 2>/dev/null \
    && echo "[fixtures] built sample.arm64.macho (hand-crafted, AArch64)"

# v0.0.3: MIPS32 and PPC32 ELF fixtures (hand-crafted, big-endian).
python3 gen_mips_elf.py > "$OUT_DIR/sample.mips.elf" 2>/dev/null \
    && echo "[fixtures] built sample.mips.elf (hand-crafted, MIPS32 BE)"
python3 gen_ppc_elf.py > "$OUT_DIR/sample.ppc.elf" 2>/dev/null \
    && echo "[fixtures] built sample.ppc.elf (hand-crafted, PPC32 BE)"

# v0.2.0: WebAssembly fixture.
python3 gen_wasm.py > "$OUT_DIR/sample.wasm" 2>/dev/null \
    && echo "[fixtures] built sample.wasm (minimal WASM)"

ls -la "$OUT_DIR"
