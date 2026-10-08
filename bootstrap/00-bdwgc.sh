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
# GC_PREFIX/GC_STATIC: on UCRT64 (where pnet uses MSYS2's pthreads gc) Mono
# >= 2.x needs GC_DllMain, so a static win32-threads copy goes into its own
# prefix: no second libgc-1.dll next to MSYS2's.
GC_PREFIX=${GC_PREFIX:-$STAGE0}
if [ -n "${GC_STATIC:-}" ]; then SH="--disable-shared"; else SH="--enable-shared"; fi
[ -f "$GC_PREFIX/lib/libgc.a" ] && { log "bdwgc already installed in $GC_PREFIX"; exit 0; }
W=$DNB/build/bdwgc$BOOT_SFX${GC_STATIC:+-static}; mkdir -p "$W"; [ -d "$W/gc-8.2.12" ] || tar -C "$W" -xzf "$TAR"
mkdir -p "$W/build"; cd "$W/build"
[ -f Makefile ] || ../gc-8.2.12/configure $BOOT_TRIPLET --prefix="$GC_PREFIX" \
   --enable-threads=win32 --enable-win32-dllmain ${GC_STATIC:+--disable-thread-local-alloc} $SH --enable-static \
   --enable-large-config --with-libatomic-ops=none --disable-docs CFLAGS="-O2 -g ${GC_STATIC:+-DGC_INSIDE_DLL}"
# --enable-large-config as in MSYS2's mingw-w64-gc: without it the heap-section
# table fills up (mdoc assemble in mono-2.6.4: "Too many heap sections" abort)
# (static copy: -DGC_INSIDE_DLL makes win32_threads.c name its DllMain
#  GC_DllMain, which Mono's own DllMain in libmono calls)
# (DllMain thread discovery is compiled only without thread-local alloc)
make -j4
make install
# libtool refuses ("cannot link to static lib archive libgc.la") to link the
# static copy into Mono's libmono DLL and silently drops -lgc; without the .la
# files it passes -lgc through to the linker.
if [ -n "${GC_STATIC:-}" ]; then rm -f "$GC_PREFIX"/lib/libgc.la "$GC_PREFIX"/lib/libcord.la; fi
