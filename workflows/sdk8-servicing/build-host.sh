#!/usr/bin/env bash
# Build the real servicing apphost/comhost/nethost from the pinned runtime source.
set -euo pipefail
workflow=$(cd "${1:?usage: build-host.sh WORKFLOW RUNTIME_ARCHIVE BOOTSTRAP_ARCHIVE NEW_ROOT}" && pwd)
runtime=${2:?missing runtime source}
bootstrap=${3:?missing bootstrap source}
root=$(realpath -m "${4:?missing new prepared root}")
/usr/bin/python3 "$workflow/prepare.py" "$runtime" "$bootstrap" "$root"
/usr/bin/python3 "$workflow/prepare-version.py" "$root"
/usr/bin/python3 "$workflow/prepare-file-version.py" "$root"
export RUNTIME_FILE_VERSION=$(cat "$root/file-version.txt")
export M612="$root/m" RT_VER=8.0.31 HOST_VER=8.0.31 PACK_VER=8.0.31 MSYSTEM=CLANG64
export SDKROOT="$root/host-layout" JOBS=${SDK_SERVICING_JOBS:-2}
export DOTNET_CLI_TELEMETRY_OPTOUT=1 DOTNET_CLI_HOME=$(cygpath -am "$root/cli-home")
export NUGET_PACKAGES=$(cygpath -am "$root/no-packages")
helper="$root/bootstrap/dotnet-bootstrap-3cbf21078968e2290a54ab093392b4dfeb1c6f1f/bootstrap/m612/corehost.sh"
bash "$helper" prep configure build install > "$root/corehost.log" 2>&1
echo 'PASS fresh source8.0.31 apphost/comhost/nethost and native link/header assets; SDK pack integration remains required'
