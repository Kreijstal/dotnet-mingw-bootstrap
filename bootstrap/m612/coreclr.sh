#!/bin/bash
# Build CoreCLR (dotnet/runtime src/coreclr) for win-x64 with the MSYS2 CLANG64
# toolchain (clang/lld/libc++, x86_64-w64-mingw32) or the UCRT64 one
# (GCC/binutils/libstdc++; __try/__except from ReactOS's gcc_plugin_seh):
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
# Run with MSYSTEM=CLANG64 or UCRT64, after corehost.sh (same tree, version
# headers).  UCRT64 needs REACTOS_SRC: a ReactOS checkout whose
# sdk/tools/gcc_plugin_seh registers __C_specific_handler with @unwind.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
. "$here/../env-m612.sh"
steps=${*:-prep configure build install test}
RT_VER=${RT_VER:-8.0.0}
SRC=$M612/src/runtime-$RT_VER-native
B=$M612/build/coreclr-$RT_VER$NATIVE_SUFFIX
O=$M612/coreclr-$RT_VER$NATIVE_SUFFIX                # install prefix
JOBS=${JOBS:-4}
CONFIG=${CONFIG:-Release}

step_prep() {
  # same tree as corehost.sh (it fetches, extracts and patches it)
  bash "$here/corehost.sh" prep
  [ -f "$SRC/artifacts/obj/_version.h" ] || die "no version headers; run corehost.sh configure first"
  command -v uasm >/dev/null || die "uasm not found (pacman -S $NPKG-uasm)"
  if [ "$MSYSTEM" = UCRT64 ]; then
    [ -f "${REACTOS_SRC:?set REACTOS_SRC for the GCC SEH plugin}/sdk/tools/gcc_plugin_seh/main.cpp" ] || die "no gcc_plugin_seh in $REACTOS_SRC"
  fi
}

# The GCC SEH plugin (#pragma REACTOS seh, used by PSEH's __try/__except/__finally).
gcc_seh_plugin() {
  local p; p=$(g++ -print-file-name=plugin)
  [ -f "$p/cc1plus.exe.a" ] || die "no GCC plugin SDK (pacman -S $NPKG-gcc-plugin)"
  mkdir -p "$B"
  if ! [ "$B/gcc_seh.dll" -nt "$REACTOS_SRC/sdk/tools/gcc_plugin_seh/main.cpp" ]; then
    log "build the GCC SEH plugin"
    g++ -shared -O2 -fno-rtti -I"$p/include" "$REACTOS_SRC/sdk/tools/gcc_plugin_seh/main.cpp" "$p/cc1plus.exe.a" -o "$B/gcc_seh.dll"
  fi
}

step_configure() {
  log "configure coreclr ($MSYSTEM, $CONFIG)"
  local seh=()
  if [ "$MSYSTEM" = UCRT64 ]; then
    gcc_seh_plugin
    seh=(-DCLR_MINGW_GCC_SEH_PLUGIN="$(cygpath -m "$B/gcc_seh.dll")"
         -DCLR_MINGW_PSEH_INCLUDE="$(cygpath -m "$REACTOS_SRC/sdk/lib/pseh/include")")
  fi
  cmake -G Ninja -S "$SRC/src/coreclr" -B "$B/cmake" \
    -DCMAKE_BUILD_TYPE=$CONFIG -DCMAKE_C_COMPILER=$NCC -DCMAKE_CXX_COMPILER=$NCXX \
    -DCMAKE_RC_COMPILER=$NRC -DCMAKE_ASM_MASM_COMPILER=uasm "${seh[@]}" \
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
  $NSTRIP --strip-debug "$O"/*.dll
  ls -l "$O"
}

step_test() {
  local T=$M612/test/coreclr-$RT_VER$NATIVE_SUFFIX
  mkdir -p "$T"
  log "coreclr.dll imports (system DLLs only)"
  objdump -p "$O/coreclr.dll" | grep 'DLL Name:' | sort -u
  log "exports"
  objdump -p "$O/coreclr.dll" | sed -n '/\[Ordinal\/Name Pointer\] Table/,/^$/p' | head -40
  if [ "$MSYSTEM" = CLANG64 ]; then
    log "EHa shim self-test (tests/coreclr/eha.cpp)"
    clang++ -O2 -fms-extensions -frtti -c "$SRC/src/coreclr/minipal/Windows/mingw_eha.cpp" -o "$T/eha.o"
    clang++ -O2 -fms-extensions -fno-rtti "$here/tests/coreclr/eha.cpp" "$T/eha.o" -o "$T/eha.exe" -static \
      -Wl,--wrap=__gxx_personality_seh0 -Wl,--wrap=_Unwind_Resume -Wl,--wrap=__cxa_rethrow -Wl,--wrap=__C_specific_handler
    "$T/eha.exe"
  fi
  # The same shim with UCRT64 GCC/libstdc++; __try comes from ReactOS's gcc_plugin_seh
  # (needs mingw-w64-ucrt-x86_64-gcc-plugin; REACTOS_SRC: a checkout with its @unwind fix).
  if [ -z "${REACTOS_SRC:-}" ]; then
    log "EHa shim self-test with GCC skipped (set REACTOS_SRC)"
    return
  fi
  log "EHa shim self-test with GCC (tests/coreclr/eha.cpp)"
  ( PATH=/ucrt64/bin:$PATH
    P=$(g++ -print-file-name=plugin)
    g++ -shared -O2 -fno-rtti -I"$P/include" "$REACTOS_SRC/sdk/tools/gcc_plugin_seh/main.cpp" "$P/cc1plus.exe.a" \
      -o "$T/gxx_seh.dll"
    g++ -O2 -frtti -c "$SRC/src/coreclr/minipal/Windows/mingw_eha.cpp" -o "$T/eha-gcc.o"
    g++ -O2 -fno-rtti -fnon-call-exceptions -fplugin="$T/gxx_seh.dll" -I"$REACTOS_SRC/sdk/lib/pseh/include" \
      "$here/tests/coreclr/eha.cpp" "$T/eha-gcc.o" -o "$T/eha-gcc.exe" -static \
      -Wl,--wrap=__gxx_personality_seh0 -Wl,--wrap=_Unwind_Resume -Wl,--wrap=__cxa_rethrow -Wl,--wrap=__C_specific_handler
    "$T/eha-gcc.exe" )
}

for s in $steps; do "step_$s"; done
