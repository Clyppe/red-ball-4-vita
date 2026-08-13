"""Pack ux0:data/rdball4/ from the local armeabi-v7a APK (no downloads)."""
from pathlib import Path
import shutil

ROOT = Path(__file__).resolve().parent.parent
APK = ROOT / "redball4-armeabi-v7a.apk"
SO = ROOT / "extracted" / "lib" / "armeabi-v7a" / "libcocos2dcpp.so"
ASSETS = ROOT / "extracted" / "assets"
OUT = ROOT / "data-pack" / "rdball4"


def main():
    if not APK.is_file():
        raise SystemExit(f"Missing APK: {APK}")
    if not SO.is_file():
        raise SystemExit(f"Missing .so: {SO}\nExtract the APK first.")

    if OUT.exists():
        shutil.rmtree(OUT)
    OUT.mkdir(parents=True)

    shutil.copy2(SO, OUT / "libcocos2dcpp.so")
    shutil.copy2(APK, OUT / "game.apk")
    if ASSETS.is_dir():
        shutil.copytree(ASSETS, OUT / "assets")
    (OUT / "save").mkdir(exist_ok=True)
    (OUT / "cache").mkdir(exist_ok=True)

    # Cutscenes: prefer Vita-safe Baseline encodes (outside OUT — OUT is wiped above).
    videos = OUT / "videos"
    videos.mkdir(exist_ok=True)
    encoded = ROOT / "data-pack" / "videos-vita"
    raw = ROOT / "extracted" / "res" / "raw"
    nvid = 0
    src_dir = encoded if encoded.is_dir() and any(encoded.glob("*.mp4")) else raw
    if src_dir.is_dir():
        for mp4 in sorted(src_dir.glob("*.mp4")):
            shutil.copy2(mp4, videos / mp4.name)
            nvid += 1

    so_mb = (OUT / "libcocos2dcpp.so").stat().st_size / (1024 * 1024)
    apk_mb = (OUT / "game.apk").stat().st_size / (1024 * 1024)
    print(f"Wrote {OUT}")
    print(f"  libcocos2dcpp.so  {so_mb:.1f} MB")
    print(f"  game.apk          {apk_mb:.1f} MB")
    print(f"  videos/           {nvid} mp4 files")
    print("Copy this folder to the Vita as ux0:data/rdball4/")


if __name__ == "__main__":
    main()
