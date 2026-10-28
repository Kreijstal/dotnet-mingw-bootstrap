#!/bin/bash
# Build the netstandard/.NET Framework "package" assemblies that Roslyn,
# MSBuild and NuGet consume on net472 (what NuGet would download as
# System.Memory, System.Collections.Immutable, ...), from source, for Mono.
#
# usage: netfx-pkgs.sh <bootstrap-csc.exe> <outdir> [lib...]
#   libs: System.Buffers System.Numerics.Vectors System.Runtime.CompilerServices.Unsafe
#         System.Threading.Tasks.Extensions System.Memory
#         System.Collections.Immutable System.Reflection.Metadata ... (see recipe())
# Sources: dotnet/maintenance-packages (frozen netfx packages) and
#          dotnet/runtime v$RT_VER (src/libraries, target net462).
# Every assembly is public-signed with the upstream public key, so the
# identities (name, version, token) match the NuGet packages.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
. "$here/../env-m612.sh"
BCSC=${1:?bootstrap csc.exe}; OUTDIR=${2:?outdir}; shift 2
mkdir -p "$OUTDIR"; OUTDIR=$(cd "$OUTDIR" && pwd)
MP_TAG=${MP_TAG:-2025.03.19}
RT_VER=${RT_VER:-8.0.0}
B=$M612/build/netfx-pkgs
. "$here/common.sh"
PKGDIR=$OUTDIR   # libraries built here are referenced by later ones
MP=$M612/src/maintenance-packages-$MP_TAG
RT=$M612/src/runtime-$RT_VER
NJ_VER=${NJ_VER:-13.0.3}
DT_REV=${DT_REV:-5255d40e228ea1d4b624781b5b97ec16484a3b4b}   # dotnet/deployment-tools, as pinned by dotnet/sdk v8.0.131
ROSLYN_DIR=${ROSLYN_DIR:-$(dirname "$BCSC")}   # analyzers are built against this Roslyn

prep_sources() {
  mkdir -p "$B/obj"
  if [ ! -d "$MP/src" ]; then
    fetch_tarball "https://github.com/dotnet/maintenance-packages/archive/refs/tags/$MP_TAG.tar.gz" "$M612/src/maintenance-packages-$MP_TAG.tar.gz"
    mkdir -p "$MP"; tar -C "$MP" --strip-components=1 -xzf "$M612/src/maintenance-packages-$MP_TAG.tar.gz"
    delete_binaries "$MP"
  fi
  if [ ! -d "$RT/src/libraries" ]; then
    fetch_tarball "https://github.com/dotnet/runtime/archive/refs/tags/v$RT_VER.tar.gz" "$M612/src/runtime-$RT_VER.tar.gz"
    mkdir -p "$RT"
    # only the managed library sources and the build conventions are needed
    tar -C "$RT" --strip-components=1 -xzf "$M612/src/runtime-$RT_VER.tar.gz" \
      "runtime-$RT_VER/src/libraries" "runtime-$RT_VER/eng" "runtime-$RT_VER/Directory.Build.props" "runtime-$RT_VER/Directory.Build.targets"
    delete_binaries "$RT"
  fi
  if [ ! -d "$RT/src/installer/managed" ]; then   # Microsoft.NET.HostModel
    tar -C "$RT" --strip-components=1 -xzf "$M612/src/runtime-$RT_VER.tar.gz" "runtime-$RT_VER/src/installer/managed"
    delete_binaries "$RT"
  fi
  # HostModel shares Win32-resource readers with the CoreCLR compiler tools.
  if [ ! -d "$RT/src/coreclr/tools/Common" ]; then
    tar -C "$RT" --strip-components=1 -xzf "$M612/src/runtime-$RT_VER.tar.gz" \
      "runtime-$RT_VER/src/coreclr/tools/Common" "runtime-$RT_VER/LICENSE.TXT" "runtime-$RT_VER/THIRD-PARTY-NOTICES.TXT"
    delete_binaries "$RT"
  fi
  # managed-only fixes (the native tree for corehost.sh is a separate copy)
  apply_patches "$RT" "$here/../../patches/m612/runtime-$RT_VER-managed"
}

# build_lib <AssemblyName> <csproj> <key> <AssemblyVersion> <flavor: mp|rt|rtgen> [extra csc args...]
#   rtgen = a dotnet/runtime Roslyn component (source generator), netstandard2.0,
#           SDK default items, built against our own Roslyn ($ROSLYN_DIR);
#           IDE-only code fixers (need Microsoft.CodeAnalysis.Workspaces) are left out.
build_lib() {
  local name=$1 proj=$2 key=$3 aver=$4 flavor=$5; shift 5
  local tfm=net462 outfile="$OUTDIR/$name.dll" exclude='^$'
  if [ $flavor = rtgen ]; then
    tfm=netstandard2.0; mkdir -p "$OUTDIR/analyzers"; outfile="$OUTDIR/analyzers/$name.dll"; exclude="Fixer\\.cs\$|FixAllContextExtensions\\.cs\$${RTGEN_EXCLUDE:+|$RTGEN_EXCLUDE}"
    set -- "$@" -r:"$ROSLYN_DIR/Microsoft.CodeAnalysis.dll" -r:"$ROSLYN_DIR/Microsoft.CodeAnalysis.CSharp.dll"
  fi
  local pdir; pdir=$(cd "$(dirname "$proj")" && pwd)
  local o="$B/obj/$name"; rm -rf "${o:?}"; mkdir -p "$o"
  local common corelib items=() extra=() defs
  [ $flavor = nj ] && tfm=net45
  [ $flavor = rtns ] && tfm=netstandard2.0   # a dotnet/runtime netstandard2.0-only project (SDK default items)
  if [ $flavor = mp ] || [ $flavor = nj ]; then common="$MP/src/Common/"; corelib="$MP/src/Common/src/CoreLib/"
  else common="$RT/src/libraries/Common/src/"; corelib="$RT/src/libraries/System.Private.CoreLib/src/"; fi
  local props=(-p TargetFramework=$tfm -p AssemblyName=$name -p CommonPath=$common -p CoreLibSharedDir=$corelib
    -p NetCoreAppCurrent=net${RT_VER%%.*}.0 -p NetCoreAppPrevious=net$(( ${RT_VER%%.*} - 1 )).0 -p NetCoreAppMinimum=net6.0
    -p NetFrameworkMinimum=net462 -p NetFrameworkCurrent=net481 -p MSBuildProjectExtension=.csproj -p Language=C#
    -p LibrariesProjectRoot=$RT/src/libraries/ -p TargetOS=windows -p TargetsWindows=true -p CoreClrProjectRoot=$RT/src/coreclr/)
  local dflag=(); [ $flavor != rt ] && dflag=(--default-items)
  # dotnet/runtime turns default items off globally; some projects turn them back on
  [ $flavor = rt ] && [ "$(cd "$pdir" && csproj_items -p TargetFramework=$tfm --pre "$RT/src/libraries/Directory.Build.props" --prop EnableDefaultItems "$(basename "$proj")" 2>/dev/null | cut -d= -f2)" = true ] && dflag=(--default-items)
  [ $flavor = nj ] && exclude='/obj/|/bin/'

  mapfile -t items < <(cd "$pdir" && csproj_items "${dflag[@]}" "${props[@]}" "$(basename "$proj")" | grep -Ev "$exclude" | sed "s|^|$pdir/|")
  local sres; sres=$(cd "$pdir" && csproj_items "${props[@]}" --prop StringResourcesPath "$(basename "$proj")" | cut -d= -f2)
  [ -n "$sres" ] && sres=$(cd "$pdir" && realpath -m "$sres") || sres="$pdir/Resources/Strings.resx"
  defs=$(cd "$pdir" && csproj_items "${props[@]}" --define "$(basename "$proj")")
  local nullable; nullable=$(cd "$pdir" && csproj_items "${props[@]}" --prop Nullable "$(basename "$proj")" | cut -d= -f2)
  # string resources (Arcade GenerateResxSource + Common/src/System/SR.cs)
  if [ -f "$sres" ]; then
    resgen "$sres" "$o/FxResources.$name.SR.resources" > /dev/null
    resxgen "$sres" "$o/SR.g.cs" System.SR --fx "FxResources.$name.SR"
    extra+=("$o/SR.g.cs" -resource:"$(cygpath -m "$o/FxResources.$name.SR.resources")")
    if [ $flavor = mp ]; then extra+=("$MP/src/Common/src/System/SR.cs"); else extra+=("$RT/src/libraries/Common/src/System/SR.cs"); fi
  fi
  # implicit polyfills of dotnet/runtime's src/libraries/Directory.Build.targets
  if { [ $flavor = rt ] || [ $flavor = rtgen ]; } && [ "$nullable" != disable ]; then
    extra+=("$corelib/System/Diagnostics/CodeAnalysis/NullableAttributes.cs"); nullable=enable
  fi
  if [ $flavor = rt ]; then   # (maintenance-packages projects carry their own copies)
    local ip; ip=$(cd "$pdir" && csproj_items "${props[@]}" --prop IncludeInternalObsoleteAttribute --prop IncludePlatformAttributes \
      --prop IncludeIndexRangeTypes --prop SupportedOSPlatforms --prop UnsupportedOSPlatforms "$(basename "$proj")")
    grep -qx 'IncludeInternalObsoleteAttribute=true' <<< "$ip" && extra+=("$corelib/System/ObsoleteAttribute.cs")
    grep -qE '^(IncludePlatformAttributes=true|SupportedOSPlatforms=.+|UnsupportedOSPlatforms=.+)$' <<< "$ip" && extra+=("$corelib/System/Runtime/Versioning/PlatformAttributes.cs")
    grep -qx 'IncludeIndexRangeTypes=true' <<< "$ip" && extra+=("$corelib/System/Index.cs" "$corelib/System/Range.cs" "$corelib/System/Numerics/Hashing.cs")
  fi
  if [ "$aver" = auto ]; then
    aver=$(cd "$pdir" && csproj_items "${props[@]}" -p IsPackable=true --prop AssemblyVersion "$(basename "$proj")" | cut -d= -f2)
    [ -n "$aver" ] || die "$name: no AssemblyVersion"
  fi
  # dotnet/runtime libraries use [LibraryImport]: run our from-source
  # LibraryImportGenerator (eng/generators.targets adds these two downlevel)
  if { [ $flavor = rt ] || [ $flavor = rtns ]; } && grep -qs LibraryImport "${items[@]}"; then
    [ -f "$OUTDIR/analyzers/Microsoft.Interop.LibraryImportGenerator.dll" ] || recipe Microsoft.Interop.LibraryImportGenerator
    extra+=("$corelib/System/Runtime/InteropServices/LibraryImportAttribute.cs" "$corelib/System/Runtime/InteropServices/StringMarshalling.cs"
      -analyzer:"$OUTDIR/analyzers/Microsoft.Interop.SourceGeneration.dll" -analyzer:"$OUTDIR/analyzers/Microsoft.Interop.LibraryImportGenerator.dll"
      -analyzerconfig:"$o/build.globalconfig")
    # what the SDK's GenerateMSBuildEditorConfig would pass to generators
    printf 'is_global = true\nbuild_property.TargetFrameworkIdentifier = %s\nbuild_property.TargetFrameworkVersion = v%s\n' \
      "$( [ $tfm = net462 ] && echo .NETFramework || echo .NETStandard)" "$( [ $tfm = net462 ] && echo 4.6.2 || echo 2.0)" > "$o/build.globalconfig"
  fi
  printf '[assembly: System.Reflection.AssemblyVersion("%s")]\n[assembly: System.Reflection.AssemblyFileVersion("%s")]\n[assembly: System.Reflection.AssemblyInformationalVersion("%s+m612.bootstrap.from.source")]\n[assembly: System.Reflection.AssemblyMetadata("Source", "%s")]\n' \
    "$aver" "$aver" "$aver" "$flavor" > "$o/AssemblyInfo.g.cs"
  local dl=() d; IFS=';' read -ra dl <<< "$defs"
  local dargs=(); for d in "${dl[@]}"; do [ -n "$d" ] && dargs+=("-d:$d"); done
  csc "$name" -target:library -nostdlib -optimize+ -debug:portable -deterministic -unsafe -nullable:${nullable:-disable} \
    -out:"$outfile" -publicsign -keyfile:"$(keyfile $key)" \
    -d:TRACE $(tfm_defines $tfm) \
    -nowarn:CS0436,CS8597,CS8625,CS8617,CS0169,CS0649,CS8618,CS8969,SYSLIB0011 \
    "${dargs[@]}" "${NETFX_REFS[@]}" $(lib_refs "$name") "$@" "${extra[@]}" "$o/AssemblyInfo.g.cs" "${items[@]}" \
    || die "$name failed (see $B/obj/$name.log)"
}

# System.Runtime.CompilerServices.Unsafe is written in IL.  Upstream runs
# CoreCLR's ilasm (with its #include/#define preprocessor); we preprocess with
# cpp and assemble with the seed's Mono ilasm, then embed the upstream public
# key (public signing) via .publickey.
build_unsafe() {
  local src="$MP/src/System.Runtime.CompilerServices.Unsafe/src/System.Runtime.CompilerServices.Unsafe.il"
  local o="$B/obj/Unsafe"; rm -rf "${o:?}"; mkdir -p "$o"
  local av; av=$(cd "$(dirname "$src")" && csproj_items -p TargetFramework=net462 -p IsPackable=true --prop AssemblyVersion System.Runtime.CompilerServices.Unsafe.ilproj | cut -d= -f2)
  cat > "$o/version.h" <<EOV
#define CORE_ASSEMBLY mscorlib
#define ASSEMBLY_VERSION ${av//./:}
#define CORE_ASSEMBLY_VERSION 4:0:0:0
#define FILE_VERSION {string('$av')}
#define INFORMATIONAL_VERSION {string('$av+m612.bootstrap.from.source')}
#define net462
.assembly extern CORE_ASSEMBLY
{
  .publickeytoken = (B7 7A 5C 56 19 34 E0 89 )
  .ver CORE_ASSEMBLY_VERSION
}
EOV
  cpp -P -undef -nostdinc -I"$o" -x c "$src" > "$o/Unsafe.il"
  local pk; pk=$(tr -d '\n\r' < "$KEYS"/Microsoft-*.pubkey.hex | sed 's/../& /g')
  sed -i "s/^  \.hash algorithm 0x00008004/  .publickey = ( $pk)\n&/" "$o/Unsafe.il"
  ilasm -dll -optimize -out:"$(cygpath -m "$OUTDIR/System.Runtime.CompilerServices.Unsafe.dll")" "$(cygpath -m "$o/Unsafe.il")" > "$o/ilasm.log" 2>&1 \
    || { cat "$o/ilasm.log"; die "ilasm Unsafe failed"; }
}

# references for a library: everything already built in OUTDIR (not itself),
# plus the seed's package assemblies for names not (yet) built from source
lib_refs() {
  local self=$1 f n
  for f in "$OUTDIR"/*.dll; do [ -e "$f" ] || continue; n=$(basename "$f" .dll); [ "$n" = "$self" ] || echo "-r:$(winpath "$f")"; done
  for n in $PKG_NAMES; do [ "$n" = "$self" ] || [ -f "$OUTDIR/$n.dll" ] || pkg_refs "$n"; done
}
rt_key() { # StrongNameKeyId of a dotnet/runtime library (per-library Directory.Build.props, default Open)
  local k; k=$(grep -ho '<StrongNameKeyId>[^<]*' "$RT/src/libraries/$1/Directory.Build.props" "$RT/src/libraries/$1/src/$1.csproj" 2>/dev/null | head -1 | cut -d'>' -f2)
  [ "$k" = MicrosoftShared ] && k=MSShared
  case $1 in Microsoft.Extensions.*) echo "${k:-MicrosoftAspNetCore}" ;; *) echo "${k:-Open}" ;; esac
}

tfm_defines() {
  case $1 in
    net45) echo -d:NETFRAMEWORK -d:NET45 -d:NET45_OR_GREATER ;;
    net462) echo -d:NETFRAMEWORK -d:NET462 -d:NET461_OR_GREATER -d:NET462_OR_GREATER -d:NET45_OR_GREATER -d:NET46_OR_GREATER ;;
    netstandard2.0) echo -d:NETSTANDARD -d:NETSTANDARD2_0 -d:NETSTANDARD2_0_OR_GREATER -d:NETSTANDARD1_0_OR_GREATER ;;
  esac
}
GEN=$RT/src/libraries/System.Runtime.InteropServices/gen
recipe() {
  case $1 in
    System.Runtime.CompilerServices.Unsafe) build_unsafe ;;
    System.Buffers) build_lib System.Buffers "$MP/src/System.Buffers/src/System.Buffers.csproj" Open auto mp ;;
    System.Numerics.Vectors) build_lib System.Numerics.Vectors "$MP/src/System.Numerics.Vectors/src/System.Numerics.Vectors.csproj" Microsoft auto mp ;;
    System.Threading.Tasks.Extensions) build_lib System.Threading.Tasks.Extensions "$MP/src/System.Threading.Tasks.Extensions/src/System.Threading.Tasks.Extensions.csproj" Open auto mp ;;
    Newtonsoft.Json)
      local NJ=$M612/src/newtonsoft.json-$NJ_VER
      if [ ! -d "$NJ/Src" ]; then
        fetch_tarball "https://github.com/JamesNK/Newtonsoft.Json/archive/refs/tags/$NJ_VER.tar.gz" "$NJ.tar.gz"
        mkdir -p "$NJ"; tar -C "$NJ" --strip-components=1 -xzf "$NJ.tar.gz"; delete_binaries "$NJ"
        rm -f "$NJ/Src/Newtonsoft.Json/Dynamic.snk"   # private key pair; we public-sign with keys/Newtonsoft-*
      fi
      build_lib Newtonsoft.Json "$NJ/Src/Newtonsoft.Json/Newtonsoft.Json.csproj" Newtonsoft 13.0.0.0 nj -d:SIGNED \
        -r:"$RA/Microsoft.CSharp.dll" -r:"$RA/System.Data.dll" -r:"$RA/System.ComponentModel.DataAnnotations.dll" ;;
    Microsoft.Deployment.DotNet.Releases)
      local DT=$M612/src/deployment-tools-$DT_REV
      if [ ! -d "$DT/src" ]; then
        fetch_tarball "https://github.com/dotnet/deployment-tools/archive/$DT_REV.tar.gz" "$DT.tar.gz"
        mkdir -p "$DT"; tar -C "$DT" --strip-components=1 -xzf "$DT.tar.gz"; delete_binaries "$DT"
      fi
      local d="$DT/src/Microsoft.Deployment.DotNet.Releases/src" r="$B/obj/Microsoft.Deployment.DotNet.Releases.ReleasesResources.resources"
      mkdir -p "$B/obj"; resgen "$d/ReleasesResources.resx" "$r" > /dev/null
      build_lib Microsoft.Deployment.DotNet.Releases "$d/Microsoft.Deployment.DotNet.Releases.csproj" Microsoft 2.0.0.0 rtns -resource:"$r" -r:"$RA/System.Net.Http.dll" ;;
    Microsoft.NET.HostModel)
      build_lib Microsoft.NET.HostModel "$RT/src/installer/managed/Microsoft.NET.HostModel/Microsoft.NET.HostModel.csproj" MicrosoftAspNetCore ${RT_VER%%.*}.0.0.0 rtns ;;
    Microsoft.IO.Redist) build_lib Microsoft.IO.Redist "$MP/src/Microsoft.IO.Redist/src/Microsoft.IO.Redist.csproj" Open 6.0.0.0 mp ;;
    System.Memory) build_lib System.Memory "$MP/src/System.Memory/src/System.Memory.csproj" Open auto mp ;;
    # source generators (Roslyn components), needed to compile runtime libraries
    Microsoft.Interop.SourceGeneration) build_lib Microsoft.Interop.SourceGeneration "$GEN/Microsoft.Interop.SourceGeneration/Microsoft.Interop.SourceGeneration.csproj" Open ${RT_VER%%.*}.0.0.0 rtgen ;;
    Microsoft.Interop.LibraryImportGenerator)
      [ -f "$OUTDIR/analyzers/Microsoft.Interop.SourceGeneration.dll" ] || recipe Microsoft.Interop.SourceGeneration
      build_lib Microsoft.Interop.LibraryImportGenerator "$GEN/LibraryImportGenerator/LibraryImportGenerator.csproj" Open ${RT_VER%%.*}.0.0.0 rtgen \
      -r:"$OUTDIR/analyzers/Microsoft.Interop.SourceGeneration.dll" ;;
    # System.Private.CoreLib's EventSourceGenerator (+ its intrinsics analyzer)
    System.Private.CoreLib.Generators)
      build_lib System.Private.CoreLib.Generators "$RT/src/libraries/System.Private.CoreLib/gen/System.Private.CoreLib.Generators.csproj" Open ${RT_VER%%.*}.0.0.0 rtgen ;;
    # [GeneratedComInterface]/[GeneratedComClass] (libraries that reference System.Runtime.InteropServices directly)
    Microsoft.Interop.ComInterfaceGenerator)
      [ -f "$OUTDIR/analyzers/Microsoft.Interop.SourceGeneration.dll" ] || recipe Microsoft.Interop.SourceGeneration
      build_lib Microsoft.Interop.ComInterfaceGenerator "$GEN/ComInterfaceGenerator/ComInterfaceGenerator.csproj" Open ${RT_VER%%.*}.0.0.0 rtgen \
      -r:"$OUTDIR/analyzers/Microsoft.Interop.SourceGeneration.dll" ;;
    # [GeneratedRegex] (System.Private.Xml, System.Data.Common, ... use it)
    System.Text.RegularExpressions.Generator)
      build_lib System.Text.RegularExpressions.Generator "$RT/src/libraries/System.Text.RegularExpressions/gen/System.Text.RegularExpressions.Generator.csproj" Open ${RT_VER%%.*}.0.0.0 rtgen ;;
    # the other generators the Microsoft.NETCore.App.Ref pack ships in analyzers/dotnet/cs (refpack.sh pack)
    System.Text.Json.SourceGeneration)   # the Roslyn 4.4 flavour (the pack's); its 3.11 entry point is left out
      RTGEN_EXCLUDE='JsonSourceGenerator\.Roslyn3\.11\.cs$' build_lib System.Text.Json.SourceGeneration \
        "$RT/src/libraries/System.Text.Json/gen/System.Text.Json.SourceGeneration.Roslyn4.4.csproj" Open ${RT_VER%%.*}.0.0.0 rtgen ;;
    Microsoft.Interop.JavaScript.JSImportGenerator)
      [ -f "$OUTDIR/analyzers/Microsoft.Interop.SourceGeneration.dll" ] || recipe Microsoft.Interop.SourceGeneration
      build_lib Microsoft.Interop.JavaScript.JSImportGenerator "$RT/src/libraries/System.Runtime.InteropServices.JavaScript/gen/JSImportGenerator/JSImportGenerator.csproj" \
        Open ${RT_VER%%.*}.0.0.0 rtgen -r:"$OUTDIR/analyzers/Microsoft.Interop.SourceGeneration.dll" ;;
    *) if [ -f "$RT/src/libraries/$1/src/$1.csproj" ]; then
         build_lib "$1" "$RT/src/libraries/$1/src/$1.csproj" "$(rt_key "$1")" ${RT_VER%%.*}.0.0.0 rt
       else die "no recipe for $1"; fi ;;
  esac
}

prep_sources
for l in ${*:-Microsoft.Interop.SourceGeneration Microsoft.Interop.LibraryImportGenerator System.Runtime.CompilerServices.Unsafe System.Buffers System.Numerics.Vectors System.Threading.Tasks.Extensions System.Memory System.Collections.Immutable System.Reflection.Metadata}; do
  log "netfx package: $l"; recipe "$l"
done
