#!/bin/bash
# Optional stage 0-: a private Boehm GC (bdwgc 8.2.12, same release MSYS2
# ships) for environments where MSYS2 has no gc package (MINGW32 today), or
# when win32 threads + DllMain thread registration are wanted (what Mono's own
# 2007-era Windows build used: libgc_threads=win32, --enable-win32-dllmain).
# Source: https://github.com/bdwgc/bdwgc/releases/download/v8.2.12/gc-8.2.12.tar.gz
# sha256 42e5194ad06ab6ffb806c83eb99c03462b495d979cda782f3c72c08af833cd4e (= MSYS2 mingw-w64-gc)
set -euo pipefail
. "$(dirname "$0")/env.sh"; . "$(dirname "$0")/lib.sh"
TAR=$DNB/src/gc-8.2.12.tar.gz
echo "42e5194ad06ab6ffb806c83eb99c03462b495d979cda782f3c72c08af833cd4e  $TAR" | sha256sum -c -
[ -f "$STAGE0/lib/libgc.dll.a" ] && { log "bdwgc already installed in $STAGE0"; exit 0; }
W=$DNB/build/bdwgc$BOOT_SFX; mkdir -p "$W"; [ -d "$W/gc-8.2.12" ] || tar -C "$W" -xzf "$TAR"
mkdir -p "$W/build"; cd "$W/build"
[ -f Makefile ] || ../gc-8.2.12/configure $BOOT_TRIPLET --prefix="$STAGE0" \
   --enable-threads=win32 --enable-win32-dllmain --enable-shared --enable-static \
   --with-libatomic-ops=none --disable-docs CFLAGS="-O2 -g"
make -j4
make install
