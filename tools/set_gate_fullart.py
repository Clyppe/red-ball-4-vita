#!/usr/bin/env python3
"""Set LiveArea gate (and splash) to the provided full promo art with no zoom/crop."""
from __future__ import annotations

import subprocess
from pathlib import Path

from PIL import Image

SRC = Path(
    r"C:\Users\leoer\.cursor\projects\d-Projects-MMAudio\assets"
    r"\c__Users_leoer_AppData_Roaming_Cursor_User_workspaceStorage_"
    r"0865e18f5856d59e337d05ada54c017b_images_page-c428b50a-9d69-437a-a779-d1f12fc5c820.png"
)
LA = Path(__file__).resolve().parent.parent / "soloader-boilerplate" / "extras" / "livearea"
PNGQUANT = Path(__file__).resolve().parent / "pngquant" / "pngquant" / "pngquant.exe"


def contain_full(img: Image.Image, w: int, h: int) -> Image.Image:
    """Fit entire image into w×h with no crop/zoom; pad with edge color."""
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
    if not SRC.is_file():
        raise SystemExit(f"Missing source: {SRC}")
    if not PNGQUANT.is_file():
        raise SystemExit(f"Missing pngquant: {PNGQUANT}")

    src = Image.open(SRC).convert("RGB")
    print("src", src.size)

    gate = contain_full(src, 280, 158)
    save_png8(gate, LA / "startup.png")

    # Keep splash matching the gate image.
    splash = Image.open(LA / "startup.png").convert("RGB").resize((960, 544), Image.LANCZOS)
    save_png8(splash, LA / "pic0.png")

    (LA / "template.xml").write_text(
        '<?xml version="1.0" encoding="utf-8"?>\n\n'
        '<livearea style="psmobile" format-ver="01.00" content-rev="9">\n'
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
    print("template content-rev=9")


if __name__ == "__main__":
    main()
