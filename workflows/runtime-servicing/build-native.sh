#!/usr/bin/env bash
set -euo pipefail
root=$(cd "${1:?usage: build-native.sh PREPARED_ROOT [JOBS]}" && pwd)
export RUNTIME_FILE_VERSION=$(cat "$root/file-version.txt")
here="$root/bootstrap/dotnet-bootstrap-3cbf21078968e2290a54ab093392b4dfeb1c6f1f/bootstrap/m612"
export M612="$root/m" RT_VER=8.0.31 HOST_VER=8.0.31 PACK_VER=8.0.31 MSYSTEM=CLANG64
export SDKROOT="$root/layout" JOBS=${2:-2} DOTNET_CLI_TELEMETRY_OPTOUT=1
export DOTNET_CLI_HOME=$(cygpath -am "$root/cli-home") NUGET_PACKAGES=$(cygpath -am "$root/no-packages")
# Each output and install prefix belongs to this fresh prepared root.
bash "$here/corehost.sh" prep configure build install > "$root/corehost.log" 2>&1
bash "$here/coreclr.sh" prep configure build install test > "$root/coreclr.log" 2>&1
printf 'PASS isolated native host/CoreCLR8.0.31 build and EHa shim check; matching managed framework and full runtime validation remain open\n'
