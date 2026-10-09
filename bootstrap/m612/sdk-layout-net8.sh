#!/bin/bash
# S11: lay out a .NET SDK whose tools are the net8.0 builds (roslyn-net8.sh,
# msbuild-net8.sh, nuget-net8.sh, clideps-net8.sh, sdk-net8.sh), in the
# upstream shape, so that dotnet.exe -> hostfxr resolves sdk/<ver>/dotnet.dll
# and `dotnet build` runs MSBuild.dll on the source-built runtime:
#
#   <root>/dotnet.exe, host/fxr, shared/Microsoft.NETCore.App/8.0.0, packs/  (copied from the
#                                                                           mono-based SDK root)
#   <root>/sdk/8.0.131/  dotnet.dll + CLI libraries, MSBuild.dll + targets, NuGet, Roslyn/(bincore),
#                        Sdks/, SdkResolvers/, BundledVersions.props, RID graphs, trustedroots
#   <root>/templates/8.0.131/  the common project/item templates (nupkgs zipped from source;
#                              step layout recreates <root>, so run templates after it)
#
# usage: sdk-layout-net8.sh [step...]    steps: layout templates   (default: both)
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
. "$here/../env-m612.sh"
steps=${*:-layout templates}
V=8.0.131
OLD=${OLD:-$M612/dotnet-sdk-$V}
ROOT=${ROOT:-$M612/dotnet-sdk-$V-net8}
SDK=$ROOT/sdk/$V
S=$M612/build/sdk-$V
B=$M612/build/sdk-net8
MSB8=$M612/msbuild-17.8.49-net8
NUGET8=$M612/nuget-6.8.2.3-net8
R8=$M612/roslyn-4.8.0-net8
. "$here/common.sh"

step_layout() {
  rm -rf "${ROOT:?}"; mkdir -p "$SDK"
  cp -r "$OLD/dotnet.exe" "$OLD/host" "$OLD/shared" "$OLD/packs" "$ROOT/"
  # MSBuild (assemblies, targets, Current/, its out-of-band dependencies)
  cp -r "$MSB8"/. "$SDK/"
  # out-of-band libraries (Microsoft.Extensions.*, ConfigurationManager, ...), third-party/satellite libs
  cp "$NET8_OOB"/*.dll "$M612"/net8-pkgs/*.dll "$SDK/"
  # NuGet (pack task goes to Sdks/NuGet.Build.Tasks.Pack)
  cp "$NUGET8"/*.dll "$NUGET8"/*.targets "$NUGET8"/*.props "$NUGET8"/*.runtimeconfig.json "$SDK/"
  # the CLI and its libraries; the resolvers below
  local f
  for f in "$B"/out/*.dll; do case $(basename "$f") in Microsoft.NET.Build.Tasks.dll|Microsoft.NET.Sdk.WorkloadManifestReader.dll) ;; *) cp "$f" "$SDK/" ;; esac; done
  cp "$B"/out-Microsoft.DotNet.TemplateLocator/Microsoft.DotNet.TemplateLocator.dll "$SDK/"
  cat > "$SDK/dotnet.runtimeconfig.json" <<'EOJ'
{
  "runtimeOptions": {
    "tfm": "net8.0",
    "framework": { "name": "Microsoft.NETCore.App", "version": "8.0.0" },
    "configProperties": {
      "System.GC.Concurrent": false,
      "System.Runtime.TieredPGO": true,
      "System.Runtime.Serialization.EnableUnsafeBinaryFormatterSerialization": false
    }
  }
}
EOJ
  # Roslyn: MSBuild integration in Roslyn/, the compilers in Roslyn/bincore (as the SDK ships them)
  mkdir -p "$SDK/Roslyn/bincore"
  cp "$R8"/tasks/* "$SDK/Roslyn/"
  cp "$R8"/bincore/* "$SDK/Roslyn/bincore/"
  # Sdks: the targets of the mono-based SDK (same sources) + the net8.0 task assembly
  cp -r "$OLD/sdk/$V/Sdks" "$SDK/"
  local T=$SDK/Sdks/Microsoft.NET.Sdk/tools/net8.0; mkdir -p "$T"
  cp "$B"/out/Microsoft.NET.Build.Tasks.dll "$B"/out/Microsoft.NET.Build.Tasks.pdb "$T/" 2>/dev/null || cp "$B"/out/Microsoft.NET.Build.Tasks.dll "$T/"
  for f in Microsoft.NET.HostModel Microsoft.Deployment.DotNet.Releases Microsoft.Extensions.DependencyModel; do
    cp "$SDK/$f.dll" "$T/"; done
  # dotnet/roslyn-analyzers (NetAnalyzers) not built: empty props/targets in their place
  # (the .NETCoreApp targets import the .targets too, EnableNETAnalyzers defaults to true there)
  [ -f "$SDK/Sdks/Microsoft.NET.Sdk/analyzers/build/Microsoft.CodeAnalysis.NetAnalyzers.targets" ] || \
    printf '<Project>\n  <!-- m612 bootstrap: placeholder, dotnet/roslyn-analyzers not built yet -->\n</Project>\n' \
      > "$SDK/Sdks/Microsoft.NET.Sdk/analyzers/build/Microsoft.CodeAnalysis.NetAnalyzers.targets"
  # SourceLink (sourcelink-net8.sh), imported by Microsoft.NET.Sdk.SourceLink.props
  cp -r "$M612"/sourcelink-8.0.0-net8/* "$SDK/Sdks/"
  rm -rf "${SDK:?}/Sdks/NuGet.Build.Tasks.Pack"; mkdir -p "$SDK/Sdks/NuGet.Build.Tasks.Pack/CoreCLR"
  cp -r "$NUGET8"/pack/build "$NUGET8"/pack/buildCrossTargeting "$SDK/Sdks/NuGet.Build.Tasks.Pack/"
  cp "$NUGET8"/pack/NuGet.Build.Tasks.Pack.dll "$SDK/Sdks/NuGet.Build.Tasks.Pack/CoreCLR/"
  # SDK resolvers: the dotnet resolver (SDKs + workloads, uses hostfxr) and NuGet's (msbuild-sdks from packages)
  mkdir -p "$SDK/SdkResolvers/Microsoft.DotNet.MSBuildSdkResolver" "$SDK/SdkResolvers/Microsoft.Build.NuGetSdkResolver"
  cp "$B"/out-Microsoft.DotNet.MSBuildSdkResolver/Microsoft.DotNet.MSBuildSdkResolver.dll "$SDK/SdkResolvers/Microsoft.DotNet.MSBuildSdkResolver/"
  printf '<SdkResolver>\n  <Path>..\\..\\Microsoft.Build.NuGetSdkResolver.dll</Path>\n</SdkResolver>\n' \
    > "$SDK/SdkResolvers/Microsoft.Build.NuGetSdkResolver/Microsoft.Build.NuGetSdkResolver.xml"
  # files the redist layout adds
  cp "$OLD/sdk/$V/Microsoft.NETCoreSdk.BundledVersions.props" "$OLD/sdk/$V"/*RuntimeIdentifierGraph.json "$SDK/"
  # dotnet/installer's GenerateBundledVersions also defines where the dotnet root and packs are
  # (the mono-based SDK passed them as -p:NetCoreRoot/-p:NetCoreTargetingPackRoot instead)
  python3 -S - "$SDK/Microsoft.NETCoreSdk.BundledVersions.props" <<'EOPY'
import sys
p = sys.argv[1]; s = open(p).read()
add = """    <NetCoreRoot Condition="'$(NetCoreRoot)' == ''">$([MSBuild]::NormalizePath('$(MSBuildExtensionsPath)', '..', '..'))</NetCoreRoot>
    <NetCoreTargetingPackRoot Condition="'$(NetCoreTargetingPackRoot)' == ''">$([MSBuild]::EnsureTrailingSlash('$(NetCoreRoot)'))packs</NetCoreTargetingPackRoot>
    <!-- m612: dotnet/roslyn-analyzers is not built; the NetAnalyzers stay off unless a project turns them on -->
    <EnableNETAnalyzers Condition="'$(EnableNETAnalyzers)' == ''">false</EnableNETAnalyzers>
"""
s = s.replace('  </PropertyGroup>', add + '  </PropertyGroup>', 1)
open(p, 'w', newline='\n').write(s)
EOPY
  mkdir -p "$SDK/trustedroots" "$SDK/Current/SolutionFile/ImportAfter"
  cp "$S"/src/Layout/redist/trustedroots/*.pem "$SDK/trustedroots/"
  cp "$S/src/Layout/redist/MSBuildImports/Current/SolutionFile/ImportAfter/Microsoft.NuGet.ImportAfter.targets" "$SDK/Current/SolutionFile/ImportAfter/"
  printf '%s\n%s\n%s\n%s\n' "$(cat "$S/.commit" 2>/dev/null || echo 0000000000000000000000000000000000000000)" "$V" win-x64 "$V" > "$SDK/.version"
  echo "layout: $(find "$ROOT" -type f | wc -l) files, $(du -sh "$ROOT" | cut -f1)"
}

# dotnet new templates: Microsoft.DotNet.Common.ProjectTemplates.8.0 / ItemTemplates, zipped
# from template_feed/ (text only) with a nuspec, where the template locator looks for them
step_templates() {
  local D=$ROOT/templates/$V; rm -rf "${D:?}"; mkdir -p "$D"
  python3 -S - "$S/template_feed" "$D" "$V" <<'EOPY'
import os, sys, zipfile
feed, out, ver = sys.argv[1:4]
for pid in ('Microsoft.DotNet.Common.ProjectTemplates.8.0', 'Microsoft.DotNet.Common.ItemTemplates'):
    src = os.path.join(feed, pid, 'content')
    nupkg = os.path.join(out, ('%s.%s.nupkg' % (pid, ver)).lower())
    nuspec = ('<?xml version="1.0" encoding="utf-8"?>\n<package xmlns="http://schemas.microsoft.com/packaging/2013/05/nuspec.xsd">\n'
              '  <metadata>\n    <id>%s</id>\n    <version>%s</version>\n    <authors>Microsoft</authors>\n'
              '    <description>%s (m612 bootstrap, zipped from dotnet/sdk template_feed)</description>\n'
              '    <packageTypes><packageType name="Template" /></packageTypes>\n  </metadata>\n</package>\n') % (pid, ver, pid)
    n = 0
    with zipfile.ZipFile(nupkg, 'w', zipfile.ZIP_DEFLATED) as z:
        z.writestr(pid + '.nuspec', nuspec)
        for dp, dn, fn in os.walk(src):
            for f in fn:
                full = os.path.join(dp, f)
                z.write(full, 'content/' + os.path.relpath(full, src).replace('\\', '/')); n += 1
    print('%s: %d files' % (os.path.basename(nupkg), n))
EOPY
}

for s in $steps; do log "sdk layout net8: $s"; "step_$s"; done
