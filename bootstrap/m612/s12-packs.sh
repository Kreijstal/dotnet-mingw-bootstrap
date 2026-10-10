#!/bin/bash
# S12: the targeting packs the S12 builds compile against: $S12/packroot/packs = the build SDK's packs
# (apphost, runtime pack: hard links) + Microsoft.NETCore.App.Ref 8.0.31 rebuilt by refpack.sh with two
# fixes the stage-1 pack lacked (both found by S12's MSBuild build):
#   - [assembly: CLSCompliant(true)] on every reference assembly (eng/versioning.targets adds it upstream;
#     without it every CLS-checked compile fails with CS3009 "base type 'object' is not CLS-compliant")
#   - data/PlatformManifest.txt (the SDK's conflict resolution: without it a net8.0 publish copied lower
#     package versions of framework assemblies, e.g. SBRP System.Collections.Immutable 7.0.0, next to the app)
# The pack is compiled by the build SDK's net8.0 csc.dll (no Mono).  The S12 builds point
# NetCoreTargetingPackRoot at it (s12_props); the existing SDK roots are left as they are.
# usage: s12-packs.sh
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
. "$here/../env-m612.sh"
B=$M612/s12
. "$here/common.sh"
. "$here/s12-common.sh"
P=$S12/packroot
rm -rf "${P:?}"; mkdir -p "$P/packs"
for d in "$BUILD_SDK"/packs/*; do
  case $(basename "$d") in Microsoft.NETCore.App.Ref) ;; *) cp -al "$d" "$P/packs/" ;; esac
done
export CSC_DOTNET=$BUILD_SDK/dotnet.exe
t0=$SECONDS
REFPACK_B=$S12/build/refpack SDKROOT=$P bash "$here/refpack.sh" "$BUILD_SDK/sdk/8.0.131/Roslyn/bincore/csc.dll" plan refs pack
echo "TIME refpack $((SECONDS - t0)) s"
ls "$P/packs/Microsoft.NETCore.App.Ref"/*/ref/net8.0 | wc -l
