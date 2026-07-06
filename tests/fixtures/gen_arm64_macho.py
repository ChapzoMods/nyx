#!/usr/bin/env python3
"""Generate a minimal AArch64 Mach-O executable for Nyx v0.0.2 tests.

The binary contains a small __text section with this sequence:
    mov x0, #0
    mov x1, #1
    add x2, x0, x1
    ret
"""
import struct, sys

# Mach-O 64-bit magic and CPU types.
MH_MAGIC_64 = 0xFEEDFACF
CPU_TYPE_ARM64 = 0x0100000C
CPU_SUBTYPE_ARM64_ALL = 0
MH_EXECUTE = 2
LC_SEGMENT_64 = 0x19
LC_MAIN = 0x80000028
MH_PIE = 0x2000

# AArch64 instruction encoders (just the ones we need).
def mov_imm(rd, imm16):
    # movz xrd, #imm16 (lsl #0)
    return 0xD2800000 | ((imm16 & 0xFFFF) << 5) | (rd & 0x1F)

def add_reg(rd, rn, rm):
    # add xrd, xrn, xrm
    return 0x8B000000 | ((rm & 0x1F) << 16) | ((rn & 0x1F) << 5) | (rd & 0x1F)

def ret():
    # ret x30 (default)
    return 0xD65F03C0

# Build the __text payload: 4 instructions * 4 bytes = 16 bytes.
text_bytes = struct.pack('<IIII',
    mov_imm(0, 0),   # mov x0, #0
    mov_imm(1, 1),   # mov x1, #1
    add_reg(2, 0, 1),# add x2, x0, x1
    ret(),           # ret
)
text_size = len(text_bytes)

# Layout:
# 0x000  Mach-O header (32 bytes)
# 0x020  LC_SEGMENT_64 + 1 section (72 + 80 = 152 bytes)
# 0x0B8  LC_MAIN (24 bytes)
# 0x0D0  padding to 0x1000
# 0x1000 __text (16 bytes)
# 0x1010 padding to 0x2000 (filesize)

vmaddr = 0x100000000
vmsize = 0x2000
fileoff_text = 0x1000
filesize = 0x2000

header = struct.pack('<IIIIIIII',
    MH_MAGIC_64, CPU_TYPE_ARM64, CPU_SUBTYPE_ARM64_ALL, MH_EXECUTE,
    2,                # ncmds
    0,                # sizeofcmds (patched below)
    MH_PIE,
    0)

segname = b'__TEXT\x00' + b'\x00' * 9
sectname = b'__text\x00' + b'\x00' * 9
segment_cmd = struct.pack('<II', LC_SEGMENT_64, 152) + segname + struct.pack('<QQQQIIII',
    vmaddr, vmsize, 0, filesize, 7, 5, 1, 0)
section = sectname + segname + struct.pack('<QQIIIIIIII',
    vmaddr,             # addr
    text_size,          # size
    fileoff_text,       # offset
    2,                  # align (4-byte)
    0, 0,               # reloff, nreloc
    0x80000400,         # flags: S_ATTR_PURE_INSTRUCTIONS | S_ATTR_SOME_INSTRUCTIONS
    0, 0, 0)            # reserved1-3

main_cmd = struct.pack('<IIQQ', LC_MAIN, 24, fileoff_text, 0)

# Patch sizeofcmds (offset 20 in the Mach-O header).
cmds = segment_cmd + section + main_cmd
header = header[:20] + struct.pack('<I', len(cmds)) + header[24:]

macho = header + cmds
macho += b'\x00' * (fileoff_text - len(macho))
macho += text_bytes
macho += b'\x00' * (filesize - len(macho))

sys.stdout.buffer.write(macho)
