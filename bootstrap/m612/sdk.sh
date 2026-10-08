#!/bin/bash
# Build the .NET SDK's MSBuild part for .NET Framework hosts (dotnet/sdk
# Microsoft.NET.Build.Tasks, net472 flavour) from source and lay out an SDK
# directory (sdk/<ver>/Sdks/Microsoft.NET.Sdk/...) that the source-built
# MSBuild on Mono resolves through MSBuildSDKsPath.  This is what Visual
# Studio's MSBuild uses to build SDK-style projects, and what Mono 6.12 used
# to ship (from prebuilt nupkgs).
#
# usage: sdk.sh <version> <csc.exe> <msbuild-version> [step...]
#   steps: prep tasks layout test
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
. "$here/../env-m612.sh"
V=${1:?sdk version}; BCSC=${2:?csc.exe}; MSB=${3:?msbuild version}; shift 3
steps=${*:-prep tasks layout test}
SRC=$M612/src/sdk-$V.tar.gz
B=$M612/build/sdk-$V
PKGDIR=${PKGDIR:-$M612/pkgs}
MSBOUT=$M612/msbuild-$MSB/tools
SDKROOT=$M612/dotnet-sdk-$V          # <root>/sdk/<V>/{Sdks,Microsoft.NETCoreSdk.BundledVersions.props}
SDKDIR=$SDKROOT/sdk/$V
RT_VER=${RT_VER:-8.0.0}; NETCOREAPP_VER=${NETCOREAPP_VER:-8.0.31}
. "$here/common.sh"
O=$B/obj/out
P=$here/../../patches/m612/sdk-$V

step_prep() {
  fetch_tarball "https://github.com/dotnet/sdk/archive/refs/tags/v$V.tar.gz" "$SRC"
  rm -rf "${B:?}"; mkdir -p "$B"
  tar -C "$B" --strip-components=1 -xzf "$SRC"
  delete_binaries "$B"    # 2 test PEs (dummyshim.exe, testwpf.dll)
  apply_patches "$B" "$P"
  mkdir -p "$O"
}

FWREFS=("${NETFX_REFS[@]}" -r:"$RA/System.Net.Http.dll" -r:"$RA/Microsoft.CSharp.dll" -r:"$RA/System.IO.Compression.FileSystem.dll"
  -r:"$RA/System.Security.dll" -r:"$RA/System.Data.dll" -r:"$RA/Facades/System.Threading.dll" -r:"$RA/Facades/System.IO.dll"
  -r:"$RA/Facades/System.Reflection.dll" -r:"$RA/Facades/System.Runtime.Extensions.dll" -r:"$RA/Facades/System.Linq.dll"
  -r:"$RA/Facades/System.Text.Encoding.dll" -r:"$RA/Facades/System.Runtime.InteropServices.RuntimeInformation.dll"
  -r:"$RA/Facades/System.Security.Cryptography.Algorithms.dll" -r:"$RA/Facades/System.Security.Cryptography.Primitives.dll"
  -r:"$RA/Facades/System.Runtime.InteropServices.dll" -r:"$RA/Facades/System.Diagnostics.Process.dll")
dllrefs() { local f; for f in "$@"; do [ -e "$f" ] && echo "-r:$(winpath "$f")"; done; }

step_tasks() {
  local pd="$B/src/Tasks/Microsoft.NET.Build.Tasks" o="$B/obj/tasks" items=() f
  rm -rf "${o:?}"; mkdir -p "$o"
  mapfile -t items < <(cd "$pd" && CSPROJ_NO_FOLLOW=1 csproj_items -p TargetFramework=net472 -p RepoRoot="$B/" \
      -p TargetFrameworkIdentifier=.NETFramework Microsoft.NET.Build.Tasks.csproj 2>/dev/null | grep -v '/obj/\|/bin/' | sed "s|^|$pd/|")
  # two string tables with GenerateSource (Arcade GenerateResxSource)
  resgen "$B/src/Tasks/Common/Resources/Strings.resx" "$o/Microsoft.NET.Build.Tasks.Resources.Strings.resources" > /dev/null
  resxgen "$B/src/Tasks/Common/Resources/Strings.resx" "$o/Strings.g.cs" Microsoft.NET.Build.Tasks.Strings \
    --resource-name Microsoft.NET.Build.Tasks.Resources.Strings
  resgen "$B/src/Resolvers/Microsoft.NET.Sdk.WorkloadManifestReader/Strings.resx" "$o/Microsoft.NET.Sdk.Localization.WorkloadManifestReader.Strings.resources" > /dev/null
  resxgen "$B/src/Resolvers/Microsoft.NET.Sdk.WorkloadManifestReader/Strings.resx" "$o/WmrStrings.g.cs" Microsoft.NET.Sdk.Localization.Strings \
    --resource-name Microsoft.NET.Sdk.Localization.WorkloadManifestReader.Strings
  # <ImplicitUsings>enable</ImplicitUsings> (repo Directory.Build.props): the SDK's GenerateGlobalUsings set for Microsoft.NET.Sdk
  # plus the repo's <Using Include> items (Directory.Build.props)
  printf 'global using global::%s;\n' System System.Collections.Generic System.IO System.Linq System.Net.Http System.Threading System.Threading.Tasks \
    System.Runtime.InteropServices System.Text System.Xml.Linq > "$o/GlobalUsings.g.cs"
  printf '[assembly: System.Reflection.AssemblyVersion("%s")]\n[assembly: System.Reflection.AssemblyFileVersion("%s")]\n[assembly: System.Reflection.AssemblyInformationalVersion("%s+m612.bootstrap.from.source")]\n' \
    "$V.0" "$V.0" "$V" > "$o/AssemblyInfo.g.cs"
  csc Microsoft.NET.Build.Tasks -target:library -nostdlib -optimize+ -debug:portable -deterministic -unsafe -nullable:annotations \
    -langversion:latest -out:"$O/Microsoft.NET.Build.Tasks.dll" -publicsign -keyfile:"$(keyfile Microsoft)" \
    -d:TRACE -d:NETFRAMEWORK -d:NET472 -d:NET472_OR_GREATER -d:NET46_OR_GREATER -d:NET45_OR_GREATER \
    -nowarn:CS1591,CS1573,CS0618,CS0612,CS1574,CS8600,CS8601,CS8602,CS8603,CS8604,CS8618,CS8625,CS0436 \
    "${FWREFS[@]}" $(dllrefs "$PKGDIR"/*.dll) \
    $(dllrefs "$MSBOUT"/Microsoft.Build.Framework.dll "$MSBOUT"/Microsoft.Build.Utilities.Core.dll "$MSBOUT"/Microsoft.Build.dll "$MSBOUT"/Microsoft.Build.Tasks.Core.dll "$MSBOUT"/Microsoft.NET.StringTools.dll) \
    $(dllrefs "$MSBOUT"/NuGet.*.dll) \
    -resource:"$o/Microsoft.NET.Build.Tasks.Resources.Strings.resources" -resource:"$o/Microsoft.NET.Sdk.Localization.WorkloadManifestReader.Strings.resources" \
    "$o/Strings.g.cs" "$o/WmrStrings.g.cs" "$o/GlobalUsings.g.cs" "$o/AssemblyInfo.g.cs" "${items[@]}" || die "Microsoft.NET.Build.Tasks failed ($B/obj/Microsoft.NET.Build.Tasks.log)"
}

step_layout() {
  local S="$SDKDIR/Sdks/Microsoft.NET.Sdk" t="$B/src/Tasks/Microsoft.NET.Build.Tasks" f
  rm -rf "${SDKROOT:?}"; mkdir -p "$S/Sdk" "$S/targets" "$S/tools/net472"
  cp "$t"/sdk/* "$S/Sdk/"
  cp -r "$t"/targets/* "$B"/src/Tasks/Common/targets/* "$S/targets/"
  cp "$O"/Microsoft.NET.Build.Tasks.dll "$O"/Microsoft.NET.Build.Tasks.pdb "$S/tools/net472/"
  for f in "$PKGDIR"/*.dll "$MSBOUT"/NuGet.*.dll; do cp "$f" "$S/tools/net472/"; done
  # Microsoft.NETCoreSdk.BundledVersions.props: generated by the SDK's redist
  # layout upstream (GenerateBundledVersions.targets, needs the whole product);
  # this hand-written subset is enough for .NETFramework targets.  The
  # KnownFrameworkReference/KnownAppHostPack items for net8.0 come later, with
  # the from-source reference and runtime packs.
  cat > "$SDKDIR/Microsoft.NETCoreSdk.BundledVersions.props" <<EOP
<Project>
  <PropertyGroup>
    <NETCoreSdkVersion>$V</NETCoreSdkVersion>
    <NETCoreSdkRuntimeIdentifier>win-x64</NETCoreSdkRuntimeIdentifier>
    <NETCoreSdkPortableRuntimeIdentifier>win-x64</NETCoreSdkPortableRuntimeIdentifier>
    <_NETCoreSdkIsPreview>false</_NETCoreSdkIsPreview>
    <NETCoreAppMaximumVersion>8.0</NETCoreAppMaximumVersion>
    <BundledNETCoreAppTargetFrameworkVersion>8.0</BundledNETCoreAppTargetFrameworkVersion>
    <BundledNETCoreAppPackageVersion>$NETCOREAPP_VER</BundledNETCoreAppPackageVersion>
    <BundledNETStandardTargetFrameworkVersion>2.1</BundledNETStandardTargetFrameworkVersion>
    <BundledNETStandardPackageVersion>2.1.0</BundledNETStandardPackageVersion>
    <BundledNETCorePlatformsPackageVersion>$NETCOREAPP_VER</BundledNETCorePlatformsPackageVersion>
    <BundledRuntimeIdentifierGraphFile>\$(MSBuildThisFileDirectory)RuntimeIdentifierGraph.json</BundledRuntimeIdentifierGraphFile>
    <NETCoreSdkRuntimeIdentifierChain>win-x64;win;any;base</NETCoreSdkRuntimeIdentifierChain>
  </PropertyGroup>
</Project>
EOP
  rm -rf "${MSBOUT:?}/SdkResolvers/Microsoft.DotNet.MSBuildSdkResolver"
  # The SDK bundles dotnet/roslyn-analyzers (analyzers/, codestyle/) and imports
  # analyzers/build/Microsoft.CodeAnalysis.NetAnalyzers.props unconditionally for
  # C#.  roslyn-analyzers is not built yet: install an empty props file in its
  # place (EnableNETAnalyzers defaults to false for .NETFramework anyway).
  mkdir -p "$S/analyzers/build"
  printf '<Project>\n  <!-- m612 bootstrap: placeholder, dotnet/roslyn-analyzers not built yet -->\n</Project>\n' \
    > "$S/analyzers/build/Microsoft.CodeAnalysis.NetAnalyzers.props"
  # RID graph: runtime.json from source (dotnet/runtime Microsoft.NETCore.Platforms)
  local rg; rg=$(ls "$M612"/src/runtime-$RT_VER/src/libraries/Microsoft.NETCore.Platforms/src/runtime.json 2>/dev/null || true)
  [ -n "$rg" ] && cp "$rg" "$SDKDIR/RuntimeIdentifierGraph.json" && cp "$rg" "$SDKDIR/PortableRuntimeIdentifierGraph.json"
  # point the MSBuild toolset at this SDK directory
  cp "$MSBOUT/MSBuild.exe.config" "$B/obj/MSBuild.exe.config.orig"; : > "$B/obj/none.txt"
  python3 -S "$here/tools/fix_config.py" "$B/obj/MSBuild.exe.config.orig" "$MSBOUT/MSBuild.exe.config" "$B/obj/none.txt" \
    "MSBuildSDKsPath=$(cygpath -w "$SDKDIR/Sdks")" "MSBuildEnableWorkloadResolver=false" "SuppressImplicitGitSourceLink=true"
  # (SourceLink: dotnet/sourcelink is not built yet, so its bundled Sdks are absent;
  #  no workloads; the upstream sentinel mechanism needs the dotnet SDK resolver,
  #  which finds SDKs through the native hostfxr.dll we do not have yet)
  printf '#!/bin/sh\nMSBuildSDKsPath="%s" exec mono "%s/MSBuild.exe" "$@"\n' "$(cygpath -w "$SDKDIR/Sdks")" "$(cygpath -m "$MSBOUT")" > "$SDKROOT/msbuild-mono"
  chmod +x "$SDKROOT/msbuild-mono"
}

step_test() {
  local T=$M612/test/sdk-$V-sdkstyle
  rm -rf "${T:?}"; mkdir -p "$T/emptyfeed"; cp "$here"/tests/sdkstyle/* "$T/"
  cd "$T"
  # the default SDK resolver takes MSBuildSDKsPath from the environment (BuildEnvironmentHelper)
  MSBuildSDKsPath=$(cygpath -w "$SDKDIR/Sdks") mono "$MSBOUT/MSBuild.exe" -nologo -m:1 -nr:false -v:${TEST_VERBOSITY:-m} -restore sdkstyle.csproj
  mono bin/Debug/net472/sdkstyle.exe
}

for s in $steps; do log "sdk $V: $s"; "step_$s"; done
