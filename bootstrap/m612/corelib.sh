#!/bin/bash
# Build System.Private.CoreLib for the Mono flavor of dotnet/runtime 8.0.0
# (win-x64), from the same patched tree the native runtime (mono-runtime.sh)
# was built from, so that CoreLib's icalls/structure layouts match coreclr.dll.
#
# Sources: src/mono/System.Private.CoreLib/System.Private.CoreLib.csproj plus
# the shared src/libraries/System.Private.CoreLib/src/System.Private.CoreLib.Shared.projitems,
# evaluated by tools/csproj_items.py with TargetOS=windows, Platform=x64,
# Configuration=Release (defines MONO, TARGET_AMD64, TARGET_64BIT,
# TARGET_WINDOWS, FEATURE_PERFTRACING, FEATURE_MANAGED_ETW(_CHANNELS),
# MONO_FEATURE_SRE, ...).  Generated inputs, all from source:
#   NativeRuntimeEventSource.Mono.cs  src/coreclr/scripts/genRuntimeEventSources.py (python)
#   SR                                Strings.resx via tools/ResxGen.cs + resgen
#   [GeneratedEventSource], [LibraryImport]  the EventSourceGenerator and the
#                                     LibraryImportGenerator, built from source (netfx-pkgs.sh)
#
# usage: corelib.sh <csc.exe> [step...]       steps: gen events compile
#   csc.exe: Roslyn 4.8.0 (what runtime 8.0.0 builds with, LangVersion=preview)
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
. "$here/../env-m612.sh"
BCSC=${1:?csc.exe}; shift
steps=${*:-gen events compile}
RT_VER=${RT_VER:-8.0.0}; TFM=net${RT_VER%%.*}.0
SRC=$M612/src/runtime-$RT_VER-native          # full tree, patches 0001.. applied (corehost.sh prep)
L=$SRC/src/libraries
B=$M612/build/corelib-$RT_VER
O=${FXOUT:-$M612/netcoreapp-$RT_VER}/lib      # implementation assemblies of Microsoft.NETCore.App
PKGDIR=${PKGDIR:-$M612/pkgs}
. "$here/common.sh"
mkdir -p "$B" "$O"
COMMIT=$(cat "$SRC/.commit" 2>/dev/null || echo unknown)

step_gen() {
  local g
  for g in Microsoft.Interop.LibraryImportGenerator System.Private.CoreLib.Generators; do
    [ -f "$PKGDIR/analyzers/$g.dll" ] && continue
    bash "$here/netfx-pkgs.sh" "$BCSC" "$PKGDIR" "$g"
  done
}

step_events() {
  mkdir -p "$B/gen"
  python3 -B "$SRC/src/coreclr/scripts/genRuntimeEventSources.py" --man "$SRC/src/coreclr/vm/ClrEtwAll.man" \
    --intermediate "$B/gen" --runtimeflavor mono
  ls -l "$B/gen/NativeRuntimeEventSource.Mono.cs"
}

PROPS=(-p TargetFramework=$TFM -p NetCoreAppCurrent=$TFM -p TargetOS=windows -p TargetsWindows=true -p Platform=x64
  -p Configuration=Release -p TargetArchitecture=x64 -p LibrariesProjectRoot=$L/ -p CoreLibSharedDir=$L/System.Private.CoreLib/src/
  -p CoreClrProjectRoot=$SRC/src/coreclr/)

step_compile() {
  local pd=$SRC/src/mono/System.Private.CoreLib o=$B/obj/System.Private.CoreLib items=() dl=() dargs=() d
  rm -rf "${o:?}"; mkdir -p "$o"
  mapfile -t items < <(cd "$pd" && csproj_items "${PROPS[@]}" System.Private.CoreLib.csproj 2>/dev/null | grep -v '^@' | sed "s|^|$pd/|")
  IFS=';' read -ra dl <<< "$(cd "$pd" && csproj_items "${PROPS[@]}" --define System.Private.CoreLib.csproj 2>/dev/null)"
  for d in "${dl[@]}"; do [ -n "$d" ] && dargs+=("-d:$d"); done
  log "CoreLib: ${#items[@]} source files, defines ${dl[*]}"
  # string resources: manifest System.Private.CoreLib.Strings (the projitems' StringResourcesName)
  resgen "$L/System.Private.CoreLib/src/Resources/Strings.resx" "$o/System.Private.CoreLib.Strings.resources" > /dev/null
  resxgen "$L/System.Private.CoreLib/src/Resources/Strings.resx" "$o/SR.g.cs" System.SR --corelib --resource-name System.Private.CoreLib.Strings
  # what Arcade/the SDK would generate (versioning.targets, GenerateAssemblyInfo)
  cat > "$o/AssemblyInfo.g.cs" <<EOF
[assembly: System.Reflection.AssemblyCompany("Microsoft Corporation")]
[assembly: System.Reflection.AssemblyCopyright("© Microsoft Corporation. All rights reserved.")]
[assembly: System.Reflection.AssemblyDescription("System.Private.CoreLib")]
[assembly: System.Reflection.AssemblyProduct("Microsoft® .NET")]
[assembly: System.Reflection.AssemblyTitle("System.Private.CoreLib")]
[assembly: System.Reflection.AssemblyVersion("${RT_VER%%.*}.0.0.0")]
[assembly: System.Reflection.AssemblyFileVersion("$RT_VER.0")]
[assembly: System.Reflection.AssemblyInformationalVersion("$RT_VER+$COMMIT")]
[assembly: System.Reflection.AssemblyMetadata(".NETFrameworkAssembly", "")]
[assembly: System.Reflection.AssemblyMetadata("Serviceable", "True")]
[assembly: System.Reflection.AssemblyMetadata("IsTrimmable", "True")]
[assembly: System.Runtime.Versioning.TargetFramework(".NETCoreApp,Version=v${TFM#net}", FrameworkDisplayName = ".NET ${TFM#net}")]
EOF
  printf 'is_global = true\nbuild_property.TargetFrameworkIdentifier = .NETCoreApp\nbuild_property.TargetFrameworkVersion = v%s\nbuild_property.LibraryImportGenerator_UseMarshalType = true\n' \
    "${TFM#net}" > "$o/build.globalconfig"
  # eng/illink.targets: the combined ILLink descriptors/substitutions/link attributes, embedded so
  # that trimming an app (PublishTrimmed) keeps what the runtime needs (System.TypedReference, ...)
  local k kind illink=()
  for k in Descriptors Substitutions LinkAttributes; do
    local xs=(); mapfile -t xs < <(cd "$pd" && csproj_items "${PROPS[@]}" -p FeaturePerfTracing=true --kind ILLink${k}Xmls System.Private.CoreLib.csproj 2>/dev/null | sed "s|^\([^/A-Za-z]\)|\1|; s|^\([^A-Za-z]:\)|\1|")
    for kind in "${!xs[@]}"; do [[ ${xs[$kind]} == /* || ${xs[$kind]} =~ ^[A-Za-z]: ]] || xs[$kind]=$pd/${xs[$kind]}; done
    python3 -S "$M612_TOOLS_SRC/illinkxml.py" combine "$o/ILLink.$k.xml" "${xs[@]}" | sed 's/^/  /'
    [ -f "$o/ILLink.$k.xml" ] && illink+=(-resource:"$o/ILLink.$k.xml,ILLink.$k.xml")
  done
  local A=$PKGDIR/analyzers
  # csc on Mono does not create the -generatedfilesout subdirectories itself
  mkdir -p "$o/generated/Microsoft.Interop.LibraryImportGenerator/Microsoft.Interop.LibraryImportGenerator" \
    "$o/generated/System.Private.CoreLib.Generators/Generators.EventSourceGenerator"
  csc System.Private.CoreLib -target:library -nostdlib -noconfig -optimize+ -deterministic -unsafe -nullable:enable -debug:portable \
    -runtimemetadataversion:v4.0.30319 -out:"$O/System.Private.CoreLib.dll" -publicsign -keyfile:"$(keyfile SilverlightPlatform)" \
    -d:NET -d:NET8_0 -d:NET8_0_OR_GREATER -d:NET7_0_OR_GREATER -d:NET6_0_OR_GREATER -d:NET5_0_OR_GREATER \
    -d:NETCOREAPP3_1_OR_GREATER -d:NETCOREAPP3_0_OR_GREATER -d:NETCOREAPP2_1_OR_GREATER -d:NETCOREAPP2_0_OR_GREATER \
    -d:NETCOREAPP1_1_OR_GREATER -d:NETCOREAPP1_0_OR_GREATER "${dargs[@]}" \
    -nowarn:0419,0649,618,67,CS8597,CS1574,CS1580,CS1581,CS1584,CS1658,CS0436,CS3021,CS8981 -warn:4 \
    -analyzer:"$A/Microsoft.Interop.SourceGeneration.dll" -analyzer:"$A/Microsoft.Interop.LibraryImportGenerator.dll" \
    -analyzer:"$A/System.Private.CoreLib.Generators.dll" -analyzerconfig:"$o/build.globalconfig" \
    -generatedfilesout:"$o/generated" \
    -resource:"$o/System.Private.CoreLib.Strings.resources,System.Private.CoreLib.Strings.resources" "${illink[@]}" \
    "$o/AssemblyInfo.g.cs" "$o/SR.g.cs" "$B/gen/NativeRuntimeEventSource.Mono.cs" "${items[@]}" || die "CoreLib failed ($B/obj/System.Private.CoreLib.log)"
  ls -l "$O/System.Private.CoreLib.dll"
}

for s in $steps; do log "corelib $RT_VER: $s"; "step_$s"; done
