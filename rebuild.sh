#!/bin/bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"

# Always prefer softfp — hardfp cannot load Android ARMv7 .so files.
if [ -d /d/vitasdk-softfp/vitasdk ]; then
  export VITASDK=/d/vitasdk-softfp/vitasdk
elif [ -z "${VITASDK:-}" ]; then
  if [ -d /c/Users/leoer/vitasdk ]; then
    export VITASDK=/c/Users/leoer/vitasdk
  elif [ -d /d/vitasdk ]; then
    export VITASDK=/d/vitasdk
  fi
fi

if [ -z "${VITASDK:-}" ] || [ ! -d "$VITASDK" ]; then
  echo "Set VITASDK to your VitaSDK *softfp* install (needed to load Android ARMv7 .so files)."
  exit 1
fi

export PATH="$VITASDK/bin:$ROOT/tools/make/bin:/d/Projects/tocakitchen2-vita/tools/cmake/cmake-3.31.6-windows-x86_64/bin:/d/Projects/tocakitchen2-vita/tools/ninja:/usr/bin:$PATH"
echo "VITASDK=$VITASDK"

BUILD="$ROOT/soloader-boilerplate/build"
mkdir -p "$BUILD"
cd "$BUILD"
if command -v ninja >/dev/null 2>&1; then
  cmake .. -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_MAKE_PROGRAM="$(command -v ninja)"
  cmake --build . --parallel 4
else
  cmake .. -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release
  make -j4
fi
ls -la RedBall4.vpk
if [ -d /c/Users/leoer/Downloads ]; then
  cp -f RedBall4.vpk /c/Users/leoer/Downloads/RedBall4.vpk
  echo "Copied to Downloads/RedBall4.vpk"
fi
if [ -d /f/ ]; then
  cp -f RedBall4.vpk /f/RedBall4.vpk
  echo "Copied to F:/RedBall4.vpk"
fi
