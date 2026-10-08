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
# Mono's win32 DllMain calls GC_DllMain: link a static win32-threads bdwgc
# (bootstrap/00-bdwgc.sh with GC_PREFIX=$DNB/gc-win32-$MSYSTEM GC_STATIC=1)
GCW=$DNB/gc-win32-$MSYSTEM
[ -f "$GCW/lib/libgc.a" ] || GC_PREFIX="$GCW" GC_STATIC=1 bash "$R/bootstrap/00-bdwgc.sh"
export CPPFLAGS="-I$GCW/include $CPPFLAGS" LDFLAGS="-L$GCW/lib $LDFLAGS"
export SOURCE_DATE_EPOCH=315532800
MK=(EXTERNAL_MCS="$STAGE0/bin/cscc" EXTERNAL_RUNTIME="$STAGE0/bin/ilrun -C 16384"
    SKIP_BASIC_PROFILE_CHECK=yes CFLAGS="$CF" V=1)
cd "$S"
# a later patch may touch configure.in: regenerate before (re)configuring
if [ configure.in -nt configure ]; then NOCONFIGURE=1 ./autogen.sh; rm -f Makefile; fi
[ -f Makefile ] || CFLAGS="$CF" ./configure $BOOT_TRIPLET --prefix="$PREFIX" --with-gc=boehm
if [ ! -f .disable-signing ]; then
  # Guix phase disable-signing (inherited from mono-1.9.1).  /usr/bin/perl:
  # a native /ucrt64 perl would get its s/// argument path-converted by MSYS2.
  /usr/bin/perl -pi -e 's/^LIB_MCS_FLAGS =/LIB_MCS_FLAGS = \/delaysign+ /' mcs/class/IBM.Data.DB2/Makefile
  [ ! -f mcs/class/FirebirdSql.Data.Firebird/Assembly/AssemblyInfo.cs ] || /usr/bin/perl -pi -e 's/AssemblyDelaySign\(false\)/AssemblyDelaySign(true)/' mcs/class/FirebirdSql.Data.Firebird/Assembly/AssemblyInfo.cs
  touch .disable-signing
fi
if [ "${STEP:-}" = runtime ]; then make -C mono -j2 CFLAGS="$CF"; exit 0; fi
# Managed.Windows.Forms embeds resources/keyboards.resources.  Upstream makes
# it with "resgen keyboards.resx || cp keyboards.resources.prebuilt": the
# tarball's keyboards.resx is malformed (create-keyboards wrote it without
# truncating an older, longer file: trailing "...</root>4gAAAAs=</value>...")
# so resgen always fails and the build silently uses the prebuilt blob (which
# is also stale: one scan table has 49 instead of 50 entries).  We regenerate
# it from source, the way upstream intended (resources/create-keyboards.sh,
# GENERATE_RESOURCES=true makes KeyboardLayout public in SWF):
#   1. build everything once with an empty placeholder (pnet's C resgen);
#   2. rebuild net_1_1 SWF with GENERATE_RESOURCES=true, compile and run
#      create-keyboards.cs against it into a *fresh* keyboards.resx,
#      resgen it into keyboards.resources;
#   3. delete the SWF assemblies and let make rebuild them normally.
KB=mcs/class/Managed.Windows.Forms/resources
if [ ! -f $KB/keyboards.resources ] && [ ! -f $KB/.keyboards-placeholder ]; then
  : > $KB/empty.txt
  "$STAGE0/bin/resgen" $KB/empty.txt $KB/keyboards.resources
  touch $KB/.keyboards-placeholder
fi
make "${MK[@]}"
if [ -f $KB/.keyboards-placeholder ]; then
  W=$S/runtime/mono-wrapper; L=../../lib
  rm -f mcs/class/lib/net_1_1/System.Windows.Forms.dll
  make -C mcs/class/Managed.Windows.Forms PROFILE=net_1_1 GENERATE_RESOURCES=true "${MK[@]}"
  (cd $KB && rm -f keyboards.resx keyboards.resources create-keyboards.exe &&
   MONO_PATH="$L/net_1_1" $W $L/net_1_1/mcs.exe /nologo create-keyboards.cs /r:System.Windows.Forms.dll /out:create-keyboards.exe &&
   MONO_PATH="$L/net_1_1" $W ./create-keyboards.exe &&
   MONO_PATH="$L/net_1_1" $W $L/net_1_1_bootstrap/resgen.exe keyboards.resx keyboards.resources)
  rm -f $KB/.keyboards-placeholder $KB/empty.txt mcs/class/lib/*/System.Windows.Forms.dll
  make "${MK[@]}"
fi
make install "${MK[@]}"
