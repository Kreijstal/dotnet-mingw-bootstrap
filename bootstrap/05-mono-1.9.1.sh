#!/bin/bash
# Stage 2: mono 1.9.1 (git tag mono-1.9.1.1), compiled by stage-1 mono-1.2.6,
# as Guix' mono-1.9.1 (gnu/packages/dotnet.scm, commit 893bd4a8ef39).
# Source: https://github.com/mono/mono/archive/refs/tags/mono-1.9.1.1.tar.gz
#   sha256 7cc4d6a21240827446a135c8efdfd3adcdef82457467aacdcb0d39d515de1461
#   (git tree b6fe3b24959762bdac992d09ecbf0c7f845c83e5 = tree of tag mono-1.9.1.1,
#    the same commit Guix fetches from gitlab.winehq.org/mono/mono.git)
set -euo pipefail
. "$(dirname "$0")/env.sh"
R=$(cd "$(dirname "$0")/.." && pwd)
PREV=$DNB/stage1-$MSYSTEM                 # mono-1.2.6 = the bootstrap compiler
[ -x "$PREV/bin/mono.exe" ] || { echo "stage 1 (mono-1.2.6) missing in $PREV" >&2; exit 1; }
export PATH="$PREV/bin:$PATH"             # ahead of poison/, like a Guix native-input
. "$(dirname "$0")/lib.sh"
TAR=$DNB/src/mono-1.9.1.1.tar.gz
echo "7cc4d6a21240827446a135c8efdfd3adcdef82457467aacdcb0d39d515de1461  $TAR" | sha256sum -c -
PREFIX=$DNB/stage2-$MSYSTEM
W=$DNB/build/mono-1.9.1-$MSYSTEM; S=$W/mono-mono-1.9.1.1
if [ ! -f "$W/.prepared" ]; then
  rm -rf "${W:?}"; mkdir -p "$W"; tar -C "$W" -xzf "$TAR"
  apply_patches "$S" "$R/patches/mono-1.9.1"
  (cd "$S" && bash "$R/bootstrap/prep-mono.sh")
  [ -z "$(scan_binaries "$S")" ] || { scan_binaries "$S"; die "binaries left in mono tree"; }
  (cd "$S" && NOCONFIGURE=1 ./autogen.sh)
  touch "$W/.prepared"
fi
apply_patches "$S" "$R/patches/mono-1.9.1"
CF="$BOOT_CFLAGS -DARG_MAX=500"
export SOURCE_DATE_EPOCH=315532800
# Guix phase set-create-image-version: pnet emitted v2.0 images and mono
# propagates the runtime version of whatever it runs; force 1.1 for 1.x builds
export MONO_CREATE_IMAGE_VERSION=v1.1.4322
cd "$S"
if [ ! -f Makefile ]; then
  CFLAGS="$CF" ./configure $BOOT_TRIPLET --prefix="$PREFIX" --with-gc=boehm
  # Guix phase disable-signing (no keyfile; non-reproducible)
  perl -pi -e 's/^LIB_MCS_FLAGS =/LIB_MCS_FLAGS = \/delaysign+ /' mcs/class/IBM.Data.DB2/Makefile
  perl -pi -e 's/AssemblyDelaySign\(false\)/AssemblyDelaySign(true)/' mcs/class/FirebirdSql.Data.Firebird/Assembly/AssemblyInfo.cs
fi
make CFLAGS="$CF" NO_SIGN_ASSEMBLY=yes V=1
find . -name '*.mdb' -delete          # Guix phase delete-mdb (random GUIDs)
make install CFLAGS="$CF" NO_SIGN_ASSEMBLY=yes V=1
