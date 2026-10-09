#!/bin/bash
# Build CoreCLR (dotnet/runtime src/coreclr) for win-x64 with the MSYS2 CLANG64
# toolchain (clang/lld/libc++, x86_64-w64-mingw32):
#   coreclr.dll  - the runtime (VM, GC, type loader, interop, debugger RC thread)
#   clrjit.dll   - RyuJIT
#   mscorrc.dll, clretwrc.dll - the native string resources
# from the same patched tree as corehost.sh/mono-runtime.sh (patches 0001..).
# The MASM helpers are assembled with UASM (mingw-w64-clang-x86_64-uasm).
# MSVC's /EHa C++/SEH semantics come from minipal/Windows/mingw_eha.cpp (patch 0012).
#
# Upstream drives this CMake project from build-runtime.cmd (Arcade/MSBuild);
# this script passes the options build-runtime.cmd passes for windows.x64.Release.
#
# usage: coreclr.sh [step...]   steps: prep configure build install test
# Run with MSYSTEM=CLANG64, after corehost.sh (same tree, version headers).
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
. "$here/../env-m612.sh"
[ "$MSYSTEM" = CLANG64 ] || die "coreclr.sh wants MSYSTEM=CLANG64 (got $MSYSTEM)"
steps=${*:-prep configure build install test}
RT_VER=${RT_VER:-8.0.0}
SRC=$M612/src/runtime-$RT_VER-native
B=$M612/build/coreclr-$RT_VER
O=$M612/coreclr-$RT_VER                # install prefix
JOBS=${JOBS:-4}
CONFIG=${CONFIG:-Release}

step_prep() {
  # same tree as corehost.sh (it fetches, extracts and patches it)
  bash "$here/corehost.sh" prep
  [ -f "$SRC/artifacts/obj/_version.h" ] || die "no version headers; run corehost.sh configure first"
  command -v uasm >/dev/null || die "uasm not found (pacman -S mingw-w64-clang-x86_64-uasm)"
}

step_configure() {
  log "configure coreclr ($MSYSTEM, $CONFIG)"
  cmake -G Ninja -S "$SRC/src/coreclr" -B "$B/cmake" \
    -DCMAKE_BUILD_TYPE=$CONFIG -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
    -DCMAKE_RC_COMPILER=llvm-windres -DCMAKE_ASM_MASM_COMPILER=uasm \
    -DCMAKE_INSTALL_PREFIX="$O" \
    -DCLR_CMAKE_HOST_ARCH=x64 -DCLR_CMAKE_TARGET_ARCH=x64 -DCLR_CMAKE_TARGET_OS=windows \
    -DCLI_CMAKE_FALLBACK_OS=win10 -DCLR_CMAKE_PGO_INSTRUMENT=0 -DCLR_CMAKE_PGO_OPTIMIZE=0 \
    -DCLR_CMAKE_KEEP_NATIVE_SYMBOLS=ON \
    -DPython3_EXECUTABLE="$(cygpath -m "$(command -v python3)")"
}

# coreclr pulls in clrjit, the GC, the binder, the metadata reader/emitter and
# the in-process debugger; mscorrc/clretwrc are resource-only DLLs.
step_build() { log "build coreclr"; ninja -C "$B/cmake" -j"$JOBS" coreclr clrjit mscorrc clretwrc; }

step_install() {
  log "install -> $O"
  rm -rf "${O:?}"; mkdir -p "$O"
  local f
  for f in dlls/mscoree/coreclr/coreclr.dll jit/clrjit.dll dlls/mscorrc/mscorrc.dll dlls/clretwrc/clretwrc.dll; do
    cp "$B/cmake/$f" "$O/"
  done
  # keep the -g copies in the build tree; ship stripped DLLs
  llvm-strip --strip-debug "$O"/*.dll
  ls -l "$O"
}

step_test() {
  log "coreclr.dll imports (system DLLs only)"
  llvm-readobj --coff-imports "$O/coreclr.dll" | grep 'Name:' | sort -u
  log "exports"
  llvm-readobj --coff-exports "$O/coreclr.dll" | grep 'Name:' | head -40
  log "EHa shim self-test (tests/coreclr/eha.cpp)"
  local T=$M612/test/coreclr-$RT_VER
  mkdir -p "$T"
  clang++ -O2 -fms-extensions -frtti -c "$SRC/src/coreclr/minipal/Windows/mingw_eha.cpp" -o "$T/eha.o"
  clang++ -O2 -fms-extensions -fno-rtti "$here/tests/coreclr/eha.cpp" "$T/eha.o" -o "$T/eha.exe" -static \
    -Wl,--wrap=__gxx_personality_seh0 -Wl,--wrap=_Unwind_Resume -Wl,--wrap=__cxa_rethrow -Wl,--wrap=__C_specific_handler
  "$T/eha.exe"
}

for s in $steps; do "step_$s"; done
