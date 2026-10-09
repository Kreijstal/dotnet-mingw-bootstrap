#!/bin/bash
# S11: the .NET SDK's own managed parts as net8.0 assemblies, from dotnet/sdk
# v8.0.131 (the tree sdk.sh prepared), with sdkproj.sh (DotNetBuildFromSource=true,
# IncludeAspNetCoreRuntime=false):
#   resolvers  Microsoft.DotNet.NativeWrapper, SdkResolver, WorkloadManifestReader,
#              Microsoft.DotNet.MSBuildSdkResolver, Microsoft.NET.Sdk.WorkloadMSBuildSdkResolver
#   cli        Microsoft.DotNet.InternalAbstractions, Cli.Utils, Configurer, Cli.Sln.Internal,
#              TemplateLocator, Microsoft.Win32.Msi, Microsoft.TemplateEngine.Cli, dotnet.dll
#   tasks      Microsoft.NET.Build.Tasks (net8.0, Sdks/Microsoft.NET.Sdk/tools/net8.0)
#
# usage: sdk-net8.sh [step...]     steps: cli tasks   (default: both; layout is sdk-layout-net8.sh)
#   (step dotnet rebuilds only dotnet.dll)
# Needs: msbuild-net8.sh, nuget-net8.sh, clideps-net8.sh (all steps incl. templating), OOB libraries
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
. "$here/../env-m612.sh"
steps=${*:-cli tasks}
V=8.0.131
S=$M612/build/sdk-$V
B=$M612/build/sdk-net8; mkdir -p "$B/obj"
BCSC=${BCSC:-$M612/roslyn-4.8.0/csc.exe}
MSB8=${MSB8:-$M612/msbuild-17.8.49-net8}
NUGET8=${NUGET8:-$M612/nuget-6.8.2.3-net8}
. "$here/common.sh"
. "$here/sdkproj.sh"
[ -d "$S/src/Cli" ] || die "run sdk.sh $V prep first"
SP_OUT=$B/out; SP_GROUP=sdk-net8; mkdir -p "$SP_OUT"
SP_TF=net8.0; SP_VER=$V.0; SP_FILEVER=$V.0; SP_INFOVER=$V; SP_DEFS=(); SP_DEFAULT_KEY=MSShared
SP_EVAL=(-p RepoRoot=$S/ -p DotNetBuildFromSource=true -p IncludeAspNetCoreRuntime=false -p Configuration=Release
  -p OS=Windows_NT --pre "$S/Directory.Build.props")
SP_REFS=("$MSB8" "$NUGET8" "$M612/net8-pkgs"); SP_EXPLICIT=1
p() { SP_TF=${2:-net8.0}; sdkproj "$S/src/$1"; SP_TF=net8.0; }
# assemblies that compile other projects' sources in (resolvers, template locator) go to a
# directory of their own; references are the transitive ProjectReferences only (SP_EXPLICIT)
ps() { local o=$SP_OUT; SP_OUT=$B/out-$(basename "$1" .csproj); p "$@"; SP_OUT=$o; }

step_cli() {
  p Cli/Microsoft.DotNet.InternalAbstractions/Microsoft.DotNet.InternalAbstractions.csproj netstandard2.0
  p Cli/Microsoft.DotNet.Cli.Utils/Microsoft.DotNet.Cli.Utils.csproj
  p Resolvers/Microsoft.DotNet.NativeWrapper/Microsoft.DotNet.NativeWrapper.csproj
  p Resolvers/Microsoft.DotNet.SdkResolver/Microsoft.DotNet.SdkResolver.csproj
  p Resolvers/Microsoft.NET.Sdk.WorkloadManifestReader/Microsoft.NET.Sdk.WorkloadManifestReader.csproj
  ps Resolvers/Microsoft.DotNet.MSBuildSdkResolver/Microsoft.DotNet.MSBuildSdkResolver.csproj
  ps Resolvers/Microsoft.NET.Sdk.WorkloadMSBuildSdkResolver/Microsoft.NET.Sdk.WorkloadMSBuildSdkResolver.csproj
  p Cli/Microsoft.DotNet.Configurer/Microsoft.DotNet.Configurer.csproj
  p Cli/Microsoft.DotNet.Cli.Sln.Internal/Microsoft.DotNet.Cli.Sln.Internal.csproj
  ps Microsoft.DotNet.TemplateLocator/Microsoft.DotNet.TemplateLocator.csproj
  p Microsoft.Win32.Msi/Microsoft.Win32.Msi.csproj
  # Wcwidth.Sources 1.0.0 (a source-only package): the same files from spectreconsole/wcwidth 1.0.0
  local W=$M612/src/wcwidth-1.0.0
  [ -d "$W/src" ] || { mkdir -p "$W"; tar -C "$W" --strip-components=1 -xzf "$M612/src/wcwidth-1.0.0.tar.gz"; delete_binaries "$W"; }
  SP_TF=net8.0; sdkproj "$S/src/Cli/Microsoft.TemplateEngine.Cli/Microsoft.TemplateEngine.Cli.csproj" $(find "$W/src/Wcwidth" -name '*.cs' ! -path '*/obj/*')
  step_dotnet
}
step_dotnet() { p Cli/dotnet/dotnet.csproj; }
step_tasks() { p Tasks/Microsoft.NET.Build.Tasks/Microsoft.NET.Build.Tasks.csproj; }

for s in $steps; do log "sdk $V net8.0: $s"; "step_$s"; done
