#!/bin/bash
# S11: the Roslyn compilers as net8.0 assemblies (what the .NET SDK ships in
# sdk/<ver>/Roslyn/bincore: csc.dll, vbc.dll, VBCSCompiler.dll and the
# Microsoft.CodeAnalysis* libraries), compiled against the from-source
# reference pack (refpack.sh) with direct csc/vbc command lines, from the
# Roslyn tree that roslyn.sh + vbc.sh prepared (resources and regenerated
# syntax/bound-tree/error-facts sources are reused from there).  Plus the
# MSBuild integration (Microsoft.Build.Tasks.CodeAnalysis, net8.0) against the
# net8.0 MSBuild of msbuild-net8.sh.
#
# usage: roslyn-net8.sh <roslyn-version> [step...]
#   steps: core csharp vb razor csc vbc server tasks install   (default: all but tasks)
#   compilers: the net472 csc.exe/vbc.exe of the same version (roslyn.sh, vbc.sh) on Mono 6.12
# Output: $M612/roslyn-<version>-net8/{bincore/,tasks/}
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
. "$here/../env-m612.sh"
V=${1:?roslyn version}; shift
steps=${*:-core csharp vb razor csc vbc server install}
B=$M612/build/roslyn-$V
BCSC=$M612/roslyn-$V/csc.exe
VBC=$M612/roslyn-$V/vbc.exe
OUT=$M612/roslyn-$V-net8
MSB8=${MSB8:-$M612/msbuild-17.8.49-net8}
. "$here/common.sh"
[ -f "$B/obj/Microsoft.CodeAnalysis.dll" ] && [ -f "$B/obj/Microsoft.CodeAnalysis.VisualBasic.dll" ] || die "run roslyn.sh $V and vbc.sh $V first"
O=$B/obj/net8; mkdir -p "$O"
COMMON=(-nostdlib -noconfig -optimize+ -debug:portable -deterministic -nullable:enable -unsafe "${NET8_DEFS[@]}")
SIGN=(-publicsign -keyfile:"$(keyfile MSShared)")
IVT_PK=$(tr -d '\n\r' < "$KEYS"/MSShared-*.pubkey.hex)
ivt() { local f=$1; shift; : > "$f"; for a in "$@"; do echo "[assembly: System.Runtime.CompilerServices.InternalsVisibleTo(\"$a, PublicKey=$IVT_PK\")]" >> "$f"; done; }
ver() { printf '[assembly: System.Reflection.AssemblyVersion("%s")]\n[assembly: System.Reflection.AssemblyFileVersion("%s")]\n[assembly: System.Reflection.AssemblyInformationalVersion("%s (m612 bootstrap, net8.0)")]\n[assembly: System.Runtime.Versioning.TargetFramework(".NETCoreApp,Version=v8.0", FrameworkDisplayName = ".NET 8.0")]\n' "${V%.*}.0.0" "$V.0" "$V" > "$1"; }
refs() { net8_refs "$@"; }

step_core() {
  cd "$B"
  ivt "$O/core-ivt.cs" Microsoft.CodeAnalysis.CSharp Microsoft.CodeAnalysis.VisualBasic Microsoft.CodeAnalysis.Scripting \
    Microsoft.CodeAnalysis.CSharp.Scripting csc csi vbc VBCSCompiler Microsoft.Build.Tasks.CodeAnalysis Microsoft.CodeAnalysis.ExternalAccess.RazorCompiler
  ver "$O/core-ver.cs"
  local extra=(); [ -f obj/CollectionsStrings.g.cs ] && extra+=(obj/CollectionsStrings.g.cs -resource:obj/Microsoft.CodeAnalysis.Collections.Internal.Strings.resources)
  csc net8-core "${COMMON[@]}" "${SIGN[@]}" -target:library -out:"$O/Microsoft.CodeAnalysis.dll" -d:COMPILERCORE $(refs) \
    -resource:obj/Microsoft.CodeAnalysis.CodeAnalysisResources.resources \
    -resource:src/Compilers/Core/Portable/Resources/default.win32manifest,Microsoft.CodeAnalysis.Resources.default.win32manifest \
    obj/CodeAnalysisResources.g.cs "$O/core-ivt.cs" "$O/core-ver.cs" "${extra[@]}" \
    $(find src/Compilers/Core/Portable src/Compilers/Core/AnalyzerDriver src/Dependencies/CodeAnalysis.Debugging \
           src/Dependencies/PooledObjects src/Dependencies/Collections $(ls -d src/Dependencies/Contracts 2>/dev/null) \
           -name '*.cs' ! -path '*/obj/*' ! -name '*.Package.*') \
    || die "Microsoft.CodeAnalysis (net8.0) failed ($B/obj/net8-core.log)"
}

step_csharp() {
  cd "$B"
  ivt "$O/cs-ivt.cs" csc csi Microsoft.CodeAnalysis.CSharp.Scripting VBCSCompiler
  ver "$O/cs-ver.cs"
  csc net8-csharp "${COMMON[@]}" "${SIGN[@]}" -target:library -out:"$O/Microsoft.CodeAnalysis.CSharp.dll" $(refs "$O/Microsoft.CodeAnalysis.dll") \
    -resource:obj/Microsoft.CodeAnalysis.CSharp.CSharpResources.resources \
    obj/CSharpResources.g.cs "$O/cs-ivt.cs" "$O/cs-ver.cs" \
    $(find src/Compilers/CSharp/Portable src/Compilers/CSharp/CSharpAnalyzerDriver -name '*.cs') \
    $(find obj/gen -name '*.cs') || die "Microsoft.CodeAnalysis.CSharp (net8.0) failed ($B/obj/net8-csharp.log)"
}

# Microsoft.CodeAnalysis.VisualBasic: VB, compiled by the from-source vbc.exe (vbc.sh) with the
# sources/resources vbc.sh generated; same options as vbc.sh plus the .NETCoreApp defines
step_vb() {
  cd "$B"
  local P=src/Compilers/VisualBasic/Portable GD=src/Compilers/VisualBasic/Portable/Generated
  printf 'Imports System.Runtime.CompilerServices\n' > "$O/vb-attr.vb"
  local a; for a in vbc vbi VBCSCompiler Microsoft.CodeAnalysis.VisualBasic.Scripting; do
    echo "<Assembly: InternalsVisibleTo(\"$a, PublicKey=$IVT_PK\")>" >> "$O/vb-attr.vb"; done
  printf '<Assembly: System.Reflection.AssemblyVersion("%s")>\n<Assembly: System.Reflection.AssemblyFileVersion("%s")>\n<Assembly: System.Reflection.AssemblyInformationalVersion("%s (m612 bootstrap, net8.0)")>\n' \
    "${V%.*}.0.0" "$V.0" "$V" >> "$O/vb-attr.vb"
  local emb=() f; for f in Embedded InternalXmlHelper VbCoreSourceText VbMyTemplateText; do
    emb+=(-resource:$P/Symbols/EmbeddedSymbols/$f.vb,$f.vb); done
  # -vbruntime* embeds the VB runtime helpers (VBRuntime=Embed); Microsoft.VisualBasic(.Core) of the
  # pack is not referenced, else its modules (Err, Loc, ...) bind before the compiler's own names
  local rsp=$O/vb.rsp; : > "$rsp"
  for a in -target:library -out:"$O/Microsoft.CodeAnalysis.VisualBasic.dll" -nostdlib -optimize+ -debug:portable -deterministic \
      -optionstrict+ -optioninfer+ -optionexplicit+ -optioncompare:binary -langversion:latest -removeintchecks+ -vbruntime* \
      -nowarn:40057,42014,42016,41999,42017,42018,42019,42032,42036,42020,42021,42022,42353,42354,42355 \
      "-define:TRACE=True,RELEASE=True,NETCOREAPP=True,NET=True,NET8_0=True,NET8_0_OR_GREATER=True,NET7_0_OR_GREATER=True,NET6_0_OR_GREATER=True,NET5_0_OR_GREATER=True,NETCOREAPP3_1_OR_GREATER=True,NETCOREAPP3_0_OR_GREATER=True,NETCOREAPP2_0_OR_GREATER=True" \
      "${SIGN[@]}" -imports:Microsoft.VisualBasic,System,System.Collections,System.Collections.Generic,System.Diagnostics,System.Linq,System.Threading.Tasks,"IdentifierComparison = Microsoft.CodeAnalysis.CaseInsensitiveComparison",Roslyn.Utilities \
      $(refs "$O/Microsoft.CodeAnalysis.dll" | grep -v '/Microsoft\.VisualBasic') -resource:obj/Microsoft.CodeAnalysis.VisualBasic.VBResources.resources "${emb[@]}" \
      obj/VBResources.g.vb "$O/vb-attr.vb" obj/vbgen/Syntax.xml.*.Generated.vb obj/vbgen/BoundNodes.xml.Generated.vb obj/vbgen/ErrorFacts.Generated.vb \
      $(find src/Compilers/VisualBasic/BasicAnalyzerDriver -name '*.vb') $(find $P -name '*.vb' ! -path "$GD/*" ! -path "$P/Symbols/EmbeddedSymbols/Embedded.vb" ! -path "$P/Symbols/EmbeddedSymbols/InternalXmlHelper.vb" \
        ! -path "$P/Symbols/EmbeddedSymbols/VbCoreSourceText.vb" ! -path "$P/Symbols/EmbeddedSymbols/VbMyTemplateText.vb"); do
    if [[ $a =~ ^(-[A-Za-z]+):(/.*)$ ]]; then a="${BASH_REMATCH[1]}:$(winpath "${BASH_REMATCH[2]}")"
    elif [[ $a == /* ]]; then a=$(winpath "$a"); fi
    printf '"%s"\n' "$a" >> "$rsp"
  done
  local rc=0; mono "$VBC" -nologo -noconfig @"$(winpath "$rsp")" > "$B/obj/net8-vb.log" 2>&1 || rc=$?
  grep -E 'error BC' "$B/obj/net8-vb.log" | head -30 || true
  echo "vbc net8-vb: rc=$rc, $(grep -c 'error BC' "$B/obj/net8-vb.log" || true) errors"
  [ $rc = 0 ] || die "Microsoft.CodeAnalysis.VisualBasic (net8.0) failed"
}

# Microsoft.CodeAnalysis.ExternalAccess.RazorCompiler (csc/VBCSCompiler ship it for the Razor generator)
step_razor() {
  cd "$B"
  local d=src/Tools/ExternalAccess/RazorCompiler; ver "$O/razor-ver.cs"
  csc net8-razor "${COMMON[@]}" "${SIGN[@]}" -target:library -out:"$O/Microsoft.CodeAnalysis.ExternalAccess.RazorCompiler.dll" \
    $(refs "$O/Microsoft.CodeAnalysis.dll" "$O/Microsoft.CodeAnalysis.CSharp.dll") "$O/razor-ver.cs" $(find $d -name '*.cs') \
    || die "RazorCompiler (net8.0) failed"
}

cmdline_items() { CSPROJ_NO_FOLLOW=1 csproj_items -p TargetFramework=net8.0 "$1" 2>/dev/null; }
step_csc() {
  cd "$B"; ver "$O/csc-ver.cs"
  csc net8-csc "${COMMON[@]}" "${SIGN[@]}" -target:exe -out:"$O/csc.dll" -main:Microsoft.CodeAnalysis.CSharp.CommandLine.Program \
    $(refs "$O/Microsoft.CodeAnalysis.dll" "$O/Microsoft.CodeAnalysis.CSharp.dll") "$O/csc-ver.cs" \
    $(cmdline_items src/Compilers/CSharp/csc/CscCommandLine.projitems) || die "csc.dll failed"
}
step_vbc() {
  cd "$B"; ver "$O/vbc-ver.cs"
  csc net8-vbc "${COMMON[@]}" "${SIGN[@]}" -target:exe -out:"$O/vbc.dll" -main:Microsoft.CodeAnalysis.VisualBasic.CommandLine.Program \
    $(refs "$O/Microsoft.CodeAnalysis.dll" "$O/Microsoft.CodeAnalysis.VisualBasic.dll") "$O/vbc-ver.cs" \
    $(cmdline_items src/Compilers/VisualBasic/vbc/VbcCommandLine.projitems) || die "vbc.dll failed"
}
step_server() {
  cd "$B"; ver "$O/server-ver.cs"
  csc net8-server "${COMMON[@]}" "${SIGN[@]}" -target:exe -out:"$O/VBCSCompiler.dll" -main:Microsoft.CodeAnalysis.CompilerServer.VBCSCompiler \
    $(refs "$O/Microsoft.CodeAnalysis.dll" "$O/Microsoft.CodeAnalysis.CSharp.dll" "$O/Microsoft.CodeAnalysis.VisualBasic.dll") "$O/server-ver.cs" \
    $(cmdline_items src/Compilers/Server/VBCSCompiler/VBCSCompilerCommandLine.projitems) || die "VBCSCompiler.dll failed"
}

# Microsoft.Build.Tasks.CodeAnalysis (Csc/Vbc tasks) for MSBuild on .NET; needs msbuild-net8.sh
step_tasks() {
  cd "$B"
  local pd=src/Compilers/Core/MSBuildTask items=()
  [ -f "$MSB8/Microsoft.Build.Framework.dll" ] || die "no net8.0 MSBuild in $MSB8 (msbuild-net8.sh)"
  mapfile -t items < <(cd "$pd" && csproj_items --default-items -p TargetFramework=net8.0 Microsoft.Build.Tasks.CodeAnalysis.csproj 2>/dev/null | sed "s|^|$pd/|")
  resgen "$pd/ErrorString.resx" "$O/Microsoft.CodeAnalysis.BuildTasks.ErrorString.resources" > /dev/null
  resxgen "$pd/ErrorString.resx" "$O/ErrorString.g.cs" Microsoft.CodeAnalysis.BuildTasks.ErrorString --format
  ver "$O/tasks-ver.cs"
  csc net8-tasks "${COMMON[@]}" "${SIGN[@]}" -target:library -out:"$O/Microsoft.Build.Tasks.CodeAnalysis.dll" \
    -nowarn:CS8600,CS8601,CS8602,CS8603,CS8604,CS8618,CS8625 \
    $(refs "$MSB8/Microsoft.Build.Framework.dll" "$MSB8/Microsoft.Build.Utilities.Core.dll" "$MSB8/Microsoft.Build.Tasks.Core.dll" "$MSB8/Microsoft.NET.StringTools.dll") \
    -resource:"$O/Microsoft.CodeAnalysis.BuildTasks.ErrorString.resources" "$O/ErrorString.g.cs" "$O/tasks-ver.cs" "${items[@]}" \
    || die "Microsoft.Build.Tasks.CodeAnalysis (net8.0) failed"
  printf '<Project>\n  <PropertyGroup>\n    <CompilerApiVersion>roslyn%s</CompilerApiVersion>\n  </PropertyGroup>\n</Project>\n' "${V%.*}" > "$O/Microsoft.Managed.Core.CurrentVersions.targets"
  mkdir -p "$OUT/tasks"
  cp "$O"/Microsoft.Build.Tasks.CodeAnalysis.{dll,pdb} "$O/Microsoft.Managed.Core.CurrentVersions.targets" "$pd"/*.targets "$OUT/tasks/"
}

# bincore layout of the SDK: <app>.dll + <app>.runtimeconfig.json for each entry point
runtimeconfig() { # runtimeconfig <file> [gcServer]
  cat > "$1" <<EOJ
{
  "runtimeOptions": {
    "tfm": "net8.0",
    "framework": { "name": "Microsoft.NETCore.App", "version": "8.0.0" },
    "rollForward": "Major",
    "configProperties": { "System.GC.Server": ${2:-false}, "System.Globalization.Invariant": false }
  }
}
EOJ
}
step_install() {
  rm -rf "${OUT:?}/bincore"; mkdir -p "$OUT/bincore"
  local f; for f in Microsoft.CodeAnalysis Microsoft.CodeAnalysis.CSharp Microsoft.CodeAnalysis.VisualBasic \
      Microsoft.CodeAnalysis.ExternalAccess.RazorCompiler csc vbc VBCSCompiler; do
    cp "$O/$f.dll" "$OUT/bincore/"; cp "$O/$f.pdb" "$OUT/bincore/" 2>/dev/null || true
  done
  runtimeconfig "$OUT/bincore/csc.runtimeconfig.json" true
  runtimeconfig "$OUT/bincore/vbc.runtimeconfig.json" true
  runtimeconfig "$OUT/bincore/VBCSCompiler.runtimeconfig.json" true
  ls -la "$OUT/bincore"
}

for s in $steps; do log "roslyn $V net8.0: $s"; "step_$s"; done
