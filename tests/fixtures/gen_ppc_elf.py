#!/usr/bin/env python3
"""Generate a minimal PowerPC 32-bit big-endian ELF executable for Nyx v0.0.3.

The binary contains a tiny .text section with this sequence:
    stwu    r1, -16(r1)     # 0x9421FFF0  (prologue: save SP)
    mflr    r0              # 0x7C0802A6  (move LR to r0)
    stw     r0, 20(r1)      # 0x90140014  (save LR)
    addi    r3, r2, 0       # 0x3C420000  (li r3, 0 via addi)
    lwz     r0, 20(r1)      # 0x80140014  (restore LR)
    mtlr    r0              # 0x7C0903A6
    addi    r1, r1, 16      # 0x38210010
    blr                     # 0x4E800020  (return)
"""
import struct, sys

ELFMAG = b'\x7fELF'
ELFCLASS32 = 1
ELFDATA2MSB = 2
EV_CURRENT = 1
ET_EXEC = 2
EM_PPC = 20
SHT_PROGBITS = 1
SHT_STRTAB = 3
SHF_ALLOC = 0x2
SHF_EXECINSTR = 0x4

text = struct.pack('>IIIIIIII',
    0x9421FFF0, 0x7C0802A6, 0x90140014, 0x3C420000,
    0x80140014, 0x7C0903A6, 0x38210010, 0x4E800020,
)
text_size = len(text)

base_addr = 0x10000000
text_vaddr = base_addr + 0x1000
text_off = 0x1000

shstrtab = b'\x00.text\x00.shstrtab\x00'
shstrtab_off = 0x2000
shstrtab_size = len(shstrtab)
shoff = shstrtab_off + shstrtab_size
shoff = (shoff + 3) & ~3
filesz = shoff + 3 * 40

elf_header = bytearray(52)
elf_header[0:4] = ELFMAG
elf_header[4] = ELFCLASS32
elf_header[5] = ELFDATA2MSB
elf_header[6] = EV_CURRENT
struct.pack_into('>H', elf_header, 16, ET_EXEC)
struct.pack_into('>H', elf_header, 18, EM_PPC)
struct.pack_into('>I', elf_header, 20, EV_CURRENT)
struct.pack_into('>I', elf_header, 24, text_vaddr)
struct.pack_into('>I', elf_header, 28, 52)
struct.pack_into('>I', elf_header, 32, shoff)
struct.pack_into('>I', elf_header, 36, 0)
struct.pack_into('>H', elf_header, 40, 52)
struct.pack_into('>H', elf_header, 42, 32)
struct.pack_into('>H', elf_header, 44, 1)
struct.pack_into('>H', elf_header, 46, 40)
struct.pack_into('>H', elf_header, 48, 3)
struct.pack_into('>H', elf_header, 50, 2)

phdr = bytearray(32)
struct.pack_into('>I', phdr, 0, 1)
struct.pack_into('>I', phdr, 4, 0)
struct.pack_into('>I', phdr, 8, base_addr)
struct.pack_into('>I', phdr, 12, base_addr)
struct.pack_into('>I', phdr, 16, filesz)
struct.pack_into('>I', phdr, 20, filesz)
struct.pack_into('>I', phdr, 24, 5)
struct.pack_into('>I', phdr, 28, 0x1000)

elf = bytes(elf_header) + bytes(phdr)
elf += b'\x00' * (text_off - len(elf))
elf += text
elf += b'\x00' * (shstrtab_off - len(elf))
elf += shstrtab
elf += b'\x00' * (shoff - len(elf))

def shdr(name, type, flags, addr, offset, size, link, info, align, entsize):
    return struct.pack('>IIIIIIIIII', name, type, flags, addr, offset, size, link, info, align, entsize)

elf += shdr(0, 0, 0, 0, 0, 0, 0, 0, 0, 0)
elf += shdr(1, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, text_vaddr, text_off, text_size, 0, 0, 4, 0)
elf += shdr(7, SHT_STRTAB, 0, 0, shstrtab_off, shstrtab_size, 0, 0, 1, 0)

sys.stdout.buffer.write(elf)
