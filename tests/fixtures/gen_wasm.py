#!/usr/bin/env python3
"""Generate a minimal WebAssembly (WASM) binary for Nyx v0.2.0 tests.

The binary contains:
  - WASM header (magic + version 1)
  - Type section: 1 function type (i32) -> (i32)
  - Function section: 1 function referencing type 0
  - Export section: exports function 0 as "add"
  - Code section: function body that returns its argument + 1
"""
import struct, sys

def uleb128(v):
    out = bytearray()
    while True:
        b = v & 0x7F
        v >>= 7
        if v:
            out.append(b | 0x80)
        else:
            out.append(b)
            break
    return bytes(out)

def section(sec_id, data):
    return bytes([sec_id]) + uleb128(len(data)) + data

# WASM magic + version
wasm = b'\x00asm' + struct.pack('<I', 1)

# Type section (id=1): 1 function type
# func type: 0x60, 1 param (i32=0x7F), 1 result (i32=0x7F)
type_entry = bytes([0x60]) + uleb128(1) + bytes([0x7F]) + uleb128(1) + bytes([0x7F])
type_section = uleb128(1) + type_entry  # count=1
wasm += section(1, type_section)

# Function section (id=3): 1 function, type index 0
func_section = uleb128(1) + uleb128(0)  # count=1, type_idx=0
wasm += section(3, func_section)

# Export section (id=7): export function 0 as "add"
export_entry = b'add'  # name (3 bytes, but we need length-prefixed)
export_entry = uleb128(3) + b'add' + bytes([0x00]) + uleb128(0)  # name, kind=func, idx=0
export_section = uleb128(1) + export_entry  # count=1
wasm += section(7, export_section)

# Code section (id=10): 1 function body
# Body: local.get 0; i32.const 1; i32.add; end
body_code = bytes([
    0x20, 0x00,  # local.get 0
    0x41, 0x01,  # i32.const 1
    0x6A,        # i32.add
    0x0B,        # end
])
# Function body: locals_count=0, then code
func_body = uleb128(0) + body_code  # 0 local declarations
code_entry = uleb128(len(func_body)) + func_body
code_section = uleb128(1) + code_entry  # count=1
wasm += section(10, code_section)

sys.stdout.buffer.write(wasm)
