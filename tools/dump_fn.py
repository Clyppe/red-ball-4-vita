#!/usr/bin/env python3
import struct
from pathlib import Path

so = Path(r"D:/Projects/MMAudio/red-ball-4-vita/extracted/lib/armeabi-v7a/libcocos2dcpp.so").read_bytes()
e_phoff = struct.unpack_from("<I", so, 28)[0]
e_phentsize, e_phnum = struct.unpack_from("<HH", so, 42)
loads = []
for i in range(e_phnum):
    off = e_phoff + i * e_phentsize
    p_type, p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_flags, p_align = struct.unpack_from("<IIIIIIII", so, off)
    if p_type == 1:
        loads.append((p_offset, p_vaddr, p_filesz))
        print(f"LOAD off=0x{p_offset:x} va=0x{p_vaddr:x} sz=0x{p_filesz:x}")

def va_to_off(va):
    va &= ~1
    for p_offset, p_vaddr, p_filesz in loads:
        if p_vaddr <= va < p_vaddr + p_filesz:
            return p_offset + (va - p_vaddr)
    return None

def dump(va, n=64):
    off = va_to_off(va)
    print(f"\nva=0x{va:x} file=0x{off:x}")
    b = so[off:off+n]
    print(b.hex(" "))

dump(0x1e48bd, 32)  # selectAge
dump(0x2ed101, 140)  # getBoolForKey key,bool
dump(0x2ed185, 16)  # getBoolForKey key
dump(0x2ecf3d, 8)  # createXMLFile
dump(0x2e4a7d, 64)  # getBoolForKeyJNI
dump(0x1bad99, 24)  # isTV
dump(0x1bb7c5, 28)  # playerAge JNI

# SharedPreferences / JNI method names
import re
print("\nJNI/prefs strings:")
for m in re.finditer(rb"[\x20-\x7e]{6,60}", so):
    t = m.group().decode()
    if any(k in t for k in ("SharedPref", "getBoolean", "getInt", "putBoolean", "Cocos2dxHelper", "getBoolForKey", "nativeGet")):
        if "png" in t or "jpeg" in t:
            continue
        print(" ", t)
