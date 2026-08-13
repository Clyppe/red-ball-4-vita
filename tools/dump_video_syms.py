#!/usr/bin/env python3
import struct
from pathlib import Path

so = Path(r"D:/Projects/MMAudio/red-ball-4-vita/extracted/lib/armeabi-v7a/libcocos2dcpp.so").read_bytes()
soff, size, entsize = 0x18C, 0x50970, 16
ds_off, ds_sz = 0x50AFC, 0xBB581
ds = so[ds_off : ds_off + ds_sz]
e_phoff = struct.unpack_from("<I", so, 28)[0]
e_phentsize, e_phnum = struct.unpack_from("<HH", so, 42)
loads = []
for i in range(e_phnum):
    off = e_phoff + i * e_phentsize
    t, po, pv, pp, pf, pm, fl, al = struct.unpack_from("<IIIIIIII", so, off)
    if t == 1:
        loads.append((po, pv, pf))


def va_to_off(va):
    va &= ~1
    for o, v, s in loads:
        if v <= va < v + s:
            return o + (va - v)
    return None


out = []
for i in range(0, size, entsize):
    st_name, st_value, st_size, st_info, st_other, st_shndx = struct.unpack_from(
        "<IIIBBH", so, soff + i
    )
    end = ds.find(b"\0", st_name)
    if end < 0 or end - st_name > 200:
        continue
    name = ds[st_name:end].decode("ascii", "replace")
    if "Cocos2dxVideo" in name or name.endswith("RedBall4_pause") or name.endswith(
        "RedBall4_resume"
    ):
        off = va_to_off(st_value)
        hx = so[off : off + 48].hex(" ") if off is not None else "NOOFF"
        out.append(f"{name} va={st_value:#x} sz={st_size}\n  {hx}")

text = "\n".join(out)
Path(r"D:/Projects/MMAudio/red-ball-4-vita/tools/_video_syms.txt").write_text(
    text, encoding="utf-8"
)
print(text)
print("count", len(out))
