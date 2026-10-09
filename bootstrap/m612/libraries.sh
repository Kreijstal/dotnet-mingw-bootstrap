#!/bin/bash
# Build the implementation assemblies of Microsoft.NETCore.App 8.0 (the
# src/ projects of dotnet/runtime src/libraries, net8.0-windows where the
# project has it, else net8.0) with direct csc command lines, against
# System.Private.CoreLib (corelib.sh), the other implementation assemblies
# (ProjectReference) and the reference pack of refpack.sh (Reference).
#
#   plan    tools/libplan.py evaluates every project once -> $B/plan.tsv, $B/p/<name>/
#   gens    the Roslyn source generators the projects use, from source (netfx-pkgs.sh)
#   build   compile in dependency order; partial facades (IsPartialFacadeAssembly)
#           get their type forwards from tools/genfacades.py (GenFacades without
#           Arcade); the shims (mscorlib, netstandard, System.Core, ...) are the
#           ones refpack.sh compiled, except System/System.Data/mscorlib, whose ref
#           pack versions come from shims/*/ref and are rebuilt from shims/*/src
#   report  list what was built and skipped
#
# usage: libraries.sh <csc.exe> [step...]    (default: plan gens build report)
#   ONLY="A B" builds just those (and rebuilds them); REBUILD=1 rebuilds all
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
. "$here/../env-m612.sh"
BCSC=${1:?csc.exe}; shift
steps=${*:-plan gens build report}
RT_VER=${RT_VER:-8.0.0}; TFM=net${RT_VER%%.*}.0
SRC=$M612/src/runtime-$RT_VER-native
L=$SRC/src/libraries
B=$M612/build/libraries-$RT_VER
O=${FXOUT:-$M612/netcoreapp-$RT_VER}/lib
RP=$M612/build/refpack-$RT_VER/out          # reference assemblies (+ .meta: name version token)
STUBS=$M612/build/refpack-$RT_VER/stubs
PKGDIR=${PKGDIR:-$M612/pkgs}
A=$PKGDIR/analyzers
. "$here/common.sh"
mkdir -p "$B" "$O"
COMMIT=$(cat "$SRC/.commit" 2>/dev/null || echo unknown)

step_plan() { python3 -S "$M612_TOOLS_SRC/libplan.py" "$SRC" "$TFM" "$B" | grep -v '^\[fc-sc\]'; }

# generator project name -> analyzer dlls (built by netfx-pkgs.sh recipes)
gen_dlls() {
  case $1 in
    LibraryImportGenerator) echo Microsoft.Interop.SourceGeneration Microsoft.Interop.LibraryImportGenerator ;;
    ComInterfaceGenerator) echo Microsoft.Interop.SourceGeneration Microsoft.Interop.ComInterfaceGenerator ;;
    Microsoft.Interop.SourceGeneration) echo Microsoft.Interop.SourceGeneration ;;
    # csc 4.8 on Mono ships no System.Buffers; registering the from-source one as an
    # analyzer path lets Roslyn's analyzer loader resolve the generator's dependency
    System.Text.RegularExpressions.Generator) echo System.Text.RegularExpressions.Generator System.Buffers ;;
    JSImportGenerator) ;;                                   # browser only
    *) die "unknown generator $1" ;;
  esac
}
step_gens() {
  local g d
  for g in LibraryImportGenerator ComInterfaceGenerator $(cat "$B"/p/*/analyzers | sort -u); do
    for d in $(gen_dlls "$g"); do
      [ "$d" = System.Buffers ] && { cp "$PKGDIR/System.Buffers.dll" "$A/"; continue; }
      [ -f "$A/$d.dll" ] || bash "$here/netfx-pkgs.sh" "$BCSC" "$PKGDIR" "$d"
    done
  done
}

prop() { sed -n "s/^$2=//p" "$B/p/$1/props"; }
key_of_token() {
  case $1 in
    b03f5f7f11d50a3a) echo Microsoft ;; cc7b13ffcd2ddd51) echo Open ;; 31bf3856ad364e35) echo MSShared ;;
    adb9793829ddae60) echo MicrosoftAspNetCore ;; b77a5c561934e089) echo ECMA ;; 7cec85d7bea7798e) echo SilverlightPlatform ;;
    *) die "unknown key token $1" ;;
  esac
}

# compile <name> [extra csc args...] -> $O/<name>.dll
compile_lib() {
  local n=$1; shift
  local P=$B/p/$n o=$B/obj/$n items=() refs=() an=() extra=() dargs=() d kind
  rm -rf "${o:?}"; mkdir -p "$o"
  kind=$(prop "$n" kind)
  mapfile -t items < "$P/items"
  # version and key: the same identity as the reference assembly
  local av key
  if [ -f "$RP/$n.meta" ]; then local _n tok; read -r _n av tok < "$RP/$n.meta"; key=$(key_of_token "$tok")
  else av=${RT_VER%%.*}.0.0.0; key=$(prop "$n" StrongNameKeyId); key=${key:-Open}; [ "$key" = MicrosoftShared ] && key=MSShared; fi
  # references
  local kindref r rn haveinterop=
  while read -r kindref rn; do
    [ -n "$rn" ] || continue
    [ "$rn" = System.Runtime.InteropServices ] && haveinterop=1
    [ "$rn" = System.Private.CoreLib ] && haveinterop=1
    if [ "$kindref" = impl ] && [ -f "$O/$rn.dll" ]; then refs+=("-r:$O/$rn.dll")
    elif [ "$kindref" = impl ]; then echo "  note: $n: project reference $rn is not a framework assembly; skipped"
    elif [ -f "$RP/$rn.dll" ]; then refs+=("-r:$RP/$rn.dll")
    elif [ -f "$O/$rn.dll" ]; then refs+=("-r:$O/$rn.dll")
    else echo "  note: $n: reference $rn not found"; fi
  done < "$P/refs"
  if [ "$kind" = shim ]; then
    # checked-in forwards (System, System.Data, mscorlib also forward implementation-only
    # serialization types such as TreeSet<T>): compile against the implementation
    # assemblies and the OOB stubs
    refs=(); for d in "$O"/*.dll "$STUBS"/*.dll; do [ "$(basename "$d" .dll)" = "$n" ] || refs+=("-r:$d"); done
  fi
  # defines: the project's, plus what the SDK adds (Release, TargetFramework, TargetPlatform)
  local dl=(); IFS=';' read -ra dl < "$P/defines"
  for d in "${dl[@]}" TRACE RELEASE NETCOREAPP NET NET${RT_VER%%.*}_0 NET8_0_OR_GREATER NET7_0_OR_GREATER NET6_0_OR_GREATER \
      NET5_0_OR_GREATER NETCOREAPP3_1_OR_GREATER NETCOREAPP3_0_OR_GREATER NETCOREAPP2_1_OR_GREATER NETCOREAPP2_0_OR_GREATER \
      NETCOREAPP1_1_OR_GREATER NETCOREAPP1_0_OR_GREATER; do [ -n "$d" ] && dargs+=("-d:$d"); done
  [ "$(prop "$n" tfm)" = "$TFM-windows" ] && dargs+=(-d:WINDOWS -d:WINDOWS7_0_OR_GREATER)
  # string resources (eng/resources.targets): FxResources.<Asm>.SR + Common/src/System/SR.cs
  local sres; sres=$(prop "$n" StringResourcesPath); local pd; pd=$(dirname "$(prop "$n" project)")
  [ -z "$sres" ] && [ -f "$pd/Resources/Strings.resx" ] && sres=$pd/Resources/Strings.resx
  [ -n "$sres" ] && [[ $sres != /* ]] && sres=$(cd "$pd" && realpath -m "$sres")
  local nres=0
  if [ -n "$sres" ] && [ "$(prop "$n" OmitResources)" != true ] && [ "$kind" != shim ]; then
    resgen "$sres" "$o/FxResources.$n.SR.resources" > /dev/null
    resxgen "$sres" "$o/SR.g.cs" System.SR --fx "FxResources.$n.SR"
    extra+=("$o/SR.g.cs" "$L/Common/src/System/SR.cs" -resource:"$o/FxResources.$n.SR.resources,FxResources.$n.SR.resources")
    nres=1
  fi
  local rp rlog rgen rcls
  while IFS=$'\t' read -r rp rlog rgen rcls; do
    [ -n "$rp" ] || continue
    [ "$rp" = "$sres" ] && continue
    case $rp in
      *.resx) local rn2=${rlog:-$n.$(basename "$rp" .resx)}; rn2=${rn2%.resources}
              resgen "$rp" "$o/$rn2.resources" > /dev/null; extra+=(-resource:"$o/$rn2.resources,$rn2.resources") ;;
      *) extra+=(-resource:"$rp,${rlog:-$n.$(basename "$rp")}") ;;
    esac
    nres=1
  done < "$P/resources"
  # generators (eng/generators.targets) + the project's own analyzer references
  local gl=() g
  if [ "$kind" = src ] && [ -n "$haveinterop" ] && [ "$(prop "$n" EnableLibraryImportGenerator)" != false ]; then gl+=(LibraryImportGenerator); fi
  if [ "$kind" = src ] && grep -qx 'ref System.Runtime.InteropServices' "$P/refs"; then gl+=(ComInterfaceGenerator); fi
  mapfile -t -O "${#gl[@]}" gl < "$P/analyzers"
  local seen=" "
  for g in "${gl[@]}"; do for d in $(gen_dlls "$g"); do
    [[ $seen == *" $d "* ]] && continue; seen+="$d "; an+=(-analyzer:"$A/$d.dll"); done; done
  if [ ${#an[@]} -gt 0 ]; then
    printf 'is_global = true\nbuild_property.TargetFrameworkIdentifier = .NETCoreApp\nbuild_property.TargetFrameworkVersion = v%s\nbuild_property.LibraryImportGenerator_UseMarshalType = true\n' "${TFM#net}" > "$o/build.globalconfig"
    an+=(-analyzerconfig:"$o/build.globalconfig")
  fi
  # SkipLocalsInit (src/libraries/Directory.Build.targets) for shared-framework sources
  [ "$kind" = src ] && [ "$(prop "$n" SkipLocalsInit)" != false ] && extra+=("$L/Common/src/SkipLocalsInit.cs")
  # assembly attributes: SDK GenerateAssemblyInfo + eng/versioning.targets
  {
    printf '[assembly: System.Reflection.AssemblyVersion("%s")]\n[assembly: System.Reflection.AssemblyFileVersion("%s")]\n' "$av" "$RT_VER.0"
    printf '[assembly: System.Reflection.AssemblyInformationalVersion("%s+%s")]\n' "$RT_VER" "$COMMIT"
    printf '[assembly: System.Reflection.AssemblyCompany("Microsoft Corporation")]\n[assembly: System.Reflection.AssemblyProduct("Microsoft\\u00AE .NET")]\n'
    printf '[assembly: System.Reflection.AssemblyCopyright("\\u00A9 Microsoft Corporation. All rights reserved.")]\n'
    printf '[assembly: System.Reflection.AssemblyTitle("%s")]\n[assembly: System.Reflection.AssemblyDescription("%s")]\n' "$n" "$n"
    printf '[assembly: System.Reflection.AssemblyDefaultAlias("%s")]\n' "$n"
    printf '[assembly: System.Reflection.AssemblyMetadata("Serviceable", "True")]\n[assembly: System.Reflection.AssemblyMetadata("PreferInbox", "True")]\n'
    printf '[assembly: System.Runtime.Versioning.TargetFramework(".NETCoreApp,Version=v%s", FrameworkDisplayName = ".NET %s")]\n' "${TFM#net}" "${TFM#net}"
    [ $nres = 1 ] && [ "$kind" = src ] && echo '[assembly: System.Resources.NeutralResourcesLanguage("en-US")]'
    [ "$(prop "$n" CLSCompliant)" != false ] && [ "$kind" = src ] && echo '[assembly: System.CLSCompliant(true)]'
    [ -n "$haveinterop" ] && [ "$kind" = src ] && echo '[assembly: System.Runtime.InteropServices.DefaultDllImportSearchPaths(System.Runtime.InteropServices.DllImportSearchPath.AssemblyDirectory | System.Runtime.InteropServices.DllImportSearchPath.System32)]'
    if [ "$(prop "$n" tfm)" = "$TFM" ]; then
      local pl; for pl in $(prop "$n" SupportedOSPlatforms | tr ';' ' '); do echo "[assembly: System.Runtime.Versioning.SupportedOSPlatform(\"$pl\")]"; done
      for pl in $(prop "$n" UnsupportedOSPlatforms | tr ';' ' '); do echo "[assembly: System.Runtime.Versioning.UnsupportedOSPlatform(\"$pl\")]"; done
    fi
    [ "$kind" = src ] && cat "$P/attributes"   # the project's own AssemblyAttribute items
  } > "$o/AssemblyInfo.g.cs"
  local nullable; nullable=$(prop "$n" Nullable); nullable=${nullable:-enable}
  [ "$kind" = shim ] && nullable=disable
  csc "$n" -target:library -nostdlib -noconfig -optimize+ -deterministic -unsafe -nullable:$nullable -debug:portable -warn:0 \
    -out:"$o/$n.dll" -publicsign -keyfile:"$(keyfile "$key")" "${dargs[@]}" "${refs[@]}" "${an[@]}" \
    -nowarn:CS0618,CS0809,CS3021,CS0067,CS0169,CS0649,CS8597,CS8625,CS8617,CS0436,CS1591,CS0108,CS0114,CS3016,CS8618,CS0672,CS0612,SYSLIB0003,SYSLIB0004 \
    "$@" "${extra[@]}" "$o/AssemblyInfo.g.cs" "${items[@]}"
}

build_one() {
  local n=$1 kind; kind=$(prop "$n" kind)
  case $n in
    System|System.Data|mscorlib) ;;                       # shims whose ref pack copy is a ref project
    *) if [ "$kind" = shim ]; then cp "$RP/$n.dll" "$O/$n.dll"; echo "  shim $n: copied from the reference pack build"; return 0; fi ;;
  esac
  if [ -n "$(prop "$n" GeneratePlatformNotSupportedAssemblyMessage)$(prop "$n" GeneratePlatformNotSupportedAssembly)" ]; then
    build_pnse "$n"; return
  fi
  if [ "$(prop "$n" IsPartialFacadeAssembly)" = true ]; then
    local o=$B/obj/$n
    compile_lib "$n" || return 1
    local seeds=() rn kindref omit=()
    while read -r kindref rn; do
      [ -n "$rn" ] || continue
      if [ "$kindref" = impl ] && [ -f "$O/$rn.dll" ]; then seeds+=("$(winpath "$O/$rn.dll")")
      elif [ -f "$RP/$rn.dll" ]; then seeds+=("$(winpath "$RP/$rn.dll")"); fi
    done < "$B/p/$n/refs"
    while read -r rn; do [ -n "$rn" ] && omit+=(--omit "$rn"); done < "$B/p/$n/omit"
    mv "$o/$n.dll" "$o/$n.partial.dll"
    python3 -S "$M612_TOOLS_SRC/genfacades.py" "$(winpath "$RP/$n.dll")" "$(winpath "$o/$n.partial.dll")" "$B/obj/$n.forwards.cs" \
      "${omit[@]}" "${seeds[@]}" | grep -v '^\[fc-sc\]' || return 1
    compile_lib "$n" "$B/obj/$n.forwards.cs" || return 1
  else
    compile_lib "$n" || return 1
  fi
  cp "$B/obj/$n/$n.dll" "$O/$n.dll"
}

# PlatformNotSupported assemblies (GenAPI -> PNSE source in Arcade): the
# project's reference source with every "throw null" body throwing PNSE.
build_pnse() {
  local n=$1 o=$B/obj/$n f
  rm -rf "${o:?}"; mkdir -p "$o/src"
  local msg; msg=$(prop "$n" GeneratePlatformNotSupportedAssemblyMessage)
  for f in "$L/$n"/ref/*.cs; do
    sed 's/throw null;/throw new System.PlatformNotSupportedException();/g' "$f" > "$o/src/$(basename "$f")"
  done
  local _n av tok; read -r _n av tok < "$RP/$n.meta"
  printf '[assembly: System.Reflection.AssemblyVersion("%s")]\n[assembly: System.Reflection.AssemblyFileVersion("%s")]\n[assembly: System.Reflection.AssemblyInformationalVersion("%s+%s")]\n' \
    "$av" "$RT_VER.0" "$RT_VER" "$COMMIT" > "$o/AssemblyInfo.g.cs"
  local refs=() d; for d in "$RP"/*.dll; do [ "$(basename "$d" .dll)" = "$n" ] || refs+=("-r:$d"); done
  csc "$n" -target:library -nostdlib -noconfig -optimize+ -deterministic -unsafe -nullable:enable -debug- -warn:0 \
    -out:"$o/$n.dll" -publicsign -keyfile:"$(keyfile "$(key_of_token "$tok")")" "${refs[@]}" "$o/AssemblyInfo.g.cs" "$o"/src/*.cs || return 1
  cp "$o/$n.dll" "$O/$n.dll"; echo "  $n: PlatformNotSupported assembly ($msg)"
}

step_build() {
  local n kind tfm deps failed=()
  : > "$B/failed.txt"
  while IFS=$'\t' read -r n kind tfm deps; do
    if [ -n "${ONLY:-}" ]; then [[ " $ONLY " == *" $n "* ]] || continue
    elif [ -f "$O/$n.dll" ] && [ -z "${REBUILD:-}" ]; then continue; fi
    log "lib $n ($kind, $tfm)"
    build_one "$n" || { failed+=("$n"); echo "$n" >> "$B/failed.txt"; }
  done < "$B/plan.tsv"
  [ ${#failed[@]} -eq 0 ] || echo "FAILED (${#failed[@]}): ${failed[*]}"
}

step_report() {
  local n kind tfm deps ok=0 bad=()
  while IFS=$'\t' read -r n kind tfm deps; do
    if [ -f "$O/$n.dll" ]; then ok=$((ok+1)); else bad+=("$n"); fi
  done < "$B/plan.tsv"
  echo "built: $ok of $(wc -l < "$B/plan.tsv") planned (+ System.Private.CoreLib)"
  [ ${#bad[@]} -eq 0 ] || echo "not built: ${bad[*]}"
  echo "skipped by the plan:"; sed 's/^/  /' "$B/skipped.tsv"
}

for s in $steps; do log "libraries $RT_VER: $s"; "step_$s"; done
