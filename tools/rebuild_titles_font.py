"""
Thin + smooth the existing Shark Soft Bites BMFont atlases in-place.

Keeps the original BMFont-tool glyph shapes/AA (better than re-rasterizing
from TTF), but:
  - pulls the red outline in by ~1px (less thick)
  - gently softens remaining edge coverage
"""
from __future__ import annotations

from pathlib import Path

from PIL import Image, ImageFilter

FONT_DIR = Path(r"F:/data/rdball4/assets/Fonts/FontsMedium")
EXTRACTED_DIR = Path(__file__).resolve().parents[1] / "extracted" / "assets" / "Fonts" / "FontsMedium"


def classify(p: tuple[int, int, int, int], inverted: bool) -> str:
    r, g, b, a = p
    if a < 8:
        return "empty"
    is_red = r > 160 and g < 140 and b < 140 and r > g + 40
    is_white = r > 220 and g > 220 and b > 220
    if inverted:
        # titlesInv: red fill, white outline
        if is_white or (r > 200 and g > 180 and b > 180 and not is_red):
            return "stroke"
        return "fill"
    # titles: white fill, red outline
    if is_red or (r > 160 and g < 200 and not is_white):
        return "stroke"
    return "fill"


def thin_atlas(src: Path, inverted: bool) -> Image.Image:
    im = Image.open(src).convert("RGBA")
    w, h = im.size
    px = im.load()

    # Pass 1: mark stroke pixels that touch empty (outer outline).
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
                    if nx < 0 or ny < 0 or nx >= w or ny >= h:
                        edge = True
                        break
                    if classify(px[nx, ny], inverted) == "empty":
                        edge = True
                        break
                if edge:
                    break
            outer[y][x] = edge

    # Pass 2: erase outer stroke ring (thin by 1px).
    out = im.copy()
    opx = out.load()
    cleared = 0
    for y in range(h):
        for x in range(w):
            if outer[y][x]:
                opx[x, y] = (0, 0, 0, 0)
                cleared += 1

    # Light edge soften only on remaining partial-alpha texels (no halo regrow).
    soft = out.filter(ImageFilter.GaussianBlur(radius=0.45))
    spx = soft.load()
    for y in range(h):
        for x in range(w):
            r, g, b, a = opx[x, y]
            if a == 0 or a == 255:
                continue
            sa = spx[x, y][3]
            opx[x, y] = (r, g, b, (a + sa) // 2)

    print(f"{src.name}: cleared {cleared} outer stroke pixels (inverted={inverted})")
    return out


def process(name: str, inverted: bool) -> None:
    src = FONT_DIR / name
    bak = FONT_DIR / (name + ".bak")
    if bak.exists():
        # Always start from pristine backup.
        im = thin_atlas(bak, inverted)
    else:
        bak.write_bytes(src.read_bytes())
        im = thin_atlas(src, inverted)
    im.save(src, optimize=True)
    if EXTRACTED_DIR.exists():
        EXTRACTED_DIR.mkdir(parents=True, exist_ok=True)
        im.save(EXTRACTED_DIR / name, optimize=True)
    print(f"wrote {src}")


def main() -> None:
    process("titles.png", inverted=False)
    process("titlesInv.png", inverted=True)


if __name__ == "__main__":
    main()
