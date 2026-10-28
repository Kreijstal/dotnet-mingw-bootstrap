#!/usr/bin/env bash
# Reuse the bootstrap's source compiler driver with a private output tree.
set -euo pipefail
driver=${1:?usage: build-wmi.sh BOOTSTRAP_M612_DRIVER EXISTING_BOOTSTRAP_ROOT NEW_OUTPUT_ROOT}
seed=${2:?missing existing bootstrap root}
root=${3:?missing new output root}
driver=$(cd "$driver" && pwd)
seed=$(cd "$seed" && pwd)
mkdir -p "$(dirname "$root")"
mkdir "$root"
root=$(cd "$root" && pwd)
mkdir -p "$root/src" "$root/build" "$root/oob-8.0.0/lib"
junction() {
  /clang64/bin/python.exe -c 'import subprocess,sys; subprocess.run(["cmd.exe", "/d", "/c", "mklink", "/J", *sys.argv[1:]], check=True)' "$(cygpath -w "$1")" "$(cygpath -w "$2")"
}
junction "$root/src/runtime-8.0.0-native" "$seed/src/runtime-8.0.0-native"
junction "$root/build/refpack-8.0.0" "$seed/build/refpack-8.0.0"
mkdir -p "$root/pkgs/analyzers"
cp "$seed/pkgs/analyzers/"*.dll "$root/pkgs/analyzers/"
cp "$seed/oob-8.0.0/lib/System.CodeDom.dll" "$root/oob-8.0.0/lib/"
M612="$root" MSYSTEM=UCRT64 CSC_DOTNET=/clang64/lib/dotnet/dotnet.exe OOB=System.Management \
  bash "$driver/libraries.sh" /clang64/lib/dotnet/sdk/8.0.131/Roslyn/bincore/csc.dll plan gens build report
