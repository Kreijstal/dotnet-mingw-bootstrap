#!/bin/bash
# S11: the IL linker (PublishTrimmed) from source: Mono.Cecil 0.11.4 (dotnet/cecil at the commit
# runtime v8.0.0 pins), illink.dll + ILLink.Tasks.dll (runtime 8.0.0 src/tools/illink, net8.0) and
# the ILLink.RoslynAnalyzer (trim/AOT/single-file warnings), packed as Microsoft.NET.ILLink.Tasks
# 8.0.0 -- the package the SDK references implicitly for PublishTrimmed/IsTrimmable (KnownILLinkPack).
# The nupkg goes to <dotnet root>/library-packs, the folder the SDK adds as a restore source
# (Microsoft.NET.NuGetOfflineCache.targets), so no feed has to be configured.
#
# usage: illink-net8.sh [step...]     steps: cecil linker tasks analyzer pack   (default: all)
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
. "$here/../env-m612.sh"
steps=${*:-cecil linker tasks analyzer pack}
CECIL_REV=${CECIL_REV:-45dd3a73dd5b64b010c4251303b3664bb30df029}   # runtime v8.0.0 eng/Version.Details.xml
C=$M612/src/cecil-${CECIL_REV:0:8}
RT=$M612/src/runtime-8.0.0-native
IL=$RT/src/tools/illink
B=$M612/build/illink-net8; mkdir -p "$B/obj"
OUT=$M612/illink-8.0.0-net8
BCSC=${BCSC:-$M612/roslyn-4.8.0/csc.exe}
MSB8=${MSB8:-$M612/msbuild-17.8.49-net8}
R8=${R8:-$M612/roslyn-4.8.0-net8/bincore}
. "$here/common.sh"
. "$here/sdkproj.sh"
SP_OUT=$B/out; SP_GROUP=illink-net8; SP_DEFS=(); SP_REFS=()
RTPROPS=(-p NetCoreAppToolCurrent=net8.0 -p NetCoreAppCurrent=net8.0 -p DotNetBuildFromSource=true -p Configuration=Release
  -p RepoRoot=$RT/ -p LibrariesProjectRoot=$RT/src/libraries/ -p CommonPath=$RT/src/libraries/Common/src/)

step_cecil() {
  if [ ! -d "$C/Mono.Cecil" ]; then
    fetch_tarball "https://github.com/dotnet/cecil/archive/$CECIL_REV.tar.gz" "$M612/src/cecil-${CECIL_REV:0:8}.tar.gz"
    mkdir -p "$C"; tar -C "$C" --strip-components=1 -xzf "$M612/src/cecil-${CECIL_REV:0:8}.tar.gz"
  fi
  [ -f "$C/.deleted-binaries.txt" ] || delete_binaries "$C"
  rm -f "$C/cecil.snk"     # key pair; public-signed with keys/Cecil-* (the Consts.PublicKey text)
  # Mono.Cecil{,.Pdb,.Mdb,.Rocks}: netstandard2.0 upstream, compiled against the .NET 8 reference pack here
  SP_TF=netstandard2.0; SP_KEY=Cecil; SP_VER=0.11.4.0; SP_FILEVER=0.11.4.0; SP_INFOVER=0.11.4-alpha.23509.2
  SP_EVAL=(-p ToolsFramework=netstandard2.0 -p Configuration=Release --pre "$C/Directory.Build.props")
  local p; for p in Mono.Cecil.csproj symbols/pdb/Mono.Cecil.Pdb.csproj symbols/mdb/Mono.Cecil.Mdb.csproj rocks/Mono.Cecil.Rocks.csproj; do
    sdkproj "$C/$p"
  done
}
step_linker() {
  SP_TF=net8.0; SP_KEY=Open; SP_VER=8.0.0.0; SP_FILEVER=8.0.0.0; SP_INFOVER=8.0.0
  SP_EVAL=("${RTPROPS[@]}")
  sdkproj "$IL/src/linker/Mono.Linker.csproj"
}
step_tasks() {
  SP_TF=net8.0; SP_KEY=Open; SP_VER=8.0.0.0; SP_FILEVER=8.0.0.0; SP_INFOVER=8.0.0
  SP_EVAL=("${RTPROPS[@]}" -p NetFrameworkToolCurrent=net472)
  SP_REFS=("$MSB8/Microsoft.Build.Framework.dll" "$MSB8/Microsoft.Build.Utilities.Core.dll" "$MSB8/Microsoft.NET.StringTools.dll")
  sdkproj "$IL/src/ILLink.Tasks/ILLink.Tasks.csproj"
  SP_REFS=()
}
# the trim/AOT/single-file analyzer: a Roslyn component, against the compiler of the SDK (4.8, net8.0 build)
step_analyzer() {
  local o=$SP_OUT; SP_OUT=$B/analyzer
  SP_TF=netstandard2.0; SP_KEY=Open; SP_VER=8.0.0.0; SP_FILEVER=8.0.0.0; SP_INFOVER=8.0.0; SP_GENERATORS=" "
  SP_EVAL=("${RTPROPS[@]}")
  SP_REFS=("$R8/Microsoft.CodeAnalysis.dll" "$R8/Microsoft.CodeAnalysis.CSharp.dll")
  sdkproj "$IL/src/ILLink.RoslynAnalyzer/ILLink.RoslynAnalyzer.csproj"
  SP_OUT=$o; SP_REFS=()
}
step_pack() {
  local P=$OUT/microsoft.net.illink.tasks/8.0.0 f
  rm -rf "${OUT:?}"; mkdir -p "$P/build" "$P/Sdk" "$P/tools/net8.0" "$P/analyzers/dotnet/cs"
  cp "$IL"/src/ILLink.Tasks/build/* "$IL"/src/ILLink.RoslynAnalyzer/build/*.props "$P/build/"
  cp "$IL"/src/ILLink.Tasks/Sdk/Sdk.props "$P/Sdk/"
  for f in ILLink.Tasks illink Mono.Cecil Mono.Cecil.Pdb Mono.Cecil.Mdb Mono.Cecil.Rocks; do cp "$SP_OUT/$f.dll" "$P/tools/net8.0/"; done
  cat > "$P/tools/net8.0/illink.runtimeconfig.json" <<'EOJ'
{
  "runtimeOptions": {
    "tfm": "net8.0",
    "rollForward": "Major",
    "framework": { "name": "Microsoft.NETCore.App", "version": "8.0.0" },
    "configProperties": { "System.GC.Server": true }
  }
}
EOJ
  cp "$B/analyzer/ILLink.RoslynAnalyzer.dll" "$P/analyzers/dotnet/cs/"
  cat > "$P/microsoft.net.illink.tasks.nuspec" <<'EOX'
<?xml version="1.0" encoding="utf-8"?>
<package xmlns="http://schemas.microsoft.com/packaging/2013/05/nuspec.xsd">
  <metadata>
    <id>Microsoft.NET.ILLink.Tasks</id>
    <version>8.0.0</version>
    <authors>Microsoft</authors>
    <description>MSBuild tasks for running the IL Linker (m612 bootstrap: built from dotnet/runtime v8.0.0 and dotnet/cecil sources)</description>
    <developmentDependency>true</developmentDependency>
  </metadata>
</package>
EOX
  python3 -S - "$P" "$OUT/Microsoft.NET.ILLink.Tasks.8.0.0.nupkg" <<'EOPY'
import os, sys, zipfile
src, out = sys.argv[1:3]
with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED) as z:
    for dp, dn, fn in os.walk(src):
        for f in fn:
            full = os.path.join(dp, f); rel = os.path.relpath(full, src).replace('\\', '/')
            z.write(full, 'Microsoft.NET.ILLink.Tasks.nuspec' if rel.endswith('.nuspec') else rel)
print('wrote', out)
EOPY
}

for s in $steps; do log "illink net8.0: $s"; "step_$s"; done
