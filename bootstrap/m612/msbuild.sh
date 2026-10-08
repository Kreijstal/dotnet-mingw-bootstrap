#!/bin/bash
# Build MSBuild (net472 flavour, MonoBuild=true) from source with direct csc
# command lines on the Mono 6.12 seed, and lay it out as a runnable toolset.
#
# usage: msbuild.sh <version> <csc.exe> <roslyn-src-version> [step...]
#   steps: prep stringtools framework utilities build tasks exe roslyntask install test
# Needs: $M612/pkgs (netfx-pkgs.sh: SCI/SRM/STJ/Dataflow/MLC/Resources.Extensions ...)
#        the Roslyn source tree build/roslyn-<roslyn-src-version> (for
#        Microsoft.CodeAnalysis.Collections sources and the Csc task).
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
. "$here/../env-m612.sh"
V=${1:?msbuild version}; BCSC=${2:?csc.exe}; RV=${3:?roslyn version}; shift 3
steps=${*:-prep stringtools framework utilities build tasks exe roslyntask install test}
SRC=$M612/src/msbuild-$V.tar.gz
B=$M612/build/msbuild-$V
OUT=$M612/msbuild-$V
P=$here/../../patches/m612/msbuild-$V
PKGDIR=${PKGDIR:-$M612/pkgs}
RSRC=$M612/build/roslyn-$RV
. "$here/common.sh"
O=$B/obj/out           # compiled assemblies
mkdir -p "$M612/src"

step_prep() {
  fetch_tarball "https://github.com/dotnet/msbuild/archive/refs/tags/v$V.tar.gz" "$SRC"
  rm -rf "${B:?}"; mkdir -p "$B"
  tar -C "$B" --strip-components=1 -xzf "$SRC"
  delete_binaries "$B"
  apply_patches "$B" "$P"
  mkdir -p "$O"
}

EVAL_PROPS=(-p TargetFramework=net472 -p MonoBuild=true -p FullFrameworkTFM=net472 -p Configuration=Release
  -p TargetFrameworkIdentifier=.NETFramework -p TargetFrameworkVersion=v4.7.2)
evalp() { # evalp <projdir> <csproj> args... (MSBuild repo conventions: src/Directory.Build.props/BeforeCommon/targets)
  local d=$1 p=$2; shift 2
  (cd "$d" && csproj_items "${EVAL_PROPS[@]}" --pre "$B/src/Directory.Build.props" "$p" \
     --post "$B/src/Directory.BeforeCommon.targets" --post "$B/src/Directory.Build.targets" "$@" 2>/dev/null)
}

# all package assemblies we built from source + the netfx reference facades
pkgrefs() { local f; for f in "$PKGDIR"/*.dll; do echo "-r:$(winpath "$f")"; done; }
FWREFS=("${NETFX_REFS[@]}" -r:"$RA/System.Xaml.dll" -r:"$RA/System.Net.Http.dll" -r:"$RA/System.Windows.Forms.dll"
  -r:"$RA/WindowsBase.dll" -r:"$RA/System.Drawing.dll" -r:"$RA/Microsoft.CSharp.dll" -r:"$RA/System.Data.dll"
  -r:"$RA/System.Deployment.dll" -r:"$RA/System.Design.dll" -r:"$RA/System.Web.dll" -r:"$RA/System.Xml.Serialization.dll"
  -r:"$RA/System.Reflection.Context.dll" -r:"$RA/System.ComponentModel.Composition.dll" -r:"$RA/System.Runtime.Remoting.dll"
  -r:"$RA/System.Transactions.dll" -r:"$RA/System.IO.Compression.FileSystem.dll" -r:"$RA/System.ServiceModel.dll" -r:"$RA/Facades/System.Threading.dll"
  -r:"$RA/Facades/System.Runtime.InteropServices.RuntimeInformation.dll" -r:"$RA/Facades/System.IO.dll" -r:"$RA/Facades/System.Reflection.dll"
  -r:"$RA/Facades/System.Runtime.Extensions.dll" -r:"$RA/Facades/System.Linq.dll" -r:"$RA/Facades/System.Text.Encoding.dll")
MSB_PK=$(tr -d '\n\r' < "$KEYS"/Microsoft-*.pubkey.hex)

# build_proj <AssemblyName> <src-subdir> <csproj> [extra csc args...]
build_proj() {
  local name=$1 sub=$2 proj=$3; shift 3
  local pd="$B/src/$sub" o="$B/obj/$name"; rm -rf "${o:?}"; mkdir -p "$o"
  local items=() res=() defs extra=() d
  mapfile -t items < <(evalp "$pd" "$proj" --default-items | sed "s|^|$pd/|")
  defs=$(evalp "$pd" "$proj" --define)
  # resources: LogicalName if given, else $(AssemblyName).<FileName>.resources (MSBuild repo convention)
  local line f ln
  while IFS=$'\t' read -r f ln; do
    [ -n "$f" ] || continue
    case $f in
      *.resx|*.txt) [ -n "$ln" ] || ln="$name.$(basename "${f%.*}").resources"
         resgen "$pd/$f" "$o/$ln" > /dev/null; res+=(-resource:"$o/$ln") ;;
      *) [ -n "$ln" ] || ln="$name.$(basename "$f")"; res+=(-resource:"$pd/$f,$ln") ;;
    esac
  done < <(evalp "$pd" "$proj" --kind EmbeddedResource --meta LogicalName)
  # default EmbeddedResource glob for projects with default items
  if [ "$(evalp "$pd" "$proj" --prop EnableDefaultItems | cut -d= -f2)" != false ]; then
    for f in "$pd"/Resources/*.resx; do
      [ -e "$f" ] || continue; ln="$name.$(basename "${f%.resx}").resources"
      printf '%s\n' "${res[@]}" | grep -q "$ln" && continue
      resgen "$f" "$o/$ln" > /dev/null; res+=(-resource:"$o/$ln")
    done
  fi
  local dargs=() dl=(); IFS=';' read -ra dl <<< "$defs"
  for d in "${dl[@]}"; do
    case $d in ''|FEATURE_REPORTFILEACCESSES) continue ;; esac   # needs the prebuilt-only Microsoft.BuildXL.Processes
    dargs+=("-d:$d"); done
  # MonoBuild drops Microsoft.IO.Redist, but newer code (TerminalLogger, Xaml
  # task factory) uses it unconditionally on .NETFramework; we build the
  # package from source (netfx-pkgs.sh), so use it like upstream netfx does.
  dargs+=(-d:FEATURE_MSIOREDIST)
  cat > "$o/AssemblyInfo.g.cs" <<EOF
[assembly: System.Reflection.AssemblyVersion("15.1.0.0")]
[assembly: System.Reflection.AssemblyFileVersion("$V.0")]
[assembly: System.Reflection.AssemblyInformationalVersion("$V+m612.bootstrap.from.source")]
EOF
  csc "$name" -target:${TARGET:-library} -nostdlib -optimize+ -debug:portable -deterministic -unsafe -nullable:enable \
    -langversion:latest -out:"$O/$name.${EXT:-dll}" -publicsign -keyfile:"$(keyfile Microsoft)" \
    -d:TRACE -d:NETFRAMEWORK -d:NET472 -d:NET472_OR_GREATER -d:NET471_OR_GREATER -d:NET47_OR_GREATER -d:NET462_OR_GREATER -d:NET46_OR_GREATER -d:NET45_OR_GREATER \
    -nowarn:CS1591,CS1573,CS0618,CS0612,CS8600,CS8601,CS8602,CS8603,CS8604,CS8618,CS8625,CS8767,CS8766,CS8765,CS8769,CS8619,CS8620,CS8605,CS8610,CS8613,CS8614,CS8616,CS8617,CS8621,CS8622,CS8631,CS8633,CS8634,CS8644,CS8714,CS8762,CS8763,CS8768,CS8770,CS8774,CS8775,CS8776,CS8777,CS8824,CS8825,CS8826,CS8847,CS0436 \
    "${dargs[@]}" "${FWREFS[@]}" $(pkgrefs) $(for f in "$O"/*.dll; do [ -e "$f" ] && [ "$(basename "$f" .dll)" != "$name" ] && echo "-r:$(winpath "$f")"; done) \
    "$@" "${res[@]}" "$o/AssemblyInfo.g.cs" "${items[@]}" || die "$name failed ($B/obj/$name.log)"
}

ivt_attrs() { # ivt_attrs <file> <assembly>... : InternalsVisibleTo with the MSBuild public key
  local f=$1 a; shift; : > "$f"
  for a in "$@"; do echo "[assembly: System.Runtime.CompilerServices.InternalsVisibleTo(\"$a, PublicKey=$MSB_PK\")]" >> "$f"; done
}

step_stringtools() { build_proj Microsoft.NET.StringTools StringTools StringTools.csproj; }
step_framework() {
  # Microsoft.CodeAnalysis.Collections is a source-only package: compile the
  # sources from the Roslyn tree we built (same as what the package ships).
  local c="$RSRC/src/Dependencies/Collections" o="$B/obj/collections"; mkdir -p "$o"
  resgen "$c/Internal/Strings.resx" "$o/Microsoft.CodeAnalysis.Collections.Internal.Strings.resources" > /dev/null
  resxgen "$c/Internal/Strings.resx" "$o/CollectionsSR.g.cs" Microsoft.CodeAnalysis.Collections.Internal.SR \
    --resource-name Microsoft.CodeAnalysis.Collections.Internal.Strings
  build_proj Microsoft.Build.Framework Framework Microsoft.Build.Framework.csproj -d:MICROSOFT_CODEANALYSIS_COLLECTIONS_NO_PUBLIC \
    $(find "$c" -name '*.cs' ! -name '*.Package.*') "$o/CollectionsSR.g.cs" -resource:"$o/Microsoft.CodeAnalysis.Collections.Internal.Strings.resources"
}
step_utilities() { build_proj Microsoft.Build.Utilities.Core Utilities Microsoft.Build.Utilities.csproj; }
step_build() { build_proj Microsoft.Build Build Microsoft.Build.csproj; }
step_tasks() { build_proj Microsoft.Build.Tasks.Core Tasks Microsoft.Build.Tasks.csproj; }
step_exe() { TARGET=exe EXT=exe build_proj MSBuild MSBuild MSBuild.csproj; }
# Roslyn's MSBuild integration (Csc task + Microsoft.CSharp.Core.targets),
# from the same Roslyn source tree as the compiler we ship with this MSBuild.
step_roslyntask() {
  local pd="$RSRC/src/Compilers/Core/MSBuildTask" o="$B/obj/roslyntask" items=()
  rm -rf "${o:?}"; mkdir -p "$o"
  mapfile -t items < <(cd "$pd" && csproj_items --default-items -p TargetFramework=net472 Microsoft.Build.Tasks.CodeAnalysis.csproj 2>/dev/null | sed "s|^|$pd/|")
  resgen "$pd/ErrorString.resx" "$o/Microsoft.CodeAnalysis.BuildTasks.ErrorString.resources" > /dev/null
  resxgen "$pd/ErrorString.resx" "$o/ErrorString.g.cs" Microsoft.CodeAnalysis.BuildTasks.ErrorString --format
  local rv=${RV%.*}.0.0
  printf '[assembly: System.Reflection.AssemblyVersion("%s")]\n[assembly: System.Reflection.AssemblyFileVersion("%s")]\n[assembly: System.Reflection.AssemblyInformationalVersion("%s+m612.bootstrap.from.source")]\n' "$rv" "$RV.0" "$RV" > "$o/AssemblyInfo.g.cs"
  csc Microsoft.Build.Tasks.CodeAnalysis -target:library -nostdlib -optimize+ -debug:portable -deterministic -unsafe -nullable:enable \
    -out:"$O/Microsoft.Build.Tasks.CodeAnalysis.dll" -publicsign -keyfile:"$(keyfile MSShared)" \
    -d:TRACE -d:NETFRAMEWORK -d:NET472 -d:NET472_OR_GREATER -nowarn:CS8600,CS8601,CS8602,CS8603,CS8604,CS8618,CS8625 \
    "${FWREFS[@]}" $(pkgrefs) -r:"$O/Microsoft.Build.Framework.dll" -r:"$O/Microsoft.Build.Utilities.Core.dll" -r:"$O/Microsoft.Build.Tasks.Core.dll" \
    -resource:"$o/Microsoft.CodeAnalysis.BuildTasks.ErrorString.resources" "$o/ErrorString.g.cs" "$o/AssemblyInfo.g.cs" "${items[@]}" \
    || die "Microsoft.Build.Tasks.CodeAnalysis failed"
  printf '<Project>\n  <PropertyGroup>\n    <CompilerApiVersion>roslyn%s</CompilerApiVersion>\n  </PropertyGroup>\n</Project>\n' "${RV%.*}" > "$o/Microsoft.Managed.Core.CurrentVersions.targets"
}
# Layout like Visual Studio's: $OUT/MSBuild/Current/Bin/MSBuild.exe, Roslyn in Bin/Roslyn
step_install() {
  local root="$OUT/MSBuild" bin="$OUT/MSBuild/Current/Bin" f
  rm -rf "${OUT:?}"; mkdir -p "$bin/Roslyn"
  cp "$O"/*.dll "$O"/*.pdb "$O"/MSBuild.exe "$bin/"
  mv "$bin/Microsoft.Build.Tasks.CodeAnalysis.dll" "$bin/Microsoft.Build.Tasks.CodeAnalysis.pdb" "$bin/Roslyn/"
  for f in "$PKGDIR"/*.dll; do cp "$f" "$bin/"; done
  # (the Microsoft.Common.CurrentVersion.targets change of patch 0002 is a source edit)
  cp "$B"/src/Tasks/*.targets "$B"/src/Tasks/*.props "$B"/src/Tasks/*.tasks "$B"/src/Tasks/*.overridetasks "$bin/"
  cp "$B/src/MSBuild/MSBuild.rsp" "$bin/" 2>/dev/null || true
  # Microsoft.Common.props is also looked up as $(MSBuildExtensionsPath)\$(MSBuildToolsVersion)\Microsoft.Common.props
  cp "$B/src/Tasks/Microsoft.Common.props" "$root/Current/"
  # Roslyn: compiler + MSBuild integration
  local R=$M612/roslyn-$RV
  cp "$R"/*.dll "$R"/csc.exe "$R"/csc.exe.config "$R"/csc.rsp "$bin/Roslyn/"
  cp "$RSRC"/src/Compilers/Core/MSBuildTask/*.targets "$B/obj/roslyntask/Microsoft.Managed.Core.CurrentVersions.targets" "$bin/Roslyn/"
  # The Csc task starts csc.exe as a native process; on Windows that would be
  # Microsoft's .NET Framework, so route it through the seed mono instead.
  printf '@"%s" "%%~dp0csc.exe" %%*\r\n' "$(cygpath -w "$MINGW_PREFIX/bin/mono.exe")" > "$bin/Roslyn/csc-mono.cmd"
  # binding redirects -> the versions we built; toolset defaults for Mono
  : > "$B/obj/versions.txt"
  for f in "$bin"/*.dll; do
    echo "$(basename "$f" .dll) $(monodis --assembly "$f" | sed -n 's/^Version: *//p' | tr -d '\r')" >> "$B/obj/versions.txt"
  done
  python3 -S "$here/tools/fix_config.py" "$B/src/MSBuild/app.config" "$bin/MSBuild.exe.config" "$B/obj/versions.txt" \
    "CscToolExe=csc-mono.cmd" "UseSharedCompilation=false" \
    'CscToolPath=$([MSBuild]::GetToolsDirectory32())\Roslyn' 'MSBuildToolsPath=$([MSBuild]::GetToolsDirectory32())' 'MSBuildToolsPath64=$([MSBuild]::GetToolsDirectory32())' \
    "TargetFrameworkRootPath=$(cygpath -w "$MONO_LIB/../xbuild-frameworks")\\" \
    "MSBuildFrameworkToolsPath=$(cygpath -w "$MONO_LIB")\\" "MSBuildFrameworkToolsPath32=$(cygpath -w "$MONO_LIB")\\" \
    "MSBuildFrameworkToolsPath64=$(cygpath -w "$MONO_LIB")\\" "MSBuildFrameworkToolsPathArm64=$(cygpath -w "$MONO_LIB")\\" "MSBuildFrameworkToolsRoot=$(cygpath -w "$MONO_LIB/..")\\"
  mkdir -p "$OUT/bin"   # ($OUT/msbuild would collide with $OUT/MSBuild: case-insensitive FS)
  printf '#!/bin/sh\nexec mono "%s/MSBuild.exe" "$@"\n' "$(cygpath -m "$bin")" > "$OUT/bin/msbuild"; chmod +x "$OUT/bin/msbuild"
}

step_test() {
  local T=$M612/test/msbuild-$V-oldstyle   # outside the msbuild tree (its Directory.Build.props)
  rm -rf "${T:?}"; mkdir -p "$T"; cp "$here"/tests/oldstyle/* "$T/"
  cd "$T"
  mono "$OUT/MSBuild/Current/Bin/MSBuild.exe" -nologo -m:1 -nr:false -v:${TEST_VERBOSITY:-m} hello.csproj
  mono bin/Release/hello.exe
}

for s in $steps; do log "msbuild $V: $s"; "step_$s"; done
