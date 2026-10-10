#!/bin/bash
# S12: Roslyn 4.8.0 (the compiler SDK 8.0.131 ships) with `dotnet build` of its own project files:
#   prep   extract roslyn-4.8.0 (test binaries deleted) into s<N>/work/roslyn, patches/m612/roslyn-4.8.0,
#          global.json -> our SDK + Arcade, NuGet.config -> local feeds, s12.slnf (the projects below)
#   boot   (stage 2 only, when no real Microsoft.CodeAnalysis.Analyzers is in the feed) a *stub*
#          Microsoft.CodeAnalysis.Analyzers 3.3.4 package with no analyzers in it: CSharpSyntaxGenerator
#          references that analyzer package (PrivateAssets=all), which is built by dotnet/roslyn-analyzers,
#          which itself needs Roslyn (the VMR takes it from the previously-source-built tarball).
#          Analyzers only report diagnostics; the stub changes no output.
#   build  dotnet build + pack of s12.slnf: Microsoft.CodeAnalysis{,.CSharp,.VisualBasic,.Scripting,
#          .CSharp.Scripting,.ExternalAccess.RazorCompiler},
#          csc/vbc/VBCSCompiler/csi, Microsoft.Build.Tasks.CodeAnalysis, the Microsoft.Net.Compilers.Toolset
#          package (what the SDK lays out as Roslyn/ and Roslyn/bincore), the Microsoft.CodeAnalysis.Collections
#          source package (MSBuild compiles it in); version 4.8.0-7.25569.25 (OfficialBuildId 20251119.25)
#          as SDK 8.0.131 pins
# usage: s12-roslyn.sh [step...]   (default: prep boot build)
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
. "$here/../env-m612.sh"
steps=${*:-prep boot build}
B=$M612/s12
. "$here/common.sh"
. "$here/s12-common.sh"
V=4.8.0
R=$WORK/roslyn
TAR=$M612/src/roslyn-$V.tar.gz
BOOT=$SN/boot

PROJECTS="src/Compilers/Core/Portable/Microsoft.CodeAnalysis.csproj
src/Compilers/CSharp/Portable/Microsoft.CodeAnalysis.CSharp.csproj
src/Compilers/VisualBasic/Portable/Microsoft.CodeAnalysis.VisualBasic.vbproj
src/Compilers/CSharp/csc/AnyCpu/csc.csproj
src/Compilers/VisualBasic/vbc/AnyCpu/vbc.csproj
src/Compilers/Server/VBCSCompiler/AnyCpu/VBCSCompiler.csproj
src/Compilers/Core/MSBuildTask/Microsoft.Build.Tasks.CodeAnalysis.csproj
src/Interactive/csi/csi.csproj
src/Scripting/Core/Microsoft.CodeAnalysis.Scripting.csproj
src/Scripting/CSharp/Microsoft.CodeAnalysis.CSharp.Scripting.csproj
src/Tools/ExternalAccess/RazorCompiler/Microsoft.CodeAnalysis.ExternalAccess.RazorCompiler.csproj
src/NuGet/Microsoft.CodeAnalysis.Compilers.Package.csproj
src/NuGet/Microsoft.Net.Compilers.Toolset/AnyCpu/Microsoft.Net.Compilers.Toolset.Package.csproj
src/Dependencies/Collections/Microsoft.CodeAnalysis.Collections.Package.csproj"

step_prep() {
  s12_extract "$TAR" "$R"
  apply_patches "$R" "$here/../../patches/m612/roslyn-$V"
  python3 -S "$M612_TOOLS_SRC/s12globaljson.py" "$R/global.json" 8.0.131 Microsoft.DotNet.Arcade.Sdk=$ARCADE_VER
  python3 -S - "$R/s12.slnf" $PROJECTS <<'EOPY'
import json, sys
json.dump({'solution': {'path': 'Roslyn.sln', 'projects': [p.replace('/', '\\') for p in sys.argv[2:]]}},
          open(sys.argv[1], 'w', newline='\n'), indent=2)
EOPY
}

step_boot() {
  ls "$FEED"/microsoft.codeanalysis.analyzers.*.nupkg > /dev/null 2>&1 && { echo "real analyzers in the feed, no stub"; return 0; }
  local P=$BOOT/analyzers-stub; rm -rf "${P:?}"; mkdir -p "$P/analyzers/dotnet"
  : > "$P/analyzers/dotnet/_._"
  cat > "$P/microsoft.codeanalysis.analyzers.nuspec" <<'EOX'
<?xml version="1.0" encoding="utf-8"?>
<package xmlns="http://schemas.microsoft.com/packaging/2013/05/nuspec.xsd">
  <metadata>
    <id>Microsoft.CodeAnalysis.Analyzers</id>
    <version>3.3.4</version>
    <authors>m612</authors>
    <description>m612 S12 bootstrap stub: no analyzers (the real package is built by s12-analyzers.sh from dotnet/roslyn-analyzers, which needs Roslyn first)</description>
    <developmentDependency>true</developmentDependency>
  </metadata>
</package>
EOX
  mkdir -p "$BOOT/feed"
  python3 -S "$M612_TOOLS_SRC/s12nupkg.py" "$P" "$P/microsoft.codeanalysis.analyzers.nuspec" "$BOOT/feed"
}

# the compiler that builds Roslyn is the building SDK's own (UsingToolMicrosoftNetCompilers=false):
# Roslyn pins a Microsoft.Net.Compilers.Toolset package (4.8.0-1.23419.1, from the previously-source-built
# tarball in the VMR); compiling stage N's Roslyn with stage N-1's is the bootstrap itself
step_build() {
  s12_nugetconfig "$R" "$BOOT/feed"
  s12_build roslyn "$R" s12.slnf -p:OfficialBuildId=20251119.25 -p:RepositoryCommit=$(repo_commit "$TAR") \
    -p:UsingToolMicrosoftNetCompilers=false
}

for s in $steps; do log "s12 roslyn $V (stage $STAGE): $s"; "step_$s"; done
