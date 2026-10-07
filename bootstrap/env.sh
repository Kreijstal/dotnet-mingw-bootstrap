# Common environment for the dotnet-bootstrap stages on MSYS2 (win11).
# Source this; do not execute.
#
# DNB      build root (sources, build trees, logs)
# STAGE0   install prefix of the C-only stage (treecc, pnet, pnetlib)
#
# The poison/ directory sits after our stage prefixes but before $MINGW_PREFIX/bin: win11 has the binary-seeded
# MSYS2 mingw-w64-ucrt-x86_64-mono package installed (/ucrt64/bin/mono.exe,
# mcs, csc, xbuild ...).  Its mcs was built from monolite blobs, so nothing in
# the bootstrap may ever pick it up.  The poison wrappers make any accidental
# use fail loudly instead.
export DNB="${DNB:-$HOME/dotnetboot}"
export MSYSTEM="${MSYSTEM:-UCRT64}"
# per-environment suffix for prefixes/build dirs (UCRT64 = primary, no suffix)
if [ "$MSYSTEM" = UCRT64 ]; then export BOOT_SFX=""; else export BOOT_SFX="-$MSYSTEM"; fi
export STAGE0="${STAGE0:-$DNB/stage0$BOOT_SFX}"
case "$MSYSTEM" in
  UCRT64)  _mp=/ucrt64 ;;
  MINGW32) _mp=/mingw32 ;;
  CLANG64) _mp=/clang64 ;;
  *) echo "unsupported MSYSTEM=$MSYSTEM" >&2; return 1 ;;
esac
export MINGW_PREFIX="$_mp"
case "$MSYSTEM" in MINGW32) export MINGW_CHOST=i686-w64-mingw32 ;; *) export MINGW_CHOST=x86_64-w64-mingw32 ;; esac
# 2007-era config.guess cannot identify MINGW64_NT; always pass the triplet.
export BOOT_TRIPLET="--build=$MINGW_CHOST --host=$MINGW_CHOST"
_here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# Our own stage prefixes come first, then poison/, then the MSYS2 toolchain.
export PATH="${BOOT_EXTRA_PATH:+$BOOT_EXTRA_PATH:}$STAGE0/bin:$_here/poison:$_mp/bin:/usr/bin:/usr/bin/core_perl"
export PKG_CONFIG_PATH="$STAGE0/lib/pkgconfig:$_mp/lib/pkgconfig:$_mp/share/pkgconfig"
export ACLOCAL_PATH="$_mp/share/aclocal:/usr/share/aclocal"
# GCC 16 defaults to -std=gnu23 (bool/true/false keywords, () means (void)).
# All of this 2007-2011 code is C89/C99; pin gnu17 like Guix' GCC 14 era.
export BOOT_CFLAGS="-O2 -g -std=gnu17 -Wno-error=implicit-function-declaration -Wno-error=incompatible-pointer-types -Wno-error=int-conversion -Wno-error=implicit-int -Wno-error=return-mismatch"
unset _mp _here
# The MSYS2 runtime rewrites arguments that look like POSIX paths when it
# spawns a native .exe, so csc-style options such as /nologo, /optimize or
# /debug+ reach cscc.exe/mono.exe as "E:/msys64/nologo" (seen building
# mono-1.2.6's mcs).  Exclude the option prefixes used by mono's mcs/build
# makefiles and gacutil (/i /f /l /root /package); real paths (/home,
# /ucrt64, /mingw32, /usr, /tmp) are still converted.
export MSYS2_ARG_CONV_EXCL="/nologo;/optimize;/debug;/unsafe;/nowarn:;/warn:;/warnaserror;/r:;/reference:;/d:;/define:;/out:;/target:;/t:;/codepage:;/resource:;/res:;/linkres;/noconfig;/nostdlib;/lib:;/exclude:;/xml:;/include:;/gacdir;/package;/root;/output:;/labels;/main:;/doc:;/delaysign;/keyfile:;/keycontainer:;/checked;/langversion:;/platform:;/pkg:;/win32res:;/win32icon:;/addmodule:;/runtimemetadataversion:;/recurse:;/fullpaths;/clscheck;/sdk:;/compile;/test;/i;/f;/l;/check_refs"
# libraries built into the stage-0 prefix (e.g. a private bdwgc) come first
export CPPFLAGS="-I$STAGE0/include${CPPFLAGS:+ $CPPFLAGS}"
export LDFLAGS="-L$STAGE0/lib${LDFLAGS:+ $LDFLAGS}"
