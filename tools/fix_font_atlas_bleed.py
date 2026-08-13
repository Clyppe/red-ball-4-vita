"""Clear 1px gutters around BMFont glyphs to stop bilinear white bleed."""
from __future__ import annotations

import re
import shutil
from pathlib import Path

from PIL import Image

FONT_DIR = Path(r"F:/data/rdball4/assets/Fonts/FontsMedium")


def parse_chars(fnt: Path) -> list[dict]:
    chars = []
    for line in fnt.read_text(encoding="utf-8", errors="replace").splitlines():
        if not line.startswith("char "):
            continue
        fields = dict(re.findall(r"(\w+)=(-?\d+)", line))
        chars.append({k: int(fields[k]) for k in ("id", "x", "y", "width", "height")})
    return chars


def classify(p, inverted: bool) -> str:
    r, g, b, a = p
    if a < 8:
        return "empty"
    is_red = r > 160 and g < 140 and b < 140 and r > g + 40
    is_white = r > 220 and g > 220 and b > 220
    if inverted:
        if is_white or (r > 200 and g > 180 and b > 180 and not is_red):
            return "stroke"
        return "fill"
    if is_red or (r > 160 and g < 200 and not is_white):
        return "stroke"
    return "fill"


def thin_outer_stroke(im: Image.Image, inverted: bool) -> int:
    w, h = im.size
    px = im.load()
    outer = [[False] * w for _ in range(h)]
    for y in range(h):
        for x in range(w):
            if classify(px[x, y], inverted) != "stroke":
                continue
            edge = False
            for oy in (-1, 0, 1):
                for ox in (-1, 0, 1):
                    if ox == 0 and oy == 0:
                        continue
                    nx, ny = x + ox, y + oy
                    if nx < 0 or ny < 0 or nx >= w or ny >= h or classify(px[nx, ny], inverted) == "empty":
                        edge = True
                        break
                if edge:
                    break
            outer[y][x] = edge
    cleared = 0
    for y in range(h):
        for x in range(w):
            if outer[y][x]:
                px[x, y] = (0, 0, 0, 0)
                cleared += 1
    return cleared


def clear_gutters(im: Image.Image, chars: list[dict]) -> int:
    w, h = im.size
    px = im.load()
    n = 0
    for ch in chars:
        if ch["width"] <= 0 or ch["height"] <= 0:
            continue
        x0, y0 = ch["x"], ch["y"]
        x1, y1 = x0 + ch["width"], y0 + ch["height"]
        for x in range(x0, x1):
            for y in (y0, y1 - 1):
                if 0 <= x < w and 0 <= y < h and px[x, y][3]:
                    px[x, y] = (0, 0, 0, 0)
                    n += 1
        for y in range(y0, y1):
            for x in (x0, x1 - 1):
                if 0 <= x < w and 0 <= y < h and px[x, y][3]:
                    px[x, y] = (0, 0, 0, 0)
                    n += 1
    return n


def zero_rgb_under_clear(im: Image.Image) -> int:
    px = im.load()
    w, h = im.size
    n = 0
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            if a == 0 and (r or g or b):
                px[x, y] = (0, 0, 0, 0)
                n += 1
    return n


def process(png_name: str, fnt_name: str, inverted: bool) -> None:
    png = FONT_DIR / png_name
    bak = FONT_DIR / (png_name + ".bak")
    fnt = FONT_DIR / fnt_name
    if bak.exists():
        shutil.copy2(bak, png)
    im = Image.open(png).convert("RGBA")
    thinned = thin_outer_stroke(im, inverted)
    gutters = clear_gutters(im, parse_chars(fnt))
    junk = zero_rgb_under_clear(im)
    im.save(png, optimize=True)
    print(f"{png_name}: thin={thinned} gutter={gutters} rgb0={junk}")


def main() -> None:
    process("titles.png", "titles.fnt", inverted=False)
    process("titlesInv.png", "titlesInv.fnt", inverted=True)


if __name__ == "__main__":
    main()
