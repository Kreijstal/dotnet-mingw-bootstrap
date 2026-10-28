#!/usr/bin/env bash
set -euo pipefail
root=$(cd "${1:?usage: build-compression.sh PREPARED_ROOT [JOBS]}" && pwd)
here="$root/bootstrap/dotnet-bootstrap-3cbf21078968e2290a54ab093392b4dfeb1c6f1f/bootstrap/m612"
export M612="$root/m" RT_VER=8.0.31 MSYSTEM=CLANG64 JOBS=${2:-2}
export PATH=/clang64/bin:/usr/bin:$PATH
bash "$here/mono-runtime.sh" prep nativelibs > "$root/compression.log" 2>&1
# The original shared helper's nativelibs step builds only Windows compression.
source="$root/m/src/runtime-8.0.31-native/src/native"
lib="$root/m/mono-8.0.31/libs"
mkdir "$root/compression-check"
/clang64/bin/clang -O1 -I"$source/libs/System.IO.Compression.Native" -I"$source/external/brotli/include" "$here/tests/mono/ziptest.c" "$lib/System.IO.Compression.Native.dll.a" -o "$root/compression-check/ziptest.exe"
cp "$lib/System.IO.Compression.Native.dll" "$root/compression-check/"
"$root/compression-check/ziptest.exe"
printf 'PASS source8.0.31 native compression zlib/CRC/brotli round trips\n'
