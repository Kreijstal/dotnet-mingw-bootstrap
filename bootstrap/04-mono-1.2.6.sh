#!/bin/bash
# Stage 1: mono 1.2.6, compiled by pnet (EXTERNAL_MCS=cscc, EXTERNAL_RUNTIME=ilrun)
# as in Guix' mono-1.2.6 (gnu/packages/dotnet.scm, commit 7d96fa640a6b).
# Source: http://download.mono-project.com/sources/mono/mono-1.2.6.tar.bz2
# sha256 24cc4f396cac3053c7ba3fe68bc1b80359d75dc4f54a85f39a73cabc3d3f560f (= Guix hash)
set -euo pipefail
. "$(dirname "$0")/env.sh"; . "$(dirname "$0")/lib.sh"
R=$(cd "$(dirname "$0")/.." && pwd)
TAR=$DNB/src/mono-1.2.6.tar.bz2
echo "24cc4f396cac3053c7ba3fe68bc1b80359d75dc4f54a85f39a73cabc3d3f560f  $TAR" | sha256sum -c -
PREFIX=${MONO126_PREFIX:-$DNB/stage1-$MSYSTEM}
W=$DNB/build/mono-1.2.6-$MSYSTEM; S=$W/mono-1.2.6
if [ ! -f "$W/.prepared" ]; then
  rm -rf "${W:?}"; mkdir -p "$W"; tar -C "$W" -xjf "$TAR"
  apply_patches "$S" "$R/patches/mono-1.2.6"           # Guix applies patches before the snippet
  (cd "$S" && bash "$R/bootstrap/prep-mono.sh")        # deletes monolite/*.dll|exe etc.
  (cd "$S/mcs/class/System/System.Text.RegularExpressions" && mv -f BaseMachine.cs-2 BaseMachine.cs)
  # Guix phase fix-includes: "Upstream forgot to #include that."
  perl -pi -e 's|#include <mono/metadata/image.h>|#include <mono/metadata/image.h>\n#include <mono/metadata/assembly.h>|' "$S/mono/metadata/security.c"
  [ -z "$(scan_binaries "$S")" ] || die "binaries left in mono tree"
  (cd "$S" && autoreconf -fi)   # release tarball: no autogen.sh
  touch "$W/.prepared"
fi
apply_patches "$S" "$R/patches/mono-1.2.6"
# Guix cflags (+ -std=gnu17 for GCC 16); ARG_MAX is not defined on mingw either
CF="$BOOT_CFLAGS -DARG_MAX=500"
export SOURCE_DATE_EPOCH=315532800   # ZIP "DOS time" starts 1980 (Guix)
cd "$S"
[ -f Makefile ] || ./configure $BOOT_TRIPLET --prefix="$PREFIX" --with-gc=boehm CFLAGS="$CF"
# STEP=runtime: only build the C runtime (mono/), e.g. to probe a new host
if [ "${STEP:-}" = runtime ]; then make -C mono -j2 CFLAGS="$CF" V=1; exit 0; fi
# not parallel: "build fails nondeterministically without this" (Guix)
make EXTERNAL_MCS="$STAGE0/bin/cscc" EXTERNAL_RUNTIME="$STAGE0/bin/ilrun" CFLAGS="$CF" V=1
make install EXTERNAL_MCS="$STAGE0/bin/cscc" EXTERNAL_RUNTIME="$STAGE0/bin/ilrun" CFLAGS="$CF" V=1
