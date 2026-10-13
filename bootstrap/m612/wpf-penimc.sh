#!/bin/bash
# Source-built WPF 8 pen/stylus input DLL with trusted MSYS2 ReactOS ATL/widl.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
. "$here/../env-m612.sh"
version=8.0.0
source=${WPF_SOURCE:-$M612/src/wpf-$version}
build=${WPF_BUILD:-$M612/build/wpf-$version}
port=$here/wpf-native/penimc
root=$build/penimc-source/src/Microsoft.DotNet.Wpf/src
steps=${*:-build check}
mkdir -p "$build/lib"

prepare() {
  if [ ! -d "$root/PenImc" ]; then
    mkdir -p "$root"
    cp -r "$source/src/Microsoft.DotNet.Wpf/src/PenImc" "$source/src/Microsoft.DotNet.Wpf/src/Shared" "$root/"
  fi
  local file
  cmp -s "$port/CMakeLists.txt" "$root/PenImc/CMakeLists.txt" || cp "$port/CMakeLists.txt" "$root/PenImc/"
  mkdir -p "$root/PenImc/mingw"
  for file in NativeVersion.rc wpf_mingw_compat.h tpcshrd.h penimc_uuids.h; do
    cmp -s "$port/$file" "$root/PenImc/mingw/$file" || cp "$port/$file" "$root/PenImc/mingw/"
  done
}

step_build() {
  prepare
  CC=/clang64/bin/clang.exe CXX=/clang64/bin/clang++.exe \
    /clang64/bin/cmake.exe -S "$root/PenImc" -B "$build/penimc-build" -GNinja \
      -DCMAKE_BUILD_TYPE=Release -DWPF_VERSION="$version"
  /clang64/bin/cmake.exe --build "$build/penimc-build" --parallel "${WPF_JOBS:-4}"
  cp "$build/penimc-build/PenImc_cor3.dll" "$build/lib/"
}

step_check() {
  /clang64/bin/clang.exe -O2 "$port/exporttest.c" -o "$build/penimc-exporttest.exe"
  "$build/penimc-exporttest.exe" "$build/lib/PenImc_cor3.dll" "$build/penimc-build/gen/PenImc.def"
  /clang64/bin/clang.exe -O2 -I "$build/penimc-build/gen" "$port/penimctest.c" \
    "$build/penimc-build/gen/penimc_i.c" -o "$build/penimc-test.exe" -lole32 -luuid
  "$build/penimc-test.exe" "$build/lib/PenImc_cor3.dll"
}

for step in $steps; do
  log "WPF $version PenImc: $step"
  "step_$step"
done
