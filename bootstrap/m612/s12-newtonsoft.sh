#!/bin/bash
# S12: Newtonsoft.Json 13.0.3 (JamesNK/Newtonsoft.Json tag 13.0.3, the version the SDK, NuGet and MSBuild
# pin) with `dotnet build/pack` of Src/Newtonsoft.Json/Newtonsoft.Json.csproj, as the VMR does:
# LibraryFrameworks net6.0;netstandard2.0, version 13.0.3 / assembly 13.0.0.0 (what its build script
# passes), public-signed with the Newtonsoft public key (Dynamic.snk, a key pair, is deleted).
# Patch 0001 drops its NetAnalyzers/SourceLink tool packages.
# usage: s12-newtonsoft.sh [prep] [build]
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
. "$here/../env-m612.sh"
steps=${*:-prep build}
B=$M612/s12
. "$here/common.sh"
. "$here/s12-common.sh"
X=$WORK/newtonsoft-json
TAR=$M612/src/newtonsoft.json-13.0.3.tar.gz
step_prep() {
  s12_prep_repo "$TAR" "$X" "$here/../../patches/m612/s12-newtonsoft.json-13.0.3"
  rm -f "$X/Src/Newtonsoft.Json/Dynamic.snk"
  python3 -S "$M612_TOOLS_SRC/s12globaljson.py" "$X/Src/global.json" 8.0.131
  s12_nugetconfig "$X/Src"
}
step_build() {
  # (an environment variable: an escaped ';' in a global property would stay one TargetFramework)
  LibraryFrameworks='net6.0;netstandard2.0' s12_build newtonsoft "$X/Src" Newtonsoft.Json/Newtonsoft.Json.csproj \
    -p:VersionPrefix=13.0.3 -p:VersionSuffix= -p:AssemblyVersion=13.0.0.0 -p:FileVersion=13.0.3.27908 \
    -p:AdditionalConstants=SIGNED -p:SignAssembly=true -p:PublicSign=true -p:AssemblyOriginatorKeyFile="$(keyfile Newtonsoft)" -p:EnablePackageValidation=false \
    -p:PackageOutputPath="$(w "$X/Src/artifacts/packages")"
}
for s in $steps; do log "s12 newtonsoft.json (stage $STAGE): $s"; "step_$s"; done
