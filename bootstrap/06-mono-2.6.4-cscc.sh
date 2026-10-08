#!/bin/bash
# cscc/ilrun shortcut: mono 2.6.4 built directly by pnet (EXTERNAL_MCS=cscc,
# EXTERNAL_RUNTIME=ilrun), skipping mono 1.2.6 -> 2.4.2.3 of the Guix chain.
# 2.6.x is the last Mono whose "basic" profile is C# 1.0 / ONLY_1_1 (from 2.8
# on it is NET_2_0 and needs a generics-capable bootstrap compiler).
# Source: https://github.com/mono/mono/archive/refs/tags/mono-2.6.4.tar.gz
#   sha256 281d9edef97eefd0075da38045cb1f951bc66d9b399f14dee6eabdcafdd80576
#   (git tree b27d228821abcdfdda382553ff67926d82f8a762 = tag mono-2.6.4, as in Guix)
set -euo pipefail
. "$(dirname "$0")/env.sh"; . "$(dirname "$0")/lib.sh"
R=$(cd "$(dirname "$0")/.." && pwd)
TAR=$DNB/src/mono-2.6.4.tar.gz
echo "281d9edef97eefd0075da38045cb1f951bc66d9b399f14dee6eabdcafdd80576  $TAR" | sha256sum -c -
PREFIX=$DNB/stage-m264-$MSYSTEM
W=$DNB/build/mono-2.6.4-$MSYSTEM; S=$W/mono-mono-2.6.4
if [ ! -f "$W/.prepared" ]; then
  rm -rf "${W:?}"; mkdir -p "$W"; tar -C "$W" -xzf "$TAR"
  apply_patches "$S" "$R/patches/mono-2.6.4"
  (cd "$S" && bash "$R/bootstrap/prep-mono.sh")
  [ -z "$(scan_binaries "$S")" ] || { scan_binaries "$S"; die "binaries left in mono tree"; }
  (cd "$S" && NOCONFIGURE=1 ./autogen.sh)
  touch "$W/.prepared"
fi
apply_patches "$S" "$R/patches/mono-2.6.4"
CF="$BOOT_CFLAGS -DARG_MAX=500"
export SOURCE_DATE_EPOCH=315532800
MK=(EXTERNAL_MCS="$STAGE0/bin/cscc" EXTERNAL_RUNTIME="$STAGE0/bin/ilrun -C 16384"
    SKIP_BASIC_PROFILE_CHECK=yes CFLAGS="$CF" V=1)
cd "$S"
if [ ! -f Makefile ]; then
  CFLAGS="$CF" ./configure $BOOT_TRIPLET --prefix="$PREFIX" --with-gc=boehm
  # Guix phase disable-signing (inherited from mono-1.9.1)
  perl -pi -e 's/^LIB_MCS_FLAGS =/LIB_MCS_FLAGS = \/delaysign+ /' mcs/class/IBM.Data.DB2/Makefile
  perl -pi -e 's/AssemblyDelaySign\(false\)/AssemblyDelaySign(true)/' mcs/class/FirebirdSql.Data.Firebird/Assembly/AssemblyInfo.cs || true
fi
if [ "${STEP:-}" = runtime ]; then make -C mono -j4 CFLAGS="$CF"; exit 0; fi
make "${MK[@]}"
make install "${MK[@]}"
