# Red Ball 4 — PS Vita

Unofficial **so-loader** port of the Android **Cocos2d-x** build of Red Ball 4.

> **Legal:** This repository contains only the loader. Game data is provided separately. Do not ask for or share copyrighted APKs or `.so` files in issues.

## Downloads

| What | Where |
|------|--------|
| **VPK** (loader) | [GitHub Releases](https://github.com/Clyppe/red-ball-4-vita/releases) — install `RedBall4.vpk` |
| **Data files** | [MediaFire — REDBALL4DATA.zip](https://www.mediafire.com/file/ioa5bk8217k3zgh/REDBALL4DATA.zip) |

## Status

- Loads `libcocos2dcpp.so` from the armeabi-v7a Cocos2d-x APK (not the Unity / arm64 XAPK)
- Landscape 960×544
- Touch + D-pad / analog / face buttons
- Ads / cloud / IAP Java calls are stubbed off
- Title ID: `RDBL40001`

The Unity 6 XAPK (`arm64-v8a` / IL2CPP) **cannot** be used with so-loader.

## Requirements (device)

- PS Vita / PSTV with HENkaku / Ensō
- [kubridge](https://github.com/bythos14/kubridge) kernel plugin
- [libshacccg.suprx](https://github.com/Rinnegatamante/vitaGL/#install) (e.g. via ShaRKBR33D)

## Install (players)

1. Download **`RedBall4.vpk`** from [Releases](https://github.com/Clyppe/red-ball-4-vita/releases) and install it (VitaShell / Package Installer).
2. Download **[REDBALL4DATA.zip](https://www.mediafire.com/file/ioa5bk8217k3zgh/REDBALL4DATA.zip)** and extract so you have an `rdball4` folder.
3. Copy that folder to the Vita as:

```text
ux0:data/rdball4/
  libcocos2dcpp.so
  game.apk
  assets/
  videos/
  save/
  cache/
```

4. Launch **Red Ball 4** from LiveArea.

## Build (developers)

Needs [VitaSDK **softfp**](https://github.com/vitasdk-softfp) (`VITASDK` env var). Official hardfp VitaSDK will not correctly run Android `.so` files.

```sh
./rebuild.sh
```

Produces `soloader-boilerplate/build/RedBall4.vpk`.

## Controls

- Touch — menus and in-game (same as the phone build)
- D-pad / left analog — movement
- Cross — Android BUTTON_A
- Circle — Back

## Credits

- FDG Entertainment — original game
- [v-atamanenko / soloader-boilerplate](https://github.com/v-atamanenko/soloader-boilerplate), FalsoJNI
- [Rinnegatamante](https://github.com/Rinnegatamante) — vitaGL, so_util lineage
- TheFloW — so_util foundations
- Cut the Rope Vita port — loader pattern this project follows

## License

Loader scaffolding follows the MIT-licensed boilerplate components where applicable.
Game assets and `libcocos2dcpp.so` remain property of their owners and are **not** redistributed in this repository.
