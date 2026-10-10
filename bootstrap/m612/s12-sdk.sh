#!/bin/bash
# S12: dotnet/sdk v8.0.131 with `dotnet build/pack` of its own project files: the CLI (dotnet.dll and its
# libraries), the SDK resolvers, Microsoft.NET.Build.Tasks, Microsoft.TemplateEngine.Cli, ... (the set
# S11 built with sdkproj.sh), DotNetBuildFromSource, IncludeAspNetCoreRuntime=false (no ASP.NET Core).
# Runtime packages (Microsoft.Extensions.*, System.* out-of-band, HostModel, DotNetHostResolver) and
# ApplicationInsights come from the seed feed (s12-feed.sh), lifted here; the runtime version properties
# (MicrosoftNETCoreAppRuntime*PackageVersion 8.0.31) point at our runtime 8.0.0 (the VMR lifts them to the
# runtime it built); the targeting pack is 8.0.31 already (refpack.sh).
# UsingToolMicrosoftNetCompilers=false: compiled by the building SDK's Roslyn (the repo pins a toolset package).
# usage: s12-sdk.sh [prep] [build]
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
. "$here/../env-m612.sh"
steps=${*:-prep build}
B=$M612/s12
. "$here/common.sh"
. "$here/s12-common.sh"
X=$WORK/sdk
TAR=$M612/src/sdk-8.0.131.tar.gz
PROJS="Cli/Microsoft.DotNet.InternalAbstractions/Microsoft.DotNet.InternalAbstractions.csproj
Cli/Microsoft.DotNet.Cli.Utils/Microsoft.DotNet.Cli.Utils.csproj
Resolvers/Microsoft.DotNet.NativeWrapper/Microsoft.DotNet.NativeWrapper.csproj
Resolvers/Microsoft.DotNet.SdkResolver/Microsoft.DotNet.SdkResolver.csproj
Resolvers/Microsoft.NET.Sdk.WorkloadManifestReader/Microsoft.NET.Sdk.WorkloadManifestReader.csproj
Resolvers/Microsoft.DotNet.MSBuildSdkResolver/Microsoft.DotNet.MSBuildSdkResolver.csproj
Resolvers/Microsoft.NET.Sdk.WorkloadMSBuildSdkResolver/Microsoft.NET.Sdk.WorkloadMSBuildSdkResolver.csproj
Cli/Microsoft.DotNet.Configurer/Microsoft.DotNet.Configurer.csproj
Cli/Microsoft.DotNet.Cli.Sln.Internal/Microsoft.DotNet.Cli.Sln.Internal.csproj
Microsoft.DotNet.TemplateLocator/Microsoft.DotNet.TemplateLocator.csproj
Microsoft.Win32.Msi/Microsoft.Win32.Msi.csproj
Cli/Microsoft.TemplateEngine.Cli/Microsoft.TemplateEngine.Cli.csproj
Cli/dotnet/dotnet.csproj
Tasks/Microsoft.NET.Build.Tasks/Microsoft.NET.Build.Tasks.csproj"
export S12_LIFT_SEEDS="$(cd "$FEED_SEED" && for f in *.nupkg; do unzip -p "$f" '*.nuspec' | grep -o '<id>[^<]*' | cut -c5-; done | tr -d '\r' | tr '\n' ' ')"
step_prep() {
  s12_prep_repo "$TAR" "$X" "$here/../../patches/m612/s12-sdk-8.0.131"
  python3 -S - "$X/s12.slnf" $PROJS <<'EOPY'
import json, sys
json.dump({'solution': {'path': 'sdk.sln', 'projects': ['src\\' + p.replace('/', '\\') for p in sys.argv[2:]]}},
          open(sys.argv[1], 'w', newline='\n'), indent=2)
EOPY
}
step_build() {
  s12_build sdk "$X" s12.slnf -p:OfficialBuildId=20260811.6 -p:RepositoryCommit=$(repo_commit "$TAR") \
    -p:IncludeAspNetCoreRuntime=false -p:UsingToolMicrosoftNetCompilers=false \
    -p:MicrosoftNETCoreAppRuntimePackageVersion=8.0.0 -p:MicrosoftNETCoreAppRuntimewinx64PackageVersion=8.0.0
}
for s in $steps; do log "s12 sdk (stage $STAGE): $s"; "step_$s"; done
