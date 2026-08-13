#!/usr/bin/env python3
"""Fix LiveArea art and restore UserDefault.xml from prefs.txt."""
from __future__ import annotations

import io
import xml.sax.saxutils as xu
import zipfile
from pathlib import Path

from PIL import Image

ROOT = Path(r"D:\Projects\MMAudio\red-ball-4-vita\soloader-boilerplate\extras\livearea")
VPK = Path(r"C:\Users\leoer\Downloads\RedBall4.vpk")
PREFS = Path(r"F:\data\rdball4\save\prefs.txt")
XML_OUT = Path(r"F:\data\rdball4\save\UserDefault.xml")


def fix_startup() -> None:
    with zipfile.ZipFile(VPK) as z:
        st = Image.open(io.BytesIO(z.read("sce_sys/livearea/contents/startup.png"))).convert("RGBA")
    px = st.load()
    w, h = st.size

    def is_content(x: int, y: int) -> bool:
        r, g, b, a = px[x, y]
        if a < 15:
            return False
        if r < 12 and g < 12 and b < 12:
            return False
        # Skip the light bezel we previously baked in.
        if r > 200 and g > 200 and b > 200:
            return False
        return True

    ys = [y for y in range(h) if any(is_content(x, y) for x in range(w))]
    xs = [x for x in range(w) if any(is_content(x, y) for y in range(h))]
    content = st.crop((min(xs), min(ys), max(xs) + 1, max(ys) + 1))

    tw, th = 280, 158
    cw, ch = content.size
    scale = max(tw / cw, th / ch)
    nw, nh = int(cw * scale + 0.5), int(ch * scale + 0.5)
    art = content.resize((nw, nh), Image.LANCZOS)
    x = (nw - tw) // 2
    y = (nh - th) // 2
    startup = art.crop((x, y, x + tw, y + th))
    startup.save(ROOT / "startup.png")
    print("startup filled", startup.size, "from", content.size)


def fix_bg0() -> None:
    bg = Image.open(ROOT / "bg0.png").convert("RGBA")
    # If already zoomed from a prior run, reload original from VPK when needed.
    # Prefer the package original so re-runs are stable.
    with zipfile.ZipFile(VPK) as z:
        src = Image.open(io.BytesIO(z.read("sce_sys/livearea/contents/bg0.png"))).convert("RGBA")
    zoom = 1.24
    nw, nh = int(src.width * zoom), int(src.height * zoom)
    scaled = src.resize((nw, nh), Image.LANCZOS)
    # Keep left (Red Ball) and top (logo above gate on the right).
    out = scaled.crop((0, 0, 840, 500))
    out.convert("RGB").save(ROOT / "bg0.png", optimize=True)
    print("bg0 zoomed from", src.size, "->", out.size, "zoom", zoom)


def restore_userdefault() -> None:
    keys: list[tuple[str, str]] = []
    if PREFS.exists():
        for line in PREFS.read_text(encoding="utf-8", errors="ignore").splitlines():
            line = line.strip()
            if not line or "=" not in line:
                continue
            k, v = line.split("=", 1)
            keys.append((k, v))
    have = {k for k, _ in keys}
    for k, v in (
        ("RedBall4_player_age_selected", "true"),
        ("RedBall4_player_age", "99"),
        ("lifes", "5"),
        ("lives", "5"),
    ):
        if k not in have:
            keys.insert(0, (k, v))

    lines = ['<?xml version="1.0" encoding="UTF-8"?>', "<userDefaultRoot>"]
    for k, v in keys:
        lines.append(f"    <{k}>{xu.escape(v)}</{k}>")
    lines.append("</userDefaultRoot>")
    lines.append("")
    XML_OUT.parent.mkdir(parents=True, exist_ok=True)
    XML_OUT.write_text("\n".join(lines), encoding="utf-8", newline="\n")
    print("wrote", XML_OUT, "keys", len(keys))


if __name__ == "__main__":
    fix_startup()
    fix_bg0()
    restore_userdefault()
