"""Build Vita LiveArea assets (8-bit indexed PNG, correct sizes)."""
from pathlib import Path
import subprocess
import sys

from PIL import Image

ASSETS = Path(r"C:\Users\leoer\.cursor\projects\d-Projects-MMAudio\assets")
PAGE = ASSETS / "c__Users_leoer_AppData_Roaming_Cursor_User_workspaceStorage_empty-window_images_page-65f98fbf-9192-43ef-97cc-37d94b893654.png"
START = ASSETS / "c__Users_leoer_AppData_Roaming_Cursor_User_workspaceStorage_empty-window_images_start-e18b9031-aa88-4dd8-952b-d1c526e27075.png"
ICON = ASSETS / "c__Users_leoer_AppData_Roaming_Cursor_User_workspaceStorage_empty-window_images_icon-99072ae1-becf-430f-8753-dd592d081c72.png"

ROOT = Path(__file__).resolve().parent.parent
LA = ROOT / "soloader-boilerplate" / "extras" / "livearea"
PNGQUANT = ROOT / "tools" / "pngquant" / "pngquant" / "pngquant.exe"

TEMPLATE = """<?xml version="1.0" encoding="utf-8"?>

<livearea style="psmobile" format-ver="01.00" content-rev="3">
	<livearea-background>
		<image>bg0.png</image>
	</livearea-background>

	<gate>
		<startup-image>startup.png</startup-image>
	</gate>
</livearea>
"""


def cover(img: Image.Image, w: int, h: int) -> Image.Image:
    img = img.convert("RGB")
    scale = max(w / img.width, h / img.height)
    nw, nh = round(img.width * scale), round(img.height * scale)
    img = img.resize((nw, nh), Image.LANCZOS)
    left, top = (nw - w) // 2, (nh - h) // 2
    return img.crop((left, top, left + w, top + h))


def save_png8(img: Image.Image, path: Path):
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_suffix(".tmp.png")
    img.save(tmp, format="PNG")
    cmd = [
        str(PNGQUANT),
        "256",
        "--force",
        "--speed",
        "1",
        "-o",
        str(path),
        str(tmp),
    ]
    subprocess.check_call(cmd)
    tmp.unlink(missing_ok=True)
    print(f"{path.name}: {img.size[0]}x{img.size[1]} pngquant -> {path} ({path.stat().st_size} bytes)")


def main():
    if not PNGQUANT.is_file():
        sys.exit(f"Missing pngquant: {PNGQUANT}")
    LA.mkdir(parents=True, exist_ok=True)
    save_png8(cover(Image.open(ICON), 128, 128), LA / "icon0.png")
    save_png8(cover(Image.open(PAGE), 960, 544), LA / "pic0.png")
    save_png8(cover(Image.open(PAGE), 840, 500), LA / "bg0.png")
    save_png8(cover(Image.open(START), 280, 158), LA / "startup.png")
    (LA / "template.xml").write_text(TEMPLATE, encoding="ascii", newline="\n")
    print("template.xml: ascii, no BOM")


if __name__ == "__main__":
    main()
