#!/usr/bin/env python3
"""Generate a minimal MIPS32 big-endian ELF executable for Nyx v0.0.3 tests.

The binary contains a tiny .text section with this sequence:
    addiu   $sp, $sp, -16      # 0x27BDFFF0
    sw      $ra, 12($sp)        # 0xAFBF0010
    jal     0x400018            # 0x0C100006 (delay slot follows)
    nop                         # 0x00000000
    lw      $ra, 12($sp)        # 0x8FBF0010
    jr      $ra                 # 0x03E00008
    nop                         # 0x00000000
"""
import struct, sys

ELFMAG = b'\x7fELF'
ELFCLASS32 = 1
ELFDATA2MSB = 2
EV_CURRENT = 1
ET_EXEC = 2
EM_MIPS = 8
SHT_PROGBITS = 1
SHT_STRTAB = 3
SHF_ALLOC = 0x2
SHF_EXECINSTR = 0x4

text = struct.pack('>IIIIIII',
    0x27BDFFF0, 0xAFBF0010, 0x0C100006, 0x00000000,
    0x8FBF0010, 0x03E00008, 0x00000000,
)
text_size = len(text)

base_addr = 0x00400000
text_vaddr = base_addr + 0x1000
text_off = 0x1000
filesz = 0x2000

# Section header string table: "\0.text\0.shstrtab\0"
shstrtab = b'\x00.text\x00.shstrtab\x00'
shstrtab_off = 0x2000 + 0  # right after text segment padding (we put it at end)
# Layout:
# 0x000   ELF header (52)
# 0x034   Program header (32)
# 0x054   padding to 0x1000
# 0x1000  .text (28 bytes)
# 0x101C  padding to 0x2000
# 0x2000  .shstrtab (16 bytes)
# 0x2010  section headers (3 entries * 40 bytes = 120)
# total < 0x2100

shstrtab_off = 0x2000
shstrtab_size = len(shstrtab)
shoff = shstrtab_off + shstrtab_size
# Align shoff to 4
shoff = (shoff + 3) & ~3
filesz = shoff + 3 * 40

# ELF32 header
elf_header = bytearray(52)
elf_header[0:4] = ELFMAG
elf_header[4] = ELFCLASS32
elf_header[5] = ELFDATA2MSB
elf_header[6] = EV_CURRENT
struct.pack_into('>H', elf_header, 16, ET_EXEC)
struct.pack_into('>H', elf_header, 18, EM_MIPS)
struct.pack_into('>I', elf_header, 20, EV_CURRENT)
struct.pack_into('>I', elf_header, 24, text_vaddr)
struct.pack_into('>I', elf_header, 28, 52)        # e_phoff
struct.pack_into('>I', elf_header, 32, shoff)     # e_shoff
struct.pack_into('>I', elf_header, 36, 0)         # e_flags
struct.pack_into('>H', elf_header, 40, 52)        # e_ehsize
struct.pack_into('>H', elf_header, 42, 32)        # e_phentsize
struct.pack_into('>H', elf_header, 44, 1)         # e_phnum
struct.pack_into('>H', elf_header, 46, 40)        # e_shentsize
struct.pack_into('>H', elf_header, 48, 3)         # e_shnum
struct.pack_into('>H', elf_header, 50, 2)         # e_shstrndx

# Program header
phdr = bytearray(32)
struct.pack_into('>I', phdr, 0, 1)          # PT_LOAD
struct.pack_into('>I', phdr, 4, 0)
struct.pack_into('>I', phdr, 8, base_addr)
struct.pack_into('>I', phdr, 12, base_addr)
struct.pack_into('>I', phdr, 16, filesz)
struct.pack_into('>I', phdr, 20, filesz)
struct.pack_into('>I', phdr, 24, 5)         # PF_R | PF_X
struct.pack_into('>I', phdr, 28, 0x1000)

elf = bytes(elf_header) + bytes(phdr)
elf += b'\x00' * (text_off - len(elf))
elf += text
elf += b'\x00' * (shstrtab_off - len(elf))
elf += shstrtab
elf += b'\x00' * (shoff - len(elf))

# Section headers (40 bytes each): [0]=null, [1]=.text, [2]=.shstrtab
def shdr(name, type, flags, addr, offset, size, link, info, align, entsize):
    return struct.pack('>IIIIIIIIII', name, type, flags, addr, offset, size, link, info, align, entsize)

elf += shdr(0, 0, 0, 0, 0, 0, 0, 0, 0, 0)                              # [0] null
elf += shdr(1, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, text_vaddr, text_off, text_size, 0, 0, 4, 0)  # [1] .text
elf += shdr(7, SHT_STRTAB, 0, 0, shstrtab_off, shstrtab_size, 0, 0, 1, 0)  # [2] .shstrtab

sys.stdout.buffer.write(elf)

