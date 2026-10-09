#!/bin/bash
# S11: MSBuild as net8.0 assemblies (the flavour the .NET SDK runs: MSBuild.dll
# and its libraries in sdk/<ver>/), from the tree msbuild.sh prepared, with
# direct csc command lines against the from-source reference pack and the
# out-of-band libraries (libraries.sh OOB=: System.Configuration.ConfigurationManager,
# System.Reflection.MetadataLoadContext, System.Resources.Extensions, System.CodeDom,
# System.Security.Cryptography.Pkcs/Xml).  The project files are evaluated for
# TargetFramework=net8.0 with DotNetBuildFromSource=true, as source-build does.
#
# usage: msbuild-net8.sh <version> [step...]
#   steps: stringtools framework utilities build tasks exe install   (default: all)
# Compiler: the net472 Roslyn 4.12 csc.exe on Mono 6.12 (BCSC to override).
# Output: $M612/msbuild-<version>-net8/ (assemblies + targets, the sdk/<ver> part of MSBuild)
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
. "$here/../env-m612.sh"
V=${1:?msbuild version}; shift
steps=${*:-stringtools framework utilities build tasks exe install}
B=$M612/build/msbuild-$V
OUT=$M612/msbuild-$V-net8
BCSC=${BCSC:-$M612/roslyn-4.12.0/csc.exe}
RSRC=${RSRC:-$M612/build/roslyn-4.12.0}      # Microsoft.CodeAnalysis.Collections sources (as for net472)
. "$here/common.sh"
[ -d "$B/src" ] || die "run msbuild.sh $V prep first"
O=$B/obj/out-net8; mkdir -p "$O"

EVAL_PROPS=(-p TargetFramework=net8.0 -p NetCurrent=net8.0 -p LatestDotNetCoreForMSBuild=net8.0 -p FullFrameworkTFM=net472
  -p Configuration=Release -p DotNetBuildFromSource=true -p TargetFrameworkIdentifier=.NETCoreApp -p TargetFrameworkVersion=v8.0)
evalp() {
  local d=$1 p=$2; shift 2
  (cd "$d" && csproj_items "${EVAL_PROPS[@]}" --pre "$B/src/Directory.Build.props" "$p" \
     --post "$B/src/Directory.BeforeCommon.targets" --post "$B/src/Directory.Build.targets" "$@" 2>/dev/null)
}

# build_proj <AssemblyName> <src-subdir> <csproj> [extra csc args...]   (cf. msbuild.sh)
build_proj() {
  local name=$1 sub=$2 proj=$3; shift 3
  local pd="$B/src/$sub" o="$B/obj/net8-$name"; rm -rf "${o:?}"; mkdir -p "$o"
  local items=() res=() defs d f ln
  mapfile -t items < <(evalp "$pd" "$proj" --default-items | sed "s|^|$pd/|")
  defs=$(evalp "$pd" "$proj" --define)
  while IFS=$'\t' read -r f ln; do
    [ -n "$f" ] || continue
    case $f in
      *.resx|*.txt) [ -n "$ln" ] || ln="$name.$(basename "${f%.*}").resources"
         resgen "$pd/$f" "$o/$ln" > /dev/null; res+=(-resource:"$o/$ln") ;;
      *) [ -n "$ln" ] || ln="$name.$(basename "$f")"; res+=(-resource:"$pd/$f,$ln") ;;
    esac
  done < <(evalp "$pd" "$proj" --kind EmbeddedResource --meta LogicalName)
  if [ "$(evalp "$pd" "$proj" --prop EnableDefaultItems | cut -d= -f2)" != false ]; then
    for f in "$pd"/Resources/*.resx; do
      [ -e "$f" ] || continue; ln="$name.$(basename "${f%.resx}").resources"
      printf '%s\n' "${res[@]}" | grep -q "$ln" && continue
      resgen "$f" "$o/$ln" > /dev/null; res+=(-resource:"$o/$ln")
    done
  fi
  local dargs=() dl=(); IFS=';' read -ra dl <<< "$defs"
  for d in "${dl[@]}"; do [ -n "$d" ] && dargs+=("-d:$d"); done
  cat > "$o/AssemblyInfo.g.cs" <<EOF
[assembly: System.Reflection.AssemblyVersion("15.1.0.0")]
[assembly: System.Reflection.AssemblyFileVersion("$V.0")]
[assembly: System.Reflection.AssemblyInformationalVersion("$V+m612.bootstrap.from.source")]
[assembly: System.Runtime.Versioning.TargetFramework(".NETCoreApp,Version=v8.0", FrameworkDisplayName = ".NET 8.0")]
EOF
  NET8_SELF=$name csc "net8-$name" -target:${TARGET:-library} -nostdlib -noconfig -optimize+ -debug:portable -deterministic -unsafe -nullable:enable \
    -langversion:latest -out:"$O/$name.dll" -publicsign -keyfile:"$(keyfile Microsoft)" "${NET8_DEFS[@]}" \
    -nowarn:CS1591,CS1573,CS0618,CS0612,CS8600,CS8601,CS8602,CS8603,CS8604,CS8618,CS8625,CS8767,CS8766,CS8765,CS8769,CS8619,CS8620,CS8605,CS8610,CS8613,CS8614,CS8616,CS8617,CS8621,CS8622,CS8631,CS8633,CS8634,CS8644,CS8714,CS8762,CS8763,CS8768,CS8770,CS8774,CS8775,CS8776,CS8777,CS8824,CS8825,CS8826,CS8847,CS0436,SYSLIB0011,SYSLIB0012,SYSLIB0037,SYSLIB0044 \
    "${dargs[@]}" $(NET8_SELF=$name net8_refs "$O") \
    "$@" "${res[@]}" "$o/AssemblyInfo.g.cs" "${items[@]}" || die "$name failed ($B/obj/net8-$name.log)"
}

step_stringtools() { build_proj Microsoft.NET.StringTools StringTools StringTools.csproj; }
step_framework() {
  local c="$RSRC/src/Dependencies/Collections" o="$B/obj/net8-collections"; mkdir -p "$o"
  resgen "$c/Internal/Strings.resx" "$o/Microsoft.CodeAnalysis.Collections.Internal.Strings.resources" > /dev/null
  resxgen "$c/Internal/Strings.resx" "$o/CollectionsSR.g.cs" Microsoft.CodeAnalysis.Collections.Internal.SR \
    --resource-name Microsoft.CodeAnalysis.Collections.Internal.Strings
  build_proj Microsoft.Build.Framework Framework Microsoft.Build.Framework.csproj -d:MICROSOFT_CODEANALYSIS_COLLECTIONS_NO_PUBLIC \
    $(find "$c" -name '*.cs' ! -name '*.Package.*') "$o/CollectionsSR.g.cs" -resource:"$o/Microsoft.CodeAnalysis.Collections.Internal.Strings.resources"
}
step_utilities() { build_proj Microsoft.Build.Utilities.Core Utilities Microsoft.Build.Utilities.csproj; }
step_build() { build_proj Microsoft.Build Build Microsoft.Build.csproj; }
step_tasks() { build_proj Microsoft.Build.Tasks.Core Tasks Microsoft.Build.Tasks.csproj; }
step_exe() { TARGET=exe build_proj MSBuild MSBuild MSBuild.csproj; }

# the MSBuild part of sdk/<ver>/: assemblies, targets, MSBuild.runtimeconfig.json
step_install() {
  local f
  rm -rf "${OUT:?}"; mkdir -p "$OUT/Current"
  cp "$O"/*.dll "$O"/*.pdb "$OUT/"
  cp "$B"/src/Tasks/*.targets "$B"/src/Tasks/*.props "$B"/src/Tasks/*.tasks "$B"/src/Tasks/*.overridetasks "$OUT/"
  cp "$B/src/Tasks/Microsoft.Common.props" "$OUT/Current/"
  cp "$B/src/MSBuild/MSBuild.rsp" "$OUT/" 2>/dev/null || true
  # XSDs for the schema (MSBuild.xsd, Microsoft.Build.*.xsd) as upstream ships them
  cp "$B"/src/MSBuild/*.xsd "$OUT/" 2>/dev/null || true
  mkdir -p "$OUT/MSBuild"; cp "$B"/src/MSBuild/MSBuild/*.xsd "$OUT/MSBuild/" 2>/dev/null || true
  # the out-of-band libraries MSBuild needs at run time (not in Microsoft.NETCore.App)
  for f in System.Configuration.ConfigurationManager System.Diagnostics.EventLog System.Security.Cryptography.ProtectedData \
      System.Reflection.MetadataLoadContext System.Resources.Extensions System.CodeDom System.Security.Cryptography.Pkcs System.Security.Cryptography.Xml; do
    cp "$NET8_OOB/$f.dll" "$OUT/"
  done
  cat > "$OUT/MSBuild.runtimeconfig.json" <<'EOJ'
{
  "runtimeOptions": {
    "tfm": "net8.0",
    "framework": { "name": "Microsoft.NETCore.App", "version": "8.0.0" },
    "configProperties": {
      "System.GC.Concurrent": false,
      "System.Runtime.TieredPGO": true,
      "System.Runtime.Serialization.EnableUnsafeBinaryFormatterSerialization": true
    }
  }
}
EOJ
  ls "$OUT" | wc -l
}

for s in $steps; do log "msbuild $V net8.0: $s"; "step_$s"; done
