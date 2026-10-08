#!/bin/bash
# Build NuGet.Client (net472 flavour: NuGet.* libraries, NuGet.Build.Tasks,
# Microsoft.Build.NuGetSdkResolver, NuGet.targets) from source with direct
# csc command lines on the Mono 6.12 seed, and add it to an MSBuild layout.
#
# usage: nuget.sh <version> <csc.exe> <msbuild-dir> [step...]
#   steps: prep libs install test
# Needs $M612/pkgs (netfx-pkgs.sh incl. Newtonsoft.Json, System.Text.Json,
# Microsoft.Extensions.FileSystemGlobbing) and the MSBuild build (msbuild.sh).
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
. "$here/../env-m612.sh"
V=${1:?nuget version}; BCSC=${2:?csc.exe}; MSB=${3:?msbuild version}; shift 3
steps=${*:-prep xdt libs install test}
XDT_VER=${XDT_VER:-3.2.12}
SRC=$M612/src/nuget-$V.tar.gz
B=$M612/build/nuget-$V
PKGDIR=${PKGDIR:-$M612/pkgs}
MSBOUT=$M612/msbuild-$MSB/tools
. "$here/common.sh"
O=$B/obj/out
P=$here/../../patches/m612/nuget-$V

step_prep() {
  fetch_tarball "https://github.com/NuGet/NuGet.Client/archive/refs/tags/$V.tar.gz" "$SRC"
  rm -rf "${B:?}"; mkdir -p "$B"
  tar -C "$B" --strip-components=1 -xzf "$SRC"
  delete_binaries "$B"    # test data packages/PEs under test/
  apply_patches "$B" "$P"
  mkdir -p "$O"
}

# NuGet's assembly version is $(SemanticVersion).$(PreReleaseVersion): 6.8.2.3
AVER=$V
EVAL_PROPS=(-p TargetFramework=net472 -p NETFXTargetFramework=net472 -p Configuration=Release -p IsNetCoreProject=true
  -p TargetFrameworkIdentifier=.NETFramework -p TargetFrameworkVersion=v4.7.2)
evalp() {
  local d=$1 p=$2; shift 2
  (cd "$d" && csproj_items "${EVAL_PROPS[@]}" --pre "$B/build/common.project.props" "$p" --post "$B/build/common.targets" "$@" 2>/dev/null)
}
FWREFS=("${NETFX_REFS[@]}" -r:"$RA/System.Net.Http.dll" -r:"$RA/Microsoft.CSharp.dll" -r:"$RA/System.IO.Compression.FileSystem.dll"
  -r:"$RA/System.Security.dll" -r:"$RA/System.ComponentModel.Composition.dll" -r:"$RA/System.Data.dll"
  -r:"$RA/System.IdentityModel.dll" -r:"$RA/System.ServiceModel.dll"
  -r:"$RA/Facades/System.Threading.dll" -r:"$RA/Facades/System.IO.dll" -r:"$RA/Facades/System.Reflection.dll"
  -r:"$RA/Facades/System.Runtime.Extensions.dll" -r:"$RA/Facades/System.Linq.dll" -r:"$RA/Facades/System.Text.Encoding.dll"
  -r:"$RA/Facades/System.Runtime.InteropServices.RuntimeInformation.dll"
  -r:"$RA/Facades/System.Security.Cryptography.Algorithms.dll" -r:"$RA/Facades/System.Security.Cryptography.Primitives.dll"
  -r:"$RA/Facades/System.Security.Cryptography.X509Certificates.dll")
dllrefs() { local f; for f in "$@"; do [ -e "$f" ] && echo "-r:$(winpath "$f")"; done; }

# build_ng <AssemblyName> <project dir under src/NuGet.Core> [extra csc args]
build_ng() {
  local name=$1 pd="$B/src/NuGet.Core/$1"; shift
  local proj="$name.csproj" o="$B/obj/$name"; rm -rf "${o:?}"; mkdir -p "$o"
  local items=() res=() dargs=() d dl=() f ln rn
  mapfile -t items < <(evalp "$pd" "$proj" --default-items | sed "s|^|$pd/|")
  IFS=';' read -ra dl <<< "$(evalp "$pd" "$proj" --define)"
  for d in "${dl[@]}"; do [ -n "$d" ] && dargs+=("-d:$d"); done
  rn=$(evalp "$pd" "$proj" --prop RootNamespace | cut -d= -f2); rn=${rn:-$name}
  # resources: SDK default glob **/*.resx -> <RootNamespace>.<relative.path>.resources
  while IFS= read -r f; do
    ln="$rn.$(sed 's|/|.|g' <<< "${f%.resx}").resources"
    resgen "$pd/$f" "$o/$ln" > /dev/null; res+=(-resource:"$o/$ln")
  done < <(cd "$pd" && find . -name '*.resx' ! -path './bin/*' ! -path './obj/*' | sed 's|^\./||' | grep -v '\.[a-z][a-z]\(-[A-Za-z]*\)\?\.resx$' || true)
  printf '[assembly: System.Reflection.AssemblyVersion("%s")]\n[assembly: System.Reflection.AssemblyFileVersion("%s")]\n[assembly: System.Reflection.AssemblyInformationalVersion("%s+m612.bootstrap.from.source")]\n' \
    "$AVER" "$AVER" "$AVER" > "$o/AssemblyInfo.g.cs"
  csc "$name" -target:library -nostdlib -optimize+ -debug:portable -deterministic -unsafe -nullable:${NULLABLE:-annotations} \
    -langversion:latest -out:"$O/$name.dll" -publicsign -keyfile:"$(keyfile MSShared)" \
    -d:TRACE -d:NETFRAMEWORK -d:NET472 -d:NET472_OR_GREATER -d:NET46_OR_GREATER -d:NET45_OR_GREATER \
    -nowarn:CS1591,CS1573,CS0618,CS0612,CS1574,CS8600,CS8601,CS8602,CS8603,CS8604,CS8618,CS8625,CS8619,CS8620,CS8765,CS8767,CS8769,CS0436 \
    "${dargs[@]}" "${FWREFS[@]}" $(dllrefs "$PKGDIR"/*.dll) $(dllrefs "$O"/*.dll | grep -v "/$name.dll") "$@" \
    "${res[@]}" "$o/AssemblyInfo.g.cs" "${items[@]}" || die "$name failed ($B/obj/$name.log)"
}

# Microsoft.Web.Xdt (Microsoft.Web.XmlTransform), needed by NuGet.PackageManagement
step_xdt() {
  local X=$M612/src/xdt-$XDT_VER o="$B/obj/xdt" items=()
  if [ ! -d "$X/src" ]; then
    fetch_tarball "https://github.com/dotnet/xdt/archive/refs/tags/v$XDT_VER.tar.gz" "$X.tar.gz"
    mkdir -p "$X"; tar -C "$X" --strip-components=1 -xzf "$X.tar.gz"; delete_binaries "$X"
  fi
  local pd="$X/src/Microsoft.Web.XmlTransform"; rm -rf "${o:?}"; mkdir -p "$o"
  mapfile -t items < <(cd "$pd" && find . -name '*.cs' ! -path './obj/*' ! -path './bin/*' | sed "s|^\.|$pd|")
  resgen "$pd/Properties/Resources.resx" "$o/Microsoft.Web.XmlTransform.Properties.Resources.resources" > /dev/null
  resxgen "$pd/Properties/Resources.resx" "$o/Resources.g.cs" Microsoft.Web.XmlTransform.Properties.Resources
  local av=${XDT_VER%.*}.0.0
  printf '[assembly: System.Reflection.AssemblyVersion("%s")]\n[assembly: System.Reflection.AssemblyFileVersion("%s")]\n' "$av" "$XDT_VER.0" > "$o/AssemblyInfo.g.cs"
  csc Microsoft.Web.XmlTransform -target:library -nostdlib -optimize+ -debug:portable -deterministic \
    -out:"$PKGDIR/Microsoft.Web.XmlTransform.dll" -publicsign -keyfile:"$(keyfile Microsoft)" -d:TRACE -d:NETSTANDARD -d:NETSTANDARD2_0 \
    "${FWREFS[@]}" -resource:"$o/Microsoft.Web.XmlTransform.Properties.Resources.resources" "$o/Resources.g.cs" "$o/AssemblyInfo.g.cs" "${items[@]}" \
    || die "Microsoft.Web.XmlTransform failed"
}

MSBREFS() { dllrefs "$MSBOUT/Microsoft.Build.Framework.dll" "$MSBOUT/Microsoft.Build.Utilities.Core.dll" "$MSBOUT/Microsoft.Build.dll" "$MSBOUT/Microsoft.Build.Tasks.Core.dll"; }
step_libs() {
  local l
  for l in ${NG_LIBS:-NuGet.Frameworks NuGet.Common NuGet.Configuration NuGet.Versioning NuGet.Packaging NuGet.LibraryModel \
      NuGet.Protocol NuGet.DependencyResolver.Core NuGet.ProjectModel NuGet.Credentials NuGet.Commands NuGet.Resolver}; do
    log "nuget lib $l"; build_ng "$l"
  done
  log "nuget lib NuGet.PackageManagement"; build_ng NuGet.PackageManagement
  # upstream compiles the net472 tasks against MSBuild 4.0 from the GAC
  # (Microsoft.Build.Utilities.v4.0); we use the MSBuild we built.
  log "nuget lib NuGet.Build.Tasks"; build_ng NuGet.Build.Tasks $(MSBREFS)
  log "nuget lib Microsoft.Build.NuGetSdkResolver"; build_ng Microsoft.Build.NuGetSdkResolver $(MSBREFS)
}

# NuGet.targets/props and the assemblies go next to MSBuild, like VS does
# (Common7/IDE/CommonExtensions/Microsoft/NuGet is VS-only; MSBuild finds
# NuGet.targets through $(NuGetRestoreTargets) = $(MSBuildToolsPath)\NuGet.targets).
step_install() {
  cp "$O"/NuGet.*.dll "$O"/NuGet.*.pdb "$MSBOUT/"
  cp "$O"/Microsoft.Build.NuGetSdkResolver.dll "$MSBOUT/"
  cp "$B/src/NuGet.Core/NuGet.Build.Tasks/NuGet.targets" "$B/src/NuGet.Core/NuGet.Build.Tasks/NuGet.props" \
     "$B/src/NuGet.Core/NuGet.Build.Tasks/NuGet.RestoreEx.targets" "$MSBOUT/" 2>/dev/null || true
  local f; for f in "$PKGDIR"/*.dll; do cp "$f" "$MSBOUT/"; done
  # NuGet.targets lives next to MSBuild.exe ($(MSBuildToolsPath)\NuGet.targets,
  # the standalone default; stated explicitly), and binding redirects for the
  # new package assemblies are added.
  : > "$B/obj/versions.txt"
  for f in "$MSBOUT"/*.dll; do
    echo "$(basename "$f" .dll) $(monodis --assembly "$f" | sed -n 's/^Version: *//p' | tr -d '\r')" >> "$B/obj/versions.txt"
  done
  cp "$MSBOUT/MSBuild.exe.config" "$B/obj/MSBuild.exe.config.orig"
  python3 -S "$here/tools/fix_config.py" "$B/obj/MSBuild.exe.config.orig" "$MSBOUT/MSBuild.exe.config" "$B/obj/versions.txt" \
    'NuGetRestoreTargets=$([MSBuild]::GetToolsDirectory32())\NuGet.targets'
}

step_test() {
  local T=$M612/test/nuget-$V-restore
  rm -rf "${T:?}"; mkdir -p "$T/feed" "$T/pkgsrc"; cp "$here"/tests/restore/* "$T/"
  cd "$T"
  # a package made entirely from source: our csc + a hand-written nuspec, zipped
  mono "$(cygpath -m "$M612/roslyn-4.12.0/csc.exe")" -nologo -target:library -out:pkgsrc/Bootstrap.Greeting.dll Greeting.cs
  python3 -S - <<'EOPY'
import zipfile
with zipfile.ZipFile('feed/bootstrap.greeting.1.0.0.nupkg', 'w', zipfile.ZIP_DEFLATED) as z:
    z.write('Bootstrap.Greeting.nuspec', 'Bootstrap.Greeting.nuspec')
    z.write('pkgsrc/Bootstrap.Greeting.dll', 'lib/net472/Bootstrap.Greeting.dll')
EOPY
  mono "$MSBOUT/MSBuild.exe" -nologo -m:1 -nr:false -v:${TEST_VERBOSITY:-m} -t:Restore restoreme.csproj
  grep -o '"Bootstrap.Greeting/1.0.0"' obj/project.assets.json | head -1
  ls packages/bootstrap.greeting/1.0.0/lib/net472/
}

for s in $steps; do log "nuget $V: $s"; "step_$s"; done
