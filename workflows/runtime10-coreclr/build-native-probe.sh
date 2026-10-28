#!/usr/bin/env bash
set -euo pipefail
root=$(cd "${1:?usage: build-native-probe.sh PREPARED_ROOT NEW_OUTPUT}" && pwd)
out=$(realpath -m "$2")
here=$(cd "$(dirname "$0")" && pwd)
test ! -e "$out"
mkdir -p "$out"
clang++ -std=c++17 -O2 -fms-extensions -static -municode \
  -DHOST_AMD64 -DTARGET_AMD64 -DHOST_WINDOWS -DTARGET_WINDOWS \
  -I "$root/r/src/coreclr/inc/mingw" -I "$root/r/src/coreclr/inc" -I "$root/r/src/native" \
  -include "$root/r/src/coreclr/inc/mingw/msvc_compat.h" \
  "$here/inputs/NativeJitProbe.cpp" -o "$out/native-jit-probe.exe"
printf '%s\n' 'Native JIT probe compiled against original10.0.5 headers'
