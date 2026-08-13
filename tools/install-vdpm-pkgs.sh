#!/bin/bash
set -euo pipefail
export VITASDK=/d/vitasdk-softfp/vitasdk
export PATH="$VITASDK/bin:/usr/bin:$PATH"
VDPM=/d/Projects/MMAudio/red-ball-4-vita/tools/vdpm/vdpm
echo "VITASDK=$VITASDK"
for p in zlib kubridge taihen vitaShaRK SceShaccCgExt libmathneon opensles libsndfile libogg libvorbis flac opus mpg123 lame; do
  echo "==== installing $p ===="
  "$VDPM" "$p" || echo "FAILED $p"
done
echo DONE
ls "$VITASDK/arm-vita-eabi/lib" | grep -Ei 'OpenSL|sndfile|vorbis|kubridge|shark|mathneon|FLAC|mpg|lame|ogg|opus' || true
