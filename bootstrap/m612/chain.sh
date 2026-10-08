#!/bin/bash
# The whole phase 6 chain as built on win11 (2026-10-08), in order.
# Each step only uses the outputs of earlier steps and the Mono 6.12 seed.
# usage: chain.sh [first-step-number]      (run from anywhere on the build host)
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
. "$here/../env-m612.sh"
R() { cygpath -m "$M612/roslyn-$1/csc.exe"; }
start=${1:-1}
step() { local n=$1; shift; [ "$n" -ge "$start" ] || return 0; log "chain step $n: $*"; "$@"; }

# 1-4: Roslyn from the seed csc 3.9 (C# 9) up to 4.8 (C# 12), seed package assemblies
step 1 bash "$here/roslyn.sh" 4.0.1 "$SEED_CSC"
step 2 bash "$here/roslyn.sh" 4.4.0 "$(R 4.0.1)"
step 3 bash "$here/roslyn.sh" 4.6.0 "$(R 4.4.0)"
step 4 bash "$here/roslyn.sh" 4.8.0 "$(R 4.6.0)"
# 5: netfx "package" assemblies from source (SCI/SRM 8, System.Memory, ...)
step 5 bash "$here/netfx-pkgs.sh" "$(R 4.8.0)" "$M612/pkgs"
export PKGDIR=$M612/pkgs
# 6-11: Roslyn on the from-source packages up to 5.0 (C# 14)
step 6 bash "$here/roslyn.sh" 4.10.0 "$(R 4.8.0)"
step 7 bash "$here/roslyn.sh" 4.11.0 "$(R 4.10.0)"
step 8 bash "$here/roslyn.sh" 4.12.0 "$(R 4.11.0)"
step 9 bash "$here/roslyn.sh" 4.13.0 "$(R 4.12.0)"
step 10 env ROSLYN_TAG=NET-SDK-10.0.100-preview.4.25258.110 bash "$here/roslyn.sh" 5.0.0-p4 "$(R 4.13.0)"
step 11 env ROSLYN_TAG=Visual-Studio-2026-Version-18.0 bash "$here/roslyn.sh" 5.0.0 "$(R 5.0.0-p4)"
# 12: the rest of the package assemblies MSBuild needs (built with C# 13)
step 12 bash "$here/netfx-pkgs.sh" "$(R 4.12.0)" "$M612/pkgs" Microsoft.Bcl.AsyncInterfaces System.Text.Encodings.Web \
  System.Text.Json System.Threading.Tasks.Dataflow System.Reflection.MetadataLoadContext System.Resources.Extensions Microsoft.IO.Redist
# 13: MSBuild 17.8.49 (net472) + Roslyn 4.12 Csc task; tests an SDK-less csproj
step 13 bash "$here/msbuild.sh" 17.8.49 "$(R 4.12.0)" 4.12.0
# 14-16: package deps for NuGet and the SDK tasks (built with C# 13)
step 14 bash "$here/netfx-pkgs.sh" "$(R 4.12.0)" "$M612/pkgs" Newtonsoft.Json Microsoft.Extensions.Primitives \
  Microsoft.Extensions.FileProviders.Abstractions Microsoft.Extensions.FileSystemGlobbing Microsoft.Extensions.DependencyModel \
  Microsoft.NET.HostModel Microsoft.Deployment.DotNet.Releases
# 15: NuGet.Client 6.8.2.3 (net472) into the MSBuild tools dir; tests PackageReference restore from a local feed
step 15 bash "$here/nuget.sh" 6.8.2.3 "$(R 4.12.0)" 17.8.49
# 16: dotnet/sdk 8.0.131 Microsoft.NET.Build.Tasks (net472) + Sdks layout; tests an SDK-style net472 project
step 16 bash "$here/sdk.sh" 8.0.131 "$(R 4.12.0)" 17.8.49
