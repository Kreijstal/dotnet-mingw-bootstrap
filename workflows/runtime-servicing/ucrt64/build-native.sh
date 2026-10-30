#!/usr/bin/env bash
# The native files of the serviced runtime with UCRT64 GCC/libstdc++:
# corehost (dotnet.exe, hostfxr, hostpolicy, apphost, comhost, nethost),
# CoreCLR (coreclr, clrjit, mscorrc, clretwrc) and System.IO.Compression.Native,
# from a root that ../prepare.py, ../prepare-version.py and
# ../prepare-file-version.py made.  BOOTSTRAP is a dotnet-bootstrap tree whose
# m612 scripts build with UCRT64; REACTOS_SRC has gcc_plugin_seh and PSEH
# (only the coreclr stage needs it).  STAGES selects a subset of
# "corehost coreclr compression"; SDKROOT may name the host layout.
set -euo pipefail
root=$(cd "${1:?usage: build-native.sh PREPARED_ROOT BOOTSTRAP_TREE REACTOS_SRC [JOBS]}" && pwd)
bs=$(cd "${2:?missing bootstrap tree}" && pwd)/bootstrap/m612
here=$(cd "$(dirname "$0")" && pwd)
src=$root/m/src/runtime-8.0.31-native
export RUNTIME_FILE_VERSION=$(cat "$root/file-version.txt")
export M612="$root/m" RT_VER=8.0.31 HOST_VER=8.0.31 PACK_VER=8.0.31 MSYSTEM=UCRT64 JOBS=${4:-2}
export SDKROOT=${SDKROOT:-$root/layout-ucrt64}
stages=" ${STAGES:-corehost coreclr compression} "
[[ $stages != *" coreclr "* ]] || export REACTOS_SRC=$(cd "${3:?missing ReactOS source}" && pwd)
[ -f "$src/.applied-patches" ] || { echo "not a prepared root: $root" >&2; exit 1; }
for p in "$here"/[0-9][0-9][0-9][0-9]-*.patch; do
  grep -qxF "ucrt64/$(basename "$p")" "$src/.applied-patches" && continue
  patch -d "$src" --fuzz=0 --no-backup-if-mismatch -p1 -s -i "$p"
  echo "ucrt64/$(basename "$p")" >> "$src/.applied-patches"
done
[[ $stages != *" corehost "* ]] || bash "$bs/corehost.sh" prep configure build install > "$root/corehost-ucrt64.log" 2>&1
[[ $stages != *" coreclr "* ]] || bash "$bs/coreclr.sh" prep configure build install test > "$root/coreclr-ucrt64.log" 2>&1
[[ $stages != *" compression "* ]] || bash "$bs/mono-runtime.sh" prep nativelibs ziptest > "$root/compression-ucrt64.log" 2>&1
printf 'PASS GCC native stages:%s(CoreCLR runs the EHa self-tests, compression its zlib/CRC/brotli round trips)\n' "$stages"
