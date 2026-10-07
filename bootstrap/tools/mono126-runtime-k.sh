#!/bin/bash
# Incremental helper: apply any new patches to an existing mono-1.2.6 build
# tree and rebuild only the C runtime (mono/) with make -k -j4, to collect all
# compile errors of a host in one pass.  Usage: MSYSTEM=... bash mono126-runtime-k.sh
. "$(dirname "$0")/../env.sh"; . "$(dirname "$0")/../lib.sh"
R=$(cd "$(dirname "$0")/../.." && pwd)
S=$DNB/build/mono-1.2.6-$MSYSTEM/mono-1.2.6
apply_patches "$S" "$R/patches/mono-1.2.6"
cd "$S" && make -k -j4 -C mono CFLAGS="$BOOT_CFLAGS -DARG_MAX=500"
echo "rc=$?"
