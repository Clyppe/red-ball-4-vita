#!/usr/bin/env python3
"""Update icon, zoomed bg0, full-logo gate, splash=gate composition."""
from __future__ import annotations

import subprocess
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
LA = ROOT / "soloader-boilerplate" / "extras" / "livearea"
PNGQUANT = ROOT / "tools" / "pngquant" / "pngquant" / "pngquant.exe"
ICON_SRC = Path(
    r"C:\Users\leoer\.cursor\projects\d-Projects-MMAudio\assets"
    r"\c__Users_leoer_AppData_Roaming_Cursor_User_workspaceStorage_"
    r"0865e18f5856d59e337d05ada54c017b_images_iconnew-1db8cc80-92a2-44c3-a3be-5c5a1887fd4c.png"
)


def cover(img: Image.Image, w: int, h: int, bias_x: float = 0.5, bias_y: float = 0.5) -> Image.Image:
    img = img.convert("RGB")
    scale = max(w / img.width, h / img.height)
    nw, nh = round(img.width * scale), round(img.height * scale)
    img = img.resize((nw, nh), Image.LANCZOS)
    left = max(0, min(int((nw - w) * bias_x), nw - w))
    top = max(0, min(int((nh - h) * bias_y), nh - h))
    return img.crop((left, top, left + w, top + h))


def contain_on(img: Image.Image, w: int, h: int, bg_rgb: tuple[int, int, int]) -> Image.Image:
    img = img.convert("RGB")
    scale = min(w / img.width, h / img.height)
    nw, nh = max(1, round(img.width * scale)), max(1, round(img.height * scale))
    art = img.resize((nw, nh), Image.LANCZOS)
    out = Image.new("RGB", (w, h), bg_rgb)
    out.paste(art, ((w - nw) // 2, (h - nh) // 2))
    return out


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
    if not ICON_SRC.is_file():
        raise SystemExit(f"Missing icon source: {ICON_SRC}")

    # Preserve full keyart — pic0 will become the splash (same as gate).
    keyart_path = LA / "_keyart_src.png"
    if keyart_path.is_file():
        keyart = Image.open(keyart_path).convert("RGB")
    else:
        keyart = Image.open(LA / "pic0.png").convert("RGB")
        keyart.save(keyart_path)
        print(f"saved keyart source {keyart_path}")

    # --- icon0 128x128 ---
    icon = cover(Image.open(ICON_SRC), 128, 128, bias_x=0.5, bias_y=0.5)
    save_png8(icon, LA / "icon0.png")

    # --- bg0 840x500: zoom so RED BALL 4 sits above the right-side gate ---
    zoom = 1.42
    nw, nh = round(keyart.width * zoom), round(keyart.height * zoom)
    scaled = keyart.resize((nw, nh), Image.LANCZOS)
    # Keep Red Ball (left) + logo high on the right above the Start gate.
    left = max(0, min(int((nw - 840) * 0.28), nw - 840))
    top = max(0, min(int((nh - 500) * 0.05), nh - 500))
    bg = scaled.crop((left, top, left + 840, top + 500))
    save_png8(bg, LA / "bg0.png")
    print(f"bg0 zoom={zoom} crop=({left},{top})")

    # --- startup 280x158: full RED BALL 4 wordmark (not over-zoomed) ---
    logo = keyart.crop((445, 12, 955, 172))
    sky = logo.getpixel((logo.width // 3, 5))
    gate = contain_on(logo, 280, 158, sky)
    save_png8(gate, LA / "startup.png")

    # --- pic0 splash 960x544: same composition as the gate image ---
    splash = contain_on(logo, 960, 544, sky)
    save_png8(splash, LA / "pic0.png")

    # template content-rev bump (no BOM)
    (LA / "template.xml").write_text(
        '<?xml version="1.0" encoding="utf-8"?>\n\n'
        '<livearea style="psmobile" format-ver="01.00" content-rev="8">\n'
        "\t<livearea-background>\n"
        "\t\t<image>bg0.png</image>\n"
        "\t</livearea-background>\n\n"
        "\t<gate>\n"
        "\t\t<startup-image>startup.png</startup-image>\n"
        "\t</gate>\n"
        "</livearea>\n",
        encoding="ascii",
        newline="\n",
    )
    print("template content-rev=8")


if __name__ == "__main__":
    main()
