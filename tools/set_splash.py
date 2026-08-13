#!/usr/bin/env python3
from __future__ import annotations

import re
import subprocess
from pathlib import Path

from PIL import Image

SRC = Path(
    r"C:\Users\leoer\.cursor\projects\d-Projects-MMAudio\assets"
    r"\c__Users_leoer_AppData_Roaming_Cursor_User_workspaceStorage_"
    r"0865e18f5856d59e337d05ada54c017b_images_start-87f9a43a-3bb7-4665-b969-2d57ac56bc28.png"
)
LA = Path(__file__).resolve().parent.parent / "soloader-boilerplate" / "extras" / "livearea"
PNGQUANT = Path(__file__).resolve().parent / "pngquant" / "pngquant" / "pngquant.exe"


def main() -> None:
    src = Image.open(SRC).convert("RGB")
    print("src", src.size)
    w, h = 960, 544
    scale = max(w / src.width, h / src.height)
    nw, nh = round(src.width * scale), round(src.height * scale)
    art = src.resize((nw, nh), Image.LANCZOS)
    left, top = (nw - w) // 2, (nh - h) // 2
    out = art.crop((left, top, left + w, top + h))

    tmp = LA / "pic0.tmp.png"
    out.save(tmp)
    subprocess.check_call(
        [str(PNGQUANT), "256", "--force", "--speed", "1", "-o", str(LA / "pic0.png"), str(tmp)]
    )
    tmp.unlink(missing_ok=True)
    im = Image.open(LA / "pic0.png")
    print(f"pic0: {im.size} mode={im.mode} bytes={(LA / 'pic0.png').stat().st_size}")

    xml_path = LA / "template.xml"
    xml = xml_path.read_text(encoding="utf-8")
    xml = re.sub(r'content-rev="\d+"', 'content-rev="11"', xml)
    xml_path.write_text(xml, encoding="ascii", newline="\n")
    print("content-rev=11")


if __name__ == "__main__":
    main()
