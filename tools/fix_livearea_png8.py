#!/usr/bin/env python3
"""Rebuild LiveArea bg0/startup as Vita-safe 8-bit indexed PNGs from pic0."""
from __future__ import annotations

import re
import subprocess
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
LA = ROOT / "soloader-boilerplate" / "extras" / "livearea"
PNGQUANT = ROOT / "tools" / "pngquant" / "pngquant" / "pngquant.exe"


def cover(img: Image.Image, w: int, h: int, bias_x: float = 0.5, bias_y: float = 0.5) -> Image.Image:
    img = img.convert("RGB")
    scale = max(w / img.width, h / img.height)
    nw, nh = round(img.width * scale), round(img.height * scale)
    img = img.resize((nw, nh), Image.LANCZOS)
    left = int((nw - w) * bias_x)
    top = int((nh - h) * bias_y)
    left = max(0, min(left, nw - w))
    top = max(0, min(top, nh - h))
    return img.crop((left, top, left + w, top + h))


def save_png8(img: Image.Image, path: Path) -> None:
    tmp = path.with_suffix(".tmp.png")
    img.convert("RGB").save(tmp)
    subprocess.check_call(
        [str(PNGQUANT), "256", "--force", "--speed", "1", "-o", str(path), str(tmp)]
    )
    tmp.unlink(missing_ok=True)
    out = Image.open(path)
    print(f"{path.name}: {out.size} mode={out.mode} bytes={path.stat().st_size}")


def main() -> None:
    if not PNGQUANT.is_file():
        raise SystemExit(f"Missing pngquant: {PNGQUANT}")

    pic = Image.open(LA / "pic0.png").convert("RGB")

    # Page background: slight zoom, keep Red Ball + title
    bg = cover(pic, 840, 500, bias_x=0.22, bias_y=0.12)
    save_png8(bg, LA / "bg0.png")

    # Gate: full logo area, cover-fill the 280x158 system frame (no letterbox pads)
    logo = pic.crop((455, 5, 958, 185))
    gate = cover(logo, 280, 158, bias_x=0.42, bias_y=0.55)
    save_png8(gate, LA / "startup.png")

    xml_path = LA / "template.xml"
    xml = xml_path.read_text(encoding="utf-8")
    xml = re.sub(r'content-rev="\d+"', 'content-rev="7"', xml)
    xml_path.write_text(xml, encoding="ascii", newline="\n")
    print("template content-rev=7")


if __name__ == "__main__":
    main()
