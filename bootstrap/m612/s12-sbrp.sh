#!/bin/bash
# S12: packages from dotnet/source-build-reference-packages (release/8.0), the text-only repository
# the VMR builds first so that no repo needs a prebuilt reference package:
#   targetpacks  targeting packs as IL text (src/targetPacks/ILsrc: NETStandard.Library 2.0.3,
#                Microsoft.NETCore.App.Ref 6.0.0/7.0.0 ...), assembled with the from-source CoreCLR
#                ilasm (coreclr.sh tree, `ninja ilasm`) exactly as SBRP's build-il.sh does
#                (incl. its 'constraint' token fix), packed with the SBRP nuspec
#   textonly     text-only packages (src/textOnlyPackages: Microsoft.NETCore.Platforms, NoTargets, ...)
#   refs         reference packages (C# API surfaces, src/referencePackages) with `dotnet build` of
#                their SBRP project files (needs the Arcade SDK of s12-arcade.sh); REFS="id/ver ..."
# All go to $S12/feed-sbrp (compile-time packages; never lifted into PackageVersions.props).
#
# usage: s12-sbrp.sh [step...]   (default: prep targetpacks textonly)
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
. "$here/../env-m612.sh"
steps=${*:-prep targetpacks textonly}
B=$M612/s12
. "$here/common.sh"
. "$here/s12-common.sh"
SRC=$S12/src/sbrp
TARGETPACKS=${TARGETPACKS:-netstandard.library/1.6.1 netstandard.library/2.0.3 microsoft.netcore.app.ref/3.0.0 microsoft.netcore.app.ref/3.1.0 microsoft.netcore.app.ref/5.0.0 microsoft.netcore.app.ref/6.0.0 microsoft.netcore.app.ref/7.0.0 netstandard.library.ref/2.1.0}

step_prep() {
  local t=$S12/src/sbrp-$SBRP_SHA.tar.gz
  fetch_tarball "https://github.com/dotnet/source-build-reference-packages/archive/$SBRP_SHA.tar.gz" "$t"
  s12_extract "$t" "$SRC"
  apply_patches "$SRC" "$here/../../patches/m612/sbrp"
  rm -f "$S12/build/sbrp-refs/.s12-prepared"     # refs works on a copy; take the patched tree next time
}

# the IL files SBRP patches before assembling (TargetingPackSrcRequiringConstraintPatch)
needs_constraint_patch() {
  case $(basename "$1") in
    Microsoft.AspNetCore.Components.il|Microsoft.AspNetCore.Mvc.Cors.il|Microsoft.AspNetCore.Routing.il|netstandard.il|\
    [pP]resentation[fF]ramework*.il|System.Activities.Presentation.il|System.ComponentModel.Composition.il|\
    System.Data.Common.il|System.Data.Entity.il|System.Data.il|System.Design.il|System.Reflection.Metadata.il|\
    System.Web.il|System.Xml.il|System.XML.il|WindowsFormsIntegration.il) return 0 ;;
  esac
  return 1
}

step_targetpacks() {
  [ -x "$ILASM" ] || die "no ilasm ($ILASM): env MSYSTEM=CLANG64 ninja -C $M612/build/coreclr-8.0.0/cmake ilasm"
  local tp T il rel out n
  for tp in $TARGETPACKS; do
    T=$S12/build/sbrp/$tp; rm -rf "${T:?}"; mkdir -p "$T"
    cp -r "$SRC/src/targetPacks/ILsrc/$tp/." "$T/"
    n=0
    : > "$T/.iljobs"
    while IFS= read -r il; do
      rel=${il#$T/}; out=$T/${rel%.il}.dll
      if needs_constraint_patch "$il"; then
        sed -E "s/ constraint(([,)])|\$)/ 'constraint'\\2/g" "$il" > "$il.patched" && mv "$il.patched" "$il"
      fi
      printf '%s\t%s\n' "$(m "$il")" "$(m "$out")" >> "$T/.iljobs"; n=$((n + 1))
    done < <(find "$T" -name '*.il' | sort)
    # ilasm writes "warning : Method has no body" for every ref method; build-il.sh filters it the same way
    tr '\t' '\n' < "$T/.iljobs" | xargs -r -d '\n' -n 2 -P 4 sh -c \
      '"$0" "$1" -dll -quiet -nologo "-output=$2" 2>&1 | grep -v "warning : Method has no body" | grep -v "^$" || true; [ -f "$2" ] || { echo "ilasm failed: $1"; exit 255; }' "$(m "$ILASM")" \
      || die "ilasm failed in $tp"
    find "$T" -name '*.il' -delete; rm -f "$T/.iljobs"
    python3 -S "$M612_TOOLS_SRC/s12nupkg.py" "$T" "$(ls "$T"/*.nuspec | head -1)" "$FEED_SBRP"
    echo "$tp: $n IL files assembled"
  done
}

step_textonly() {
  local d
  for d in "$SRC"/src/textOnlyPackages/src/*/*/; do
    python3 -S "$M612_TOOLS_SRC/s12nupkg.py" "$d" "$(ls "$d"*.nuspec | head -1)" "$FEED_SBRP"
  done
}

# reference packages: the SBRP project files, built and packed by `dotnet pack` with the stage's
# bootstrap Arcade SDK (SBRP's own global.json pins a newer 8.0 Arcade build), in dependency order
# (tools/s12sbrpdeps.py follows their PackageReferences); netstandard1.x and .NET Framework targets
# are filtered out (.NET Framework targets: no targeting packs; netstandard1.x stays, packages like
# System.Runtime.Loader 4.3.0 only have it) (Arcade's DotNetTargetFrameworkFilter; SBRP turns filtering off
# for its own CI, which still builds them)
SBRP_TFMS='netstandard1.0%3bnetstandard1.1%3bnetstandard1.2%3bnetstandard1.3%3bnetstandard1.4%3bnetstandard1.5%3bnetstandard1.6%3bnetstandard2.0%3bnetstandard2.1%3bnetcoreapp3.1%3bnet5.0%3bnet6.0%3bnet7.0%3bnet8.0'
step_refs() {
  local W=$S12/build/sbrp-refs idv p n=0 t0=$SECONDS
  [ -n "${REFS:-}" ] || die "REFS='id/version ...' (lower case, as in src/referencePackages/src)"
  if [ ! -f "$W/.s12-prepared" ]; then
    rm -rf "${W:?}"; mkdir -p "$W"; cp -r "$SRC/." "$W/"
    python3 -S "$M612_TOOLS_SRC/s12globaljson.py" "$W/global.json" 8.0.131 Microsoft.DotNet.Arcade.Sdk=8.0.0-boot
    touch "$W/.s12-prepared"
  fi
  s12_nugetconfig "$W" "$S12/s2/boot/feed"
  local list; list=$(python3 -S "$M612_TOOLS_SRC/s12sbrpdeps.py" "$SRC" "$SBRP_TFMS" $REFS | tr -d "\r")
  local extra
  for idv in $list; do
    extra=; case $idv in *:noimplicit) idv=${idv%:noimplicit}; extra=-p:DisableImplicitFrameworkReferences=true ;; esac
    [ -f "$FEED_SBRP/${idv%/*}.${idv#*/}.nupkg" ] && continue
    p=$(ls "$W/src/referencePackages/src/$idv"/*.csproj)
    rm -rf "$W/artifacts/packages"
    dn pack "$(w "$p")" -c Release -nologo -v:q -nodeReuse:false -p:DotNetBuildFromSource=true -p:ContinuousIntegrationBuild=true \
      -p:UsingToolPdbConverter=false -p:FullAssemblySigningSupported=false $(s12_kf) -p:NoTargetFrameworkFiltering=false -p:DotNetTargetFrameworkFilter="$SBRP_TFMS" $extra \
      > "$S12/logs/sbrp-$(echo "$idv" | tr / -).log" 2>&1 || { tail -20 "$S12/logs/sbrp-$(echo "$idv" | tr / -).log"; die "sbrp $idv failed"; }
    cp "$W"/artifacts/packages/Release/*/*.nupkg "$FEED_SBRP/"
    n=$((n + 1)); echo "sbrp: $idv"
  done
  echo "TIME sbrp refs: $n packages in $((SECONDS - t0)) s"
}

for s in $steps; do log "s12 sbrp: $s"; "step_$s"; done
