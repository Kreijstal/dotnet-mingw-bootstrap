#!/bin/bash
# Stage 0c: DotGNU pnetlib git c3c12b8b0c65f5482d03d6a4865f7670e98baf4c
# (https://git.savannah.gnu.org/git/dotgnu-pnet/pnetlib.git, 2011-07-21),
# the C# class library compiled by pnet's cscc and run on ilrun.
set -euo pipefail
. "$(dirname "$0")/env.sh"; . "$(dirname "$0")/lib.sh"
R=$(cd "$(dirname "$0")/.." && pwd)
W=$DNB/build/pnetlib; S=$W/pnetlib
if [ ! -f "$W/.prepared" ]; then
  rm -rf "${W:?}"; mkdir -p "$W"; cp -a "$DNB/src/pnetlib" "$S"
  (cd "$S" && bash "$R/bootstrap/prep-pnetlib.sh")
  apply_patches "$S" "$R/patches/pnetlib"            # before autoreconf: 0001 edits configure.in
  [ -z "$(scan_binaries "$S")" ] || die "binaries in pnetlib tree"
  (cd "$S" && autoreconf -vif)
  touch "$W/.prepared"
fi
apply_patches "$S" "$R/patches/pnetlib"
cd "$S"
[ -f Makefile ] || ./configure $BOOT_TRIPLET --prefix="$STAGE0" --without-x CFLAGS="$BOOT_CFLAGS -Wno-pointer-to-int-cast"
make
make install LN_S="$R/bootstrap/tools/ln-s-rel"
