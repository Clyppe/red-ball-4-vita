#!/usr/bin/env python3
import re
import struct
from pathlib import Path

p = Path(r"D:/Projects/MMAudio/red-ball-4-vita/extracted/lib/armeabi-v7a/libcocos2dcpp.so")
with p.open("rb") as f:
    so = f.read()
print("size", len(so), flush=True)

names = [
    b"_ZN8Progress9selectAgeEv",
    b"_ZN11NativeUtils4isTVEv",
    b"_ZN11NativeUtils6isTab4Ev",
    b"_ZN7cocos2d13CCUserDefault",
    b"getBoolForKey",
    b"getIntegerForKey",
    b"setBoolForKey",
    b"setIntegerForKey",
    b"sharedUserDefault",
    b"getCocos2dxWritablePath",
    b"Java_com_FDGEntertainment_redball4_gp_RedBall4_playerAge",
]
for n in names:
    print(n.decode(), "->", hex(so.find(n)), flush=True)

print("\nCCUserDefault symbols:", flush=True)
for m in re.finditer(rb"_ZN[K]?7cocos2d13CCUserDefault[\x20-\x7e]{0,80}", so):
    s = m.group().split(b"\x00", 1)[0].decode("ascii", "replace")
    print(" ", s, "at", hex(m.start()), flush=True)

print("\nProgress symbols:", flush=True)
for m in re.finditer(rb"_ZN8Progress[\x20-\x7e]{0,80}", so):
    s = m.group().split(b"\x00", 1)[0].decode("ascii", "replace")
    print(" ", s, "at", hex(m.start()), flush=True)

# Parse DT_SYMTAB with bounded names
e_phoff = struct.unpack_from("<I", so, 28)[0]
e_phentsize, e_phnum = struct.unpack_from("<HH", so, 42)
loads = []
dynoff = dynsz = None
for i in range(e_phnum):
    off = e_phoff + i * e_phentsize
    p_type, p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_flags, p_align = struct.unpack_from("<IIIIIIII", so, off)
    if p_type == 1:
        loads.append((p_offset, p_vaddr, p_filesz))
    if p_type == 2:
        dynoff, dynsz = p_offset, p_filesz

def va_to_off(va):
    for p_offset, p_vaddr, p_filesz in loads:
        if p_vaddr <= va < p_vaddr + p_filesz:
            return p_offset + (va - p_vaddr)
    return None

strtab_va = symtab_va = syment = nchain = 0
for j in range(0, dynsz, 8):
    tag, val = struct.unpack_from("<II", so, dynoff + j)
    if tag == 5:
        strtab_va = val
    elif tag == 6:
        symtab_va = val
    elif tag == 11:
        syment = val
    elif tag == 4:
        hoff = va_to_off(val)
        nchain = struct.unpack_from("<II", so, hoff)[1]

strtab_off = va_to_off(strtab_va)
symtab_off = va_to_off(symtab_va)
print(f"\ndynsym nchain={nchain} syment={syment} strtab_off={strtab_off}", flush=True)

needles = (b"selectAge", b"isTV", b"isTab4", b"CCUserDefault", b"playerAge", b"BitmapDC", b"getBool", b"getInteger", b"setBool", b"sharedUser")
print("=== dynsym matches ===", flush=True)
count = min(nchain or 0, 20000)
for i in range(count):
    st_name, st_value, st_size, st_info, st_other, st_shndx = struct.unpack_from(
        "<IIIBBH", so, symtab_off + i * syment
    )
    if st_name == 0:
        continue
    end = so.find(b"\x00", strtab_off + st_name, strtab_off + st_name + 180)
    if end < 0:
        continue
    name = so[strtab_off + st_name : end]
    if any(n in name for n in needles):
        print(f"  {name.decode()} va=0x{st_value:x} size={st_size} shndx={st_shndx} info=0x{st_info:x}", flush=True)
