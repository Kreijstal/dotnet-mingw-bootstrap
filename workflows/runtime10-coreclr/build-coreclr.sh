#!/usr/bin/env bash
set -euo pipefail
root=$(cd "${1:?usage: build-coreclr.sh PREPARED_ROOT [JOBS]}" && pwd)
cdac="${CDAC_BOOTSTRAP_DIR:-$root/cdac-bootstrap}"
test -f "$cdac/tool/bin/Release/net8.0/cdac-build-tool.dll"
cmake -G Ninja -S "$root/r/src/coreclr" -B "$root/coreclr-build" \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_RC_COMPILER=llvm-windres -DCMAKE_ASM_MASM_COMPILER=uasm \
  -DCMAKE_INSTALL_PREFIX="$root/coreclr" \
  -DCLR_CMAKE_HOST_ARCH=x64 -DCLR_CMAKE_TARGET_ARCH=x64 -DCLR_CMAKE_TARGET_OS=windows \
  -DCLI_CMAKE_FALLBACK_OS=win -DCLI_CMAKE_PKG_RID=win-x64 \
  -DCLI_CMAKE_COMMIT_HASH=081d220c0a773ffb7c6bea6b48727833576a65ef \
  -DCLI_CMAKE_RESOURCE_DIR="$root/host-resources" \
  -DCLR_CMAKE_PGO_INSTRUMENT=0 -DCLR_CMAKE_PGO_OPTIMIZE=0 \
  -DCLR_CMAKE_KEEP_NATIVE_SYMBOLS=ON \
  -DCDAC_BUILD_TOOL_BINARY_PATH="$(cygpath -am "$cdac/tool/bin/Release/net8.0/cdac-build-tool.dll")" \
  -DCLR_DOTNET_HOST_PATH="$(cygpath -am /clang64/lib/dotnet/dotnet.exe)" \
  -DCLR_DOTNET_RID=win-x64 \
  -DPython_EXECUTABLE="$(cygpath -am /clang64/bin/python.exe)" \
  -DPython3_EXECUTABLE="$(cygpath -am /clang64/bin/python.exe)" \
  > "$root/coreclr-configure.log" 2>&1
ninja -C "$root/coreclr-build" -j"${2:-2}" coreclr clrjit mscorrc clretwrc \
  > "$root/coreclr-build.log" 2>&1
printf '%s\n' 'CoreCLR10 native targets compiled; managed execution and full compatibility remain required'
