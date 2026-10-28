#!/usr/bin/env bash
set -euo pipefail
root=$(cd "${1:?usage: build-host.sh PREPARED_ROOT [JOBS]}" && pwd)
cmake -G Ninja -S "$root/r/src/native/corehost" -B "$root/host-build" \
 -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
 -DCMAKE_RC_COMPILER=llvm-windres -DCMAKE_ASM_MASM_COMPILER=uasm \
 -DCMAKE_INSTALL_PREFIX="$root/host" \
 -DCLR_CMAKE_TARGET_ARCH=x64 -DCLR_CMAKE_HOST_ARCH=x64 -DCLR_CMAKE_TARGET_OS=windows \
 -DCLI_CMAKE_PKG_RID=win-x64 -DCLI_CMAKE_FALLBACK_OS=win \
 -DCLI_CMAKE_COMMIT_HASH=081d220c0a773ffb7c6bea6b48727833576a65ef \
 -DCLI_CMAKE_RESOURCE_DIR="$root/host-resources" -DCLR_CMAKE_KEEP_NATIVE_SYMBOLS=ON \
 > "$root/host-configure.log" 2>&1
ninja -C "$root/host-build" -j"${2:-2}" > "$root/host-build.log" 2>&1
cmake --install "$root/host-build" --prefix "$root/host" > "$root/host-install.log" 2>&1
echo 'PASS original10.0.5 native host targets compiled; host/component tests and full runtime/SDK10 remain required'
