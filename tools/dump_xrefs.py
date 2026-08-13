#!/usr/bin/env python3
import struct
from pathlib import Path

so = Path(r"D:/Projects/MMAudio/red-ball-4-vita/extracted/lib/armeabi-v7a/libcocos2dcpp.so").read_bytes()
needle = b"RedBall4_player_age_selected\x00"
pos = so.find(needle)
print("string file off", hex(pos))
# vaddr in first PT_LOAD is file offset (va 0)
print("string va", hex(pos))

# find 32-bit LE pointer to this VA
ptr = struct.pack("<I", pos)
idx = 0
refs = []
while True:
    i = so.find(ptr, idx)
    if i < 0:
        break
    refs.append(i)
    idx = i + 1
print("absolute ptr refs", [hex(r) for r in refs[:30]], "count", len(refs))

# also addend-relative: in ARM, often literal is the va itself
# search nearby functions: who is within 0x200 of a literal pool containing this

# Thumb BL from selectAge already known. Find all occurrences of the shorter key too.
pos2 = so.find(b"RedBall4_player_age\x00")
print("age key file off", hex(pos2), "not selected")
ptr2 = struct.pack("<I", pos2)
idx = 0
refs2 = []
while True:
    i = so.find(ptr2, idx)
    if i < 0:
        break
    refs2.append(i)
    idx = i + 1
print("age key ptr refs", [hex(r) for r in refs2[:30]], "count", len(refs2))

# dump 32 bytes around first few refs
for r in refs[:8]:
    print("ref", hex(r), so[r-16:r+8].hex(" "))
