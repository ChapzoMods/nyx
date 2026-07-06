#!/usr/bin/env python3
"""Generate a minimal but valid PE32+ (x86-64) executable for Nyx tests.

The PE contains a single executable section with a `ret` (0xC3) instruction.
"""
import struct, sys

def align_up(v, a): return (v + a - 1) & ~(a - 1)

# --- DOS header (64 bytes) + 64 bytes of placeholder so e_lfanew points past us ---
dos = bytearray(0x40)
dos[0:2] = b'MZ'
e_lfanew = 0x40
struct.pack_into('<I', dos, 0x3C, e_lfanew)

# --- PE signature ---
pe_sig = b'PE\x00\x00'

# --- COFF header (20 bytes) ---
machine = 0x8664   # AMD64
n_sections = 1
time = 0
ptr_sym = 0
n_sym = 0
opt_size = 0xF0    # PE32+ optional header size (240 bytes)
characteristics = 0x0022  # EXECUTABLE_IMAGE | LARGE_ADDRESS_AWARE
coff = struct.pack('<HHIIIHH', machine, n_sections, time, ptr_sym, n_sym, opt_size, characteristics)

# --- Optional header PE32+ (240 bytes) ---
opt_magic = 0x20B
major_link = 14
minor_link = 0
size_of_code = 0x200
size_of_init_data = 0
size_of_uninit_data = 0
entry_rva = 0x1000   # base of code
base_of_code = 0x1000
image_base = 0x140000000
section_align = 0x1000
file_align = 0x200
major_os = 6
minor_os = 0
major_img = 0
minor_img = 0
major_sub = 6
minor_sub = 0
win_ver = 0
size_of_image = 0x2000
size_of_headers = align_up(0x40 + 4 + 20 + opt_size, file_align)
size_of_headers = 0x200
checksum = 0
subsystem = 3  # CONSOLE
dll_chars = 0x8160  # HIGH_ENTROPY_VA | DYNAMIC_BASE | NX_COMPAT | TERMINAL_SERVER_AWARE
stack_reserve = 0x100000
stack_commit = 0x1000
heap_reserve = 0x100000
heap_commit = 0x1000
loader_flags = 0
n_data_dirs = 16
data_dirs = b'\x00' * (n_data_dirs * 8)

# Layout:
# 0..0x40          DOS header
# 0x40..0x44       PE signature
# 0x44..0x58       COFF header (20 bytes)
# 0x58..0x150      Optional header PE32+ (240 bytes)
# 0x150..0x200     Section header (40 bytes) + padding
# 0x200..0x400     Section .text raw data

opt = struct.pack('<HBBIIIII',
    opt_magic, major_link, minor_link, size_of_code, size_of_init_data,
    size_of_uninit_data, entry_rva, base_of_code)
opt += struct.pack('<Q', image_base)
opt += struct.pack('<IIII', section_align, file_align, (major_os << 16) | minor_os, (major_img << 16) | minor_img)
opt += struct.pack('<IIIII', (major_sub << 16) | minor_sub, win_ver, size_of_image, size_of_headers, checksum)
opt += struct.pack('<HH', subsystem, dll_chars)
opt += struct.pack('<QQQQ', stack_reserve, stack_commit, heap_reserve, heap_commit)
opt += struct.pack('<II', loader_flags, n_data_dirs)
opt += data_dirs

assert len(opt) == opt_size, f"opt header size {len(opt)} != {opt_size}"

# --- Section header (40 bytes): .text ---
sect_name = b'.text\x00\x00\x00'
virtual_size = 0x1
virtual_address = 0x1000
raw_size = 0x200
raw_ptr = 0x200
reloc_ptr = 0
linenum_ptr = 0
n_reloc = 0
n_lines = 0
sect_chars = 0x60000020  # MEM_EXECUTE | MEM_READ | CNT_CODE
sect_hdr = sect_name + struct.pack('<IIIIIIHHI', virtual_size, virtual_address, raw_size, raw_ptr, reloc_ptr, linenum_ptr, n_reloc, n_lines, sect_chars)
assert len(sect_hdr) == 40

# --- Assemble headers ---
headers = dos + pe_sig + coff + opt + sect_hdr
headers += b'\x00' * (size_of_headers - len(headers))

# --- Section body: a single `ret` + padding ---
body = b'\xc3' + b'\x00' * (raw_size - 1)

pe = headers + body
sys.stdout.buffer.write(pe)
