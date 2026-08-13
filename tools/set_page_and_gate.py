#!/usr/bin/env python3
"""Set LiveArea bg0 from page art, startup/pic0 from start art."""
from __future__ import annotations

import subprocess
from pathlib import Path

from PIL import Image

ASSETS = Path(r"C:\Users\leoer\.cursor\projects\d-Projects-MMAudio\assets")
PAGE_SRC = ASSETS / (
    "c__Users_leoer_AppData_Roaming_Cursor_User_workspaceStorage_"
    "0865e18f5856d59e337d05ada54c017b_images_page-c428b50a-9d69-437a-a779-d1f12fc5c820.png"
)
GATE_SRC = ASSETS / (
    "c__Users_leoer_AppData_Roaming_Cursor_User_workspaceStorage_"
    "0865e18f5856d59e337d05ada54c017b_images_start-dae6d661-0b3c-441b-b8b8-ed3c132b3021.png"
)
LA = Path(__file__).resolve().parent.parent / "soloader-boilerplate" / "extras" / "livearea"
PNGQUANT = Path(__file__).resolve().parent / "pngquant" / "pngquant" / "pngquant.exe"


def cover(img: Image.Image, w: int, h: int, bias_x: float = 0.5, bias_y: float = 0.5) -> Image.Image:
    img = img.convert("RGB")
    scale = max(w / img.width, h / img.height)
    nw, nh = round(img.width * scale), round(img.height * scale)
    img = img.resize((nw, nh), Image.LANCZOS)
    left = max(0, min(int((nw - w) * bias_x), nw - w))
    top = max(0, min(int((nh - h) * bias_y), nh - h))
    return img.crop((left, top, left + w, top + h))


def contain_full(img: Image.Image, w: int, h: int) -> Image.Image:
    """Fit entire image with no crop; pad with edge color."""
    img = img.convert("RGB")
    scale = min(w / img.width, h / img.height)
    nw, nh = max(1, round(img.width * scale)), max(1, round(img.height * scale))
    art = img.resize((nw, nh), Image.LANCZOS)
    px = img.load()
    samples = [
        px[0, 0],
        px[img.width - 1, 0],
        px[0, img.height - 1],
        px[img.width - 1, img.height - 1],
    ]
    bg = tuple(sum(c[i] for c in samples) // 4 for i in range(3))
    out = Image.new("RGB", (w, h), bg)
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
    if not PAGE_SRC.is_file():
        raise SystemExit(f"Missing page src: {PAGE_SRC}")
    if not GATE_SRC.is_file():
        raise SystemExit(f"Missing gate src: {GATE_SRC}")
    if not PNGQUANT.is_file():
        raise SystemExit(f"Missing pngquant: {PNGQUANT}")

    page = Image.open(PAGE_SRC).convert("RGB")
    gate_src = Image.open(GATE_SRC).convert("RGB")
    print("page", page.size, "gate", gate_src.size)

    # Page background fills 840x500 (mild cover so it fills the paper).
    # Bias keeps Red Ball left + title upper-right above the gate.
    bg = cover(page, 840, 500, bias_x=0.28, bias_y=0.12)
    save_png8(bg, LA / "bg0.png")

    # Gate: full start art, no zoom/crop into the logo.
    gate = contain_full(gate_src, 280, 158)
    save_png8(gate, LA / "startup.png")

    # Splash matches gate.
    splash = Image.open(LA / "startup.png").convert("RGB").resize((960, 544), Image.LANCZOS)
    save_png8(splash, LA / "pic0.png")

    (LA / "template.xml").write_text(
        '<?xml version="1.0" encoding="utf-8"?>\n\n'
        '<livearea style="psmobile" format-ver="01.00" content-rev="10">\n'
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
    print("template content-rev=10")


if __name__ == "__main__":
    main()
