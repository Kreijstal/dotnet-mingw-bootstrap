#!/bin/bash
# S12: the stage-N SDK root from the outputs of the S12 project builds (s12-*.sh):
#   $M612/dotnet-sdk-8.0.131-stage$STAGE-il   IL assemblies
#   $M612/dotnet-sdk-8.0.131-stage$STAGE      the same, R2R-compiled (crossgen2.sh r2rsdk): the SDK to use
#
# sdk/8.0.131 is put together the way dotnet/sdk's src/Layout/redist does it, from:
#   MSBuild    s<N>/work/msbuild    artifacts/bin/MSBuild/Release/net8.0 (MSBuild.dll, Microsoft.Build*.dll,
#                                   the common targets, Current/, satellites)
#   CLI        s<N>/work/sdk        artifacts/bin/dotnet/Release/net8.0 (dotnet.dll + the libraries it
#                                   references, NuGet/TemplateEngine/... from the packages those repos built,
#                                   the runtime libraries from the seed feed)
#   Sdks       s<N>/work/sdk        artifacts/bin/Release/Sdks (Microsoft.NET.Sdk: targets + Microsoft.NET.Build.Tasks)
#              NuGet.Build.Tasks.Pack and Microsoft.SourceLink.* / Microsoft.Build.Tasks.Git from their packages
#   Roslyn     the Microsoft.Net.Compilers.Toolset package (tasks/netcore -> Roslyn, Roslyn/bincore)
#   NuGet      s<N>/work/nuget      NuGet.Build.Tasks(.Console), NuGet.*.dll, NuGet.targets, the SDK resolver,
#                                   NuGet.CommandLine.XPlat (dotnet add package / dotnet nuget)
#   resolvers  s<N>/work/sdk        Microsoft.DotNet.MSBuildSdkResolver
# Not rebuilt in S12: the runtime's out-of-band libraries (the seed assemblies of libraries.sh, S11), and
# from the stage-1 root: dotnet.exe/host/shared (native host + CoreCLR + the
# framework of S9/S10c, already R2R), packs (s12-packs.sh: the fixed reference pack), library-packs (ILLink),
# templates, BundledVersions.props (hand-written in S7/S11), RID graphs.
# Reference assemblies that a repo's output folder picked up from source-build-reference-packages are never
# copied (MSBuild's and NuGet's bin folders have some); step check fails on any [ReferenceAssembly] DLL.
#
# usage: s12-layout.sh [step...]    steps: layout check r2r   (default: all)
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
. "$here/../env-m612.sh"
steps=${*:-layout check r2r}
B=$M612/s12
. "$here/common.sh"
. "$here/s12-common.sh"
V=8.0.131
S1=$M612/dotnet-sdk-8.0.131-net8-coreclr-r2r     # stage 1 (native host, framework, templates)
S1IL=$M612/dotnet-sdk-8.0.131-net8-coreclr       # stage 1 before R2R (the IL framework)
IL=$M612/dotnet-sdk-$V-stage$STAGE-il
R2R=$M612/dotnet-sdk-$V-stage$STAGE
SDK=$IL/sdk/$V
MSBIN=$WORK/msbuild/artifacts/bin/MSBuild/Release/net8.0
CLI=$WORK/sdk/artifacts/bin/dotnet/Release/net8.0
SDKS=$WORK/sdk/artifacts/bin/Release/Sdks
NG=$WORK/nuget/artifacts

# unzip_pkg <nupkg> <dest> [path-prefix]: extract a package (or the part under path-prefix) into dest
unzip_pkg() {
  local p=$1 d=$2 pre=${3:-} t; t=$(mktemp -d "$S12/build/pkg.XXXX")
  unzip -q -o "$p" -d "$t"
  mkdir -p "$d"; cp -r "$t/$pre"/. "$d/"
  rm -rf "${t:?}"
}
pkg() { ls "$FEED"/"$1".*.nupkg 2>/dev/null | grep -iE "/$1\.[0-9][^/]*\.nupkg$" | head -1; }

step_layout() {
  [ -x "$R2R/dotnet.exe" ] && { "$R2R/dotnet.exe" build-server shutdown > /dev/null 2>&1 || true; }
  rm -rf "${IL:?}"; mkdir -p "$SDK"
  cp -r "$S1/dotnet.exe" "$S1/host" "$S1/templates" "$S1/library-packs" "$IL/"
  cp -r "$S1IL/shared" "$IL/"          # the IL framework (the R2R root takes stage 1's R2R images of it)
  cp -r "$S12/packroot/packs" "$IL/"
  # MSBuild (only what MSBuild builds: its own assemblies, targets, satellites; not the runtime
  # libraries its output folder copied from reference packages)
  local f n
  ( cd "$MSBIN" && for f in *; do
      case $f in
        *.dll) case $f in MSBuild.dll|Microsoft.Build*.dll|Microsoft.NET.StringTools.dll) cp "$f" "$SDK/" ;; esac ;;
        *.pdb|*.xsd|*.targets|*.props|*.tasks|*.overridetasks|MSBuild.rsp|MSBuild.runtimeconfig.json|MSBuild.dll.config) cp "$f" "$SDK/" ;;
        Current|MSBuild|ref) cp -r "$f" "$SDK/" ;;
        *) [ -d "$f" ] && [ -f "$f/Microsoft.Build.resources.dll" -o -f "$f/MSBuild.resources.dll" ] && cp -r "$f" "$SDK/" ;;
      esac
    done )
  # the CLI: dotnet.dll and what it references (MSBuild's own files win); no dotnet.deps.json: as in S11
  # the host probes the SDK directory, where MSBuild, NuGet and the tasks also live
  ( cd "$CLI" && for f in *; do
      case $f in
        MSBuild.dll|Microsoft.Build.dll|Microsoft.Build.Framework.dll|Microsoft.Build.Utilities.Core.dll|Microsoft.Build.Tasks.Core.dll|Microsoft.NET.StringTools.dll|dotnet.deps.json) ;;
        *.dll|*.pdb|dotnet.runtimeconfig.json) cp "$f" "$SDK/" ;;
        *) [ -d "$f" ] && cp -r "$f" "$SDK/" ;;
      esac
    done )
  # the runtime's out-of-band libraries MSBuild, NuGet and the tasks run with (built against the SBRP
  # 7.0/6.0 reference packages; at run time our 8.0.0 builds): the seed assemblies (libraries.sh OOB, S11)
  cp "$M612"/oob-8.0.0/lib/*.dll "$SDK/"
  # NuGet: the restore task and its console runner, the libraries, the targets, the SDK resolver, XPlat
  cp "$NG"/NuGet.Build.Tasks.Console/bin/Release/net8.0/NuGet.*.dll "$NG"/NuGet.Build.Tasks.Console/bin/Release/net8.0/NuGet.*.pdb "$SDK/"
  cp "$NG"/NuGet.Build.Tasks.Console/bin/Release/net8.0/NuGet.Build.Tasks.Console.runtimeconfig.json \
     "$NG"/NuGet.Build.Tasks.Console/bin/Release/net8.0/NuGet.targets "$NG"/NuGet.Build.Tasks.Console/bin/Release/net8.0/NuGet.props \
     "$NG"/NuGet.Build.Tasks.Console/bin/Release/net8.0/NuGet.RestoreEx.targets "$SDK/"
  cp "$NG"/NuGet.Resolver/bin/Release/net8.0/NuGet.Resolver.dll "$NG"/Microsoft.Build.NuGetSdkResolver/bin/Release/net8.0/Microsoft.Build.NuGetSdkResolver.dll "$SDK/"
  cp "$NG"/NuGet.CommandLine.XPlat/bin/Release/net8.0/NuGet.CommandLine.XPlat.dll "$NG"/NuGet.CommandLine.XPlat/bin/Release/net8.0/NuGet.CommandLine.XPlat.runtimeconfig.json "$SDK/"
  # Roslyn: the Microsoft.Net.Compilers.Toolset package (what the SDK's redist lays out)
  unzip_pkg "$(pkg Microsoft.Net.Compilers.Toolset)" "$SDK/Roslyn" tasks/netcore
  # Sdks: Microsoft.NET.Sdk (targets + tasks) as the sdk repo lays it out; the NuGet pack SDK and SourceLink from packages
  mkdir -p "$SDK/Sdks"; cp -r "$SDKS"/. "$SDK/Sdks/"
  local P=$SDK/Sdks/NuGet.Build.Tasks.Pack; mkdir -p "$P/CoreCLR"
  unzip_pkg "$(pkg NuGet.Build.Tasks.Pack)" "$S12/build/nugetpack"
  cp -r "$S12/build/nugetpack/build" "$S12/build/nugetpack/buildCrossTargeting" "$P/"
  cp "$S12/build/nugetpack/CoreCLR/NuGet.Build.Tasks.Pack.dll" "$P/CoreCLR/"; rm -rf "${S12:?}/build/nugetpack"
  for n in Microsoft.Build.Tasks.Git Microsoft.SourceLink.Common Microsoft.SourceLink.GitHub Microsoft.SourceLink.GitLab \
           Microsoft.SourceLink.AzureRepos.Git Microsoft.SourceLink.Bitbucket.Git; do
    unzip_pkg "$(pkg $n)" "$SDK/Sdks/$n"
    rm -rf "$SDK/Sdks/$n"/_rels "$SDK/Sdks/$n"/package "$SDK/Sdks/$n"/*.nuspec "$SDK/Sdks/$n/[Content_Types].xml"
  done
  # dotnet/roslyn-analyzers is not built: empty NetAnalyzers props/targets as in S11 (EnableNETAnalyzers
  # defaults to false in BundledVersions.props)
  mkdir -p "$SDK/Sdks/Microsoft.NET.Sdk/analyzers/build"
  for f in props targets; do
    [ -f "$SDK/Sdks/Microsoft.NET.Sdk/analyzers/build/Microsoft.CodeAnalysis.NetAnalyzers.$f" ] || \
      printf '<Project>\n  <!-- m612 bootstrap: placeholder, dotnet/roslyn-analyzers not built -->\n</Project>\n' \
        > "$SDK/Sdks/Microsoft.NET.Sdk/analyzers/build/Microsoft.CodeAnalysis.NetAnalyzers.$f"
  done
  # SDK resolvers
  mkdir -p "$SDK/SdkResolvers/Microsoft.DotNet.MSBuildSdkResolver" "$SDK/SdkResolvers/Microsoft.Build.NuGetSdkResolver"
  cp "$WORK"/sdk/artifacts/bin/Microsoft.DotNet.MSBuildSdkResolver/Release/net8.0/Microsoft.DotNet.MSBuildSdkResolver.dll "$SDK/SdkResolvers/Microsoft.DotNet.MSBuildSdkResolver/"
  printf '<SdkResolver>\n  <Path>..\\..\\Microsoft.Build.NuGetSdkResolver.dll</Path>\n</SdkResolver>\n' \
    > "$SDK/SdkResolvers/Microsoft.Build.NuGetSdkResolver/Microsoft.Build.NuGetSdkResolver.xml"
  # files of the redist layout
  cp "$S1/sdk/$V/Microsoft.NETCoreSdk.BundledVersions.props" "$S1/sdk/$V"/*RuntimeIdentifierGraph.json "$SDK/"
  mkdir -p "$SDK/trustedroots" "$SDK/Current/SolutionFile/ImportAfter"
  cp "$WORK"/sdk/src/Layout/redist/trustedroots/*.pem "$SDK/trustedroots/"
  cp "$WORK"/sdk/src/Layout/redist/MSBuildImports/Current/SolutionFile/ImportAfter/Microsoft.NuGet.ImportAfter.targets "$SDK/Current/SolutionFile/ImportAfter/"
  printf '%s\n%s\n%s\n%s\n' "$(repo_commit "$M612/src/sdk-$V.tar.gz")" "$V" win-x64 "$V" > "$SDK/.version"
  # what built this root
  { echo "m612 S12 stage $STAGE SDK, built by $BUILD_SDK"; date -u; } > "$SDK/.m612-stage"
  echo "layout: $(find "$IL" -type f | wc -l) files, $(du -sh "$IL" | cut -f1)"
}

# no [ReferenceAssembly] DLL may be in the SDK directory (a reference package's lib/ copied by a build)
step_check() {
  CSC_DOTNET=$BUILD_SDK/dotnet.exe net8tool asminfo > /dev/null      # (compiles the tool)
  find "$SDK" -name '*.dll' -size +1k ! -path "$SDK/ref/*" | while read -r f; do m "$f"; done \
    | xargs -d '\n' -n 200 "$BUILD_SDK/dotnet.exe" "$(m "$M612/tools/asminfo.dll")" > "$S12/logs/layout-s$STAGE-asminfo.txt" 2>&1 || true
  if awk '/ mvid=/{f=$1} /ReferenceAssemblyAttribute/{print f}' "$S12/logs/layout-s$STAGE-asminfo.txt" | sort -u | grep . ; then
    die "reference assemblies in the layout (above)"
  fi
  echo "check: $(grep -c ' mvid=' "$S12/logs/layout-s$STAGE-asminfo.txt") managed assemblies, no reference assemblies"
}

# ReadyToRun: the framework of the stage-1 R2R root is the same input (not rebuilt), so it is reused;
# the SDK directory is compiled with crossgen2 (crossgen2.sh r2rsdk; crossgen2 itself is stage 1's)
step_r2r() {
  [ -x "$R2R/dotnet.exe" ] && { "$R2R/dotnet.exe" build-server shutdown > /dev/null 2>&1 || true; }
  rm -rf "${R2R:?}"; cp -r "$IL" "$R2R"
  rm -rf "${R2R:?}/shared"; cp -r "$S1/shared" "$R2R/"
  local t0=$SECONDS
  ROOT=$IL R2RROOT=$R2R bash "$here/crossgen2.sh" r2rsdk
  echo "TIME r2r sdk $((SECONDS - t0)) s; $(du -sh "$R2R" | cut -f1)"
}

for s in $steps; do log "s12 layout (stage $STAGE): $s"; "step_$s"; done
