#!/usr/bin/env bash
set -euo pipefail
root=$(cd "${1:?usage: build-framework.sh PREPARED_ROOT [STEP]}" && pwd)
export RUNTIME_PREPARED_ROOT="$root" RUNTIME_CLEAR_FORWARDER_SCRIPT="$(cd "$(dirname "$0")" && pwd)/clear-forwarder-versions.py"
export RUNTIME_FILE_VERSION=$(cat "$root/file-version.txt")
here="$root/bootstrap/dotnet-bootstrap-3cbf21078968e2290a54ab093392b4dfeb1c6f1f/bootstrap/m612"
export M612="$root/m" RT_VER=8.0.31 PACK_VER=8.0.31 MSYSTEM=UCRT64 RUNTIME_FLAVOR=CoreCLR
export FXOUT="$root/m/netcoreapp-8.0.31-coreclr" SDKROOT="$root/layout"
export CSC_DOTNET=/clang64/lib/dotnet/dotnet.exe VBC=/clang64/lib/dotnet/compiler-packs/roslyn-vb-4.9.2/vbc.dll
export DOTNET_ROOT=$(cygpath -am /clang64/lib/dotnet) DOTNET_ROOT_X64=$(cygpath -am /clang64/lib/dotnet) DOTNET_CLI_TELEMETRY_OPTOUT=1 MSBuildEnableWorkloadResolver=false
export DOTNET_CLI_HOME=$(cygpath -am "$root/managed-build/cli-home") NUGET_PACKAGES=$(cygpath -am "$root/managed-build/cache")
csc=/clang64/lib/dotnet/compiler-packs/roslyn-4.9.2/csc.dll
step=${2:-all}
if [[ $step == all || $step == refs ]];then
 bash "$here/refpack.sh" "$csc" plan refs > "$root/refpack.log" 2>&1
fi
if [[ $step == plan ]];then
 bash "$here/libraries.sh" "$csc" plan > "$root/library-plan.log" 2>&1
fi
if [[ $step == all || $step == libraries || $step == build ]];then
 for name in Microsoft.Interop.SourceGeneration Microsoft.Interop.LibraryImportGenerator Microsoft.Interop.ComInterfaceGenerator System.Private.CoreLib.Generators System.Text.RegularExpressions.Generator;do
  cp "$root/managed-build/$name/bin/Release/net8.0/"*.dll "$root/m/pkgs/analyzers/"
 done
 stages='build vb report'
 if [[ $step != build ]];then stages="plan $stages";fi
 bash "$here/libraries.sh" "$csc" $stages > "$root/libraries.log" 2>&1
fi
printf 'PASS requested serviced framework build step: %s; runtime/layout/integration verification remains required\n' "$step"
