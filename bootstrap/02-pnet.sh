#!/bin/bash
# Stage 0b: DotGNU Portable.NET (pnet) git 3baf94734d8dc3fdabba68a8891e67a43ed6c4bd
# (https://git.savannah.gnu.org/git/dotgnu-pnet/pnet.git, 2011-06-15) -- the same
# commit Guix pins in pnet-git.  Provides cscc (C# compiler written in C),
# ilrun (CVM interpreter, C), ilasm, ilalink, resgen ... No managed binaries.
set -euo pipefail
. "$(dirname "$0")/env.sh"; . "$(dirname "$0")/lib.sh"
R=$(cd "$(dirname "$0")/.." && pwd)
W=$DNB/build/pnet; S=$W/pnet
if [ ! -f "$W/.prepared" ]; then
  rm -rf "${W:?}"; mkdir -p "$W"; cp -a "$DNB/src/pnet" "$S"
  (cd "$S" && bash "$R/bootstrap/prep-pnet.sh")
  apply_patches "$S" "$R/patches/pnet"
  [ -z "$(scan_binaries "$S")" ] || die "binaries in pnet tree"
  (cd "$S" && autoreconf -vif)
  touch "$W/.prepared"
fi
# idempotent: picks up patches added after the tree was prepared (incremental
# continuation of a failed build; patches touching autotools inputs need a
# manual autoreconf)
apply_patches "$S" "$R/patches/pnet"
cd "$S"
PNET_CFLAGS="$BOOT_CFLAGS -Wno-pointer-to-int-cast"
[ -f Makefile ] || ./configure $BOOT_TRIPLET --prefix="$STAGE0" CFLAGS="$PNET_CFLAGS"
make CFLAGS="$PNET_CFLAGS"          # Guix: serial is fine, parallel not needed
make install LN_S="$R/bootstrap/tools/ln-s-rel"   # see tools/ln-s-rel
