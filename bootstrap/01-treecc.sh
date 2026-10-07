#!/bin/bash
# Stage 0a: treecc 0.3.10 (DotGNU tree compiler-compiler, plain C).
# Source: https://download.savannah.gnu.org/releases/dotgnu-pnet/treecc-0.3.10.tar.gz
# sha256 5e9d20a6938e0c6fedfed0cabc7e9e984024e4881b748d076e8c75f1aeb6efe7 (= Guix hash)
set -euo pipefail
. "$(dirname "$0")/env.sh"; . "$(dirname "$0")/lib.sh"
R=$(cd "$(dirname "$0")/.." && pwd)
TAR=$DNB/src/treecc-0.3.10.tar.gz
echo "5e9d20a6938e0c6fedfed0cabc7e9e984024e4881b748d076e8c75f1aeb6efe7  $TAR" | sha256sum -c -
[ -f "$STAGE0/bin/treecc.exe" ] && { log "treecc already installed"; exit 0; }
W=$DNB/build/treecc; S=$W/treecc-0.3.10
if [ ! -d "$S" ]; then
  mkdir -p "$W"; tar -C "$W" -xzf "$TAR"
  apply_patches "$S" "$R/patches/treecc"
  (cd "$S" && rm -f configure && autoreconf -fi)
fi
cd "$S"
[ -f Makefile ] || ./configure $BOOT_TRIPLET --prefix="$STAGE0" CFLAGS="$BOOT_CFLAGS"
make -j4
make install
