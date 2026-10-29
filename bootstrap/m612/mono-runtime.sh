#!/bin/bash
# Build the Mono flavor of the .NET runtime (dotnet/runtime src/mono) for
# win-x64 with the MSYS2 CLANG64 toolchain (clang/lld, x86_64-w64-mingw32):
#   coreclr.dll  - the Mono VM under the name the host loads (MonoSharedLibName
#                  is "coreclr" for desktop targets, see src/mono/mono.proj)
#   libmonosgen-2.0.a, mono-sgen.exe (test driver), the embedding headers
# plus the native library the Windows libraries P/Invoke into that is not part
# of the runtime itself: System.IO.Compression.Native.dll (zlib/brotli).
# System.Globalization.Native is compiled into the runtime (icushim, Windows'
# own icu.dll), System.Native is Unix-only.
#
# Upstream drives the CMake project from src/mono/mono.proj (Arcade); this
# script passes the same options mono.proj uses for TargetsWindows.
#
# usage: mono-runtime.sh [step...]   steps: prep configure build install nativelibs test
#   (test runs ziptest, the System.IO.Compression.Native round trips, as well)
# Run with MSYSTEM=CLANG64, after corehost.sh (same patched tree, version headers);
# with MSYSTEM=UCRT64 (GCC) only prep, nativelibs and ziptest.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
. "$here/../env-m612.sh"
steps=${*:-prep configure build install nativelibs test}
case "$MSYSTEM" in
  CLANG64) ;;
  UCRT64)
    for s in $steps; do
      case $s in prep|nativelibs|ziptest) ;; *) die "mono-runtime.sh $s wants MSYSTEM=CLANG64 (UCRT64: prep nativelibs ziptest)" ;; esac
    done ;;
  *) die "mono-runtime.sh wants MSYSTEM=CLANG64 or UCRT64 (got $MSYSTEM)" ;;
esac
RT_VER=${RT_VER:-8.0.0}
PACK_VER=${PACK_VER:-8.0.31}           # framework version the SDK's apps ask for (see refpack.sh)
RID=win-x64
SRC=$M612/src/runtime-$RT_VER-native
P=$here/../../patches/m612/runtime-$RT_VER
B=$M612/build/mono-$RT_VER$NATIVE_SUFFIX
O=$M612/mono-$RT_VER$NATIVE_SUFFIX     # install prefix (bin/, lib/, include/)
SDKROOT=${SDKROOT:-$M612/dotnet-sdk-8.0.131}
JOBS=${JOBS:-4}
CONFIG=${CONFIG:-Release}

step_prep() {
  # same tree as corehost.sh (it fetches, extracts and patches it)
  bash "$here/corehost.sh" prep
  [ -f "$SRC/artifacts/obj/runtime_version.h" ] || die "no version headers; run corehost.sh configure first"
  # eng/native/version/copy_version_files.sh
  cp "$SRC/eng/native/version/NativeVersion.rc" "$SRC/artifacts/obj/"
}

# The CPPFLAGS of mono.proj's "Windows specific options", minus the MSVC-only ones.
MONO_CPPFLAGS="-DWIN32 -DWIN32_LEAN_AND_MEAN -DWIN64 -D_CRT_SECURE_NO_WARNINGS -D_CRT_NONSTDC_NO_DEPRECATE \
 -DWIN32_THREADS -DWINVER=0x0601 -D_WIN32_WINNT=0x0601 -D_WIN32_IE=0x0501 -D_UNICODE -DUNICODE \
 -DFD_SETSIZE=1024 -DNVALGRIND"
[ "$CONFIG" = Release ] && MONO_CPPFLAGS="$MONO_CPPFLAGS -DNDEBUG"

step_configure() {
  log "configure mono ($MSYSTEM, $CONFIG)"
  cmake -G Ninja -S "$SRC/src/mono" -B "$B/mono" \
    -DCMAKE_BUILD_TYPE=$CONFIG -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
    -DCMAKE_RC_COMPILER=llvm-windres \
    -DCMAKE_INSTALL_PREFIX="$O" -DCMAKE_INSTALL_LIBDIR=lib \
    -DCMAKE_C_FLAGS="$MONO_CPPFLAGS" -DCMAKE_CXX_FLAGS="$MONO_CPPFLAGS" \
    -DCLR_CMAKE_HOST_ARCH=x64 -DCLR_CMAKE_KEEP_NATIVE_SYMBOLS=true \
    -DGC_SUSPEND=hybrid -DMONO_LIB_NAME=monosgen-2.0 -DMONO_SHARED_LIB_NAME=coreclr \
    -DMONO_COMPONENTS_RID=windows-x64 -DSTATIC_COMPONENTS=1 \
    -DPython3_EXECUTABLE="$(cygpath -m "$(command -v python3)")"
}

step_build() { log "build mono"; ninja -C "$B/mono" -j"$JOBS"; }

step_install() {
  log "install mono -> $O"
  mkdir -p "$O"; find "${O:?}" -mindepth 1 -maxdepth 1 ! -name libs -exec rm -rf {} +   # libs/: step nativelibs
  cmake --install "$B/mono" --prefix "$O"
  # coreclr.dll goes to lib/ (install_with_stripped_symbols); the -g debug info
  # stays in the build tree copy
  llvm-strip --strip-debug "$O/lib/coreclr.dll" "$O/bin/mono-sgen.exe"
  find "$O" -maxdepth 2 -type f \( -name '*.dll' -o -name '*.exe' \) -exec ls -l {} +
}

# src/native/libs for Windows: only System.IO.Compression.Native is a separate
# DLL there (System.Globalization.Native is linked into the runtime).
step_nativelibs() {
  log "configure + build src/native/libs (System.IO.Compression.Native)"
  cmake -G Ninja -S "$SRC/src/native/libs" -B "$B/libs" \
    -DCMAKE_BUILD_TYPE=$CONFIG -DCMAKE_C_COMPILER=$NCC -DCMAKE_CXX_COMPILER=$NCXX \
    -DCMAKE_RC_COMPILER=$NRC -DCMAKE_INSTALL_PREFIX="$O/libs" \
    -DCLR_CMAKE_TARGET_ARCH=x64 -DCLR_CMAKE_HOST_ARCH=x64 -DCLR_CMAKE_TARGET_OS=windows \
    -DCLR_CMAKE_KEEP_NATIVE_SYMBOLS=ON -DCMAKE_STATIC_LIB_LINK=1
  ninja -C "$B/libs" -j"$JOBS"
  cmake --install "$B/libs" --prefix "$O/libs"
  find "$O/libs" -type f -name '*.dll' -exec ls -l {} +
}

step_ziptest() {
  local T=$M612/test/mono-$RT_VER$NATIVE_SUFFIX L=$SRC/src/native
  mkdir -p "$T"
  log "test 2: System.IO.Compression.Native.dll round trips (zlib-intel deflate/inflate, crc32, brotli)"
  $NCC -O1 -I"$L/libs/System.IO.Compression.Native" -I"$L/external/brotli/include" "$here/tests/mono/ziptest.c" \
    "$O/libs/System.IO.Compression.Native.dll.a" -o "$T/ziptest.exe"
  cp "$O/libs/System.IO.Compression.Native.dll" "$T/"
  "$T/ziptest.exe"
}

# The runtime without CoreLib: mono-sgen.exe and the host both get as far as
# looking for System.Private.CoreLib.dll.
step_test() {
  local T=$M612/test/mono-$RT_VER rc F fw
  rm -rf "${T:?}"; mkdir -p "$T"
  log "test 1: coreclr.dll imports (system DLLs only)"
  llvm-readobj --coff-imports "$O/lib/coreclr.dll" | grep 'Name:' | sort -u
  step_ziptest
  log "test 2b: mono-sgen --version"
  "$O/bin/mono-sgen.exe" --version || true
  log "test 3: dotnet.exe (source-built host) -> hostfxr -> hostpolicy -> coreclr.dll (Mono), no CoreLib yet"
  F=$T/fakeroot; fw=$F/shared/Microsoft.NETCore.App/$PACK_VER
  mkdir -p "$F/host/fxr/$RT_VER" "$fw"
  cp "$SDKROOT/dotnet.exe" "$F/"; cp "$SDKROOT/host/fxr/$RT_VER/hostfxr.dll" "$F/host/fxr/$RT_VER/"
  cp "$M612/corehost-$RT_VER/corehost/hostpolicy.dll" "$O/lib/coreclr.dll" "$fw/"
  printf '{"runtimeTarget":{"name":".NETCoreApp,Version=v8.0/win-x64"},"targets":{".NETCoreApp,Version=v8.0/win-x64":{"Microsoft.NETCore.App.Runtime.win-x64/%s":{"native":{"hostpolicy.dll":{},"coreclr.dll":{}}}}},"libraries":{"Microsoft.NETCore.App.Runtime.win-x64/%s":{"type":"package","serviceable":false,"sha512":""}}}\n' "$PACK_VER" "$PACK_VER" > "$fw/Microsoft.NETCore.App.deps.json"
  local app=$M612/test/corehost-$RT_VER/app/bin/Debug/net8.0/apphost-test.dll
  [ -f "$app" ] || die "no test app; run corehost.sh test first"
  rc=0; COREHOST_TRACE=1 COREHOST_TRACEFILE=$(cygpath -w "$T/trace.txt") MONO_LOG_LEVEL=debug MONO_LOG_MASK=asm \
    "$F/dotnet.exe" "$app" > "$T/out.txt" 2>&1 || rc=$?
  grep -E "CoreLib|Assertion" "$T/out.txt"; echo "rc=$rc"
  grep -E 'Loaded library|CoreCLR path|coreclr_initialize|coreclr' "$T/trace.txt" | head -20
}

for s in $steps; do "step_$s"; done
