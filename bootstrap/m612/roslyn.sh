#!/bin/bash
# Build Roslyn (C# compiler only) from a source tarball with direct csc
# command lines, Guix style, running on the Mono 6.12 seed.
#
# usage: roslyn.sh <version> <bootstrap-csc.exe> [step...]
#   steps: prep res gen core csharp csc install test   (default: all)
# Output prefix: $M612/roslyn-<version>/  (csc.exe + Microsoft.CodeAnalysis*.dll)
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
. "$here/../env-m612.sh"
V=${1:?version}; BCSC=${2:?bootstrap csc.exe}; shift 2
steps=${*:-prep res gen core csharp csc install test}
# git tag of version V (4.3+ are only tagged as Visual-Studio-2022-Version-17.N)
case $V in
  4.0.1|4.1.0|4.2.0) TAG=v$V ;;
  4.*.0) _m=${V#4.}; TAG=Visual-Studio-2022-Version-17.${_m%.0} ;;
  *) TAG=${ROSLYN_TAG:?set ROSLYN_TAG for $V} ;;
esac
TAG=${ROSLYN_TAG:-$TAG}
SRC=$M612/src/roslyn-$V.tar.gz
B=$M612/build/roslyn-$V
OUT=$M612/roslyn-$V
P=$here/../../patches/m612/roslyn-$V
mkdir -p "$M612/src" "$M612/build"
. "$here/common.sh"
REFS=("${NETFX_REFS[@]}" $(pkg_refs))
COMMON=(-nostdlib -optimize+ -debug:portable -deterministic -d:TRACE -nullable:enable -unsafe)
# public-sign with the upstream (public) key so the identities match the
# Microsoft.CodeAnalysis packages analyzers are compiled against
SIGN=(-publicsign -keyfile:"$(keyfile MSShared)")

step_prep() {
  fetch_tarball "https://github.com/dotnet/roslyn/archive/refs/tags/$TAG.tar.gz" "$SRC"
  rm -rf "${B:?}"; mkdir -p "$B"
  tar -C "$B" --strip-components=1 -xzf "$SRC"
  # provenance: the tarball ships test-resource PE files (and nothing used by
  # the compiler build); delete every binary before building.
  delete_binaries "$B"
  apply_patches "$B" "$P"
  mkdir -p "$B/obj"
}

step_res() {
  cd "$B"
  resgen src/Compilers/Core/Portable/CodeAnalysisResources.resx obj/Microsoft.CodeAnalysis.CodeAnalysisResources.resources
  resgen src/Compilers/CSharp/Portable/CSharpResources.resx obj/Microsoft.CodeAnalysis.CSharp.CSharpResources.resources
  resxgen src/Compilers/Core/Portable/CodeAnalysisResources.resx obj/CodeAnalysisResources.g.cs Microsoft.CodeAnalysis.CodeAnalysisResources
  resxgen src/Compilers/CSharp/Portable/CSharpResources.resx obj/CSharpResources.g.cs Microsoft.CodeAnalysis.CSharp.CSharpResources
  if [ -f src/Dependencies/Collections/Internal/Strings.resx ]; then
    resgen src/Dependencies/Collections/Internal/Strings.resx obj/Microsoft.CodeAnalysis.Collections.Internal.Strings.resources
    resxgen src/Dependencies/Collections/Internal/Strings.resx obj/CollectionsStrings.g.cs Microsoft.CodeAnalysis.Collections.Internal.SR \
      --resource-name Microsoft.CodeAnalysis.Collections.Internal.Strings
  fi
}

step_gen() {
  cd "$B"; mkdir -p obj/gen
  local G=src/Tools/Source/CompilerGeneratorTools/Source
  # net472 has no System.Index/Range: reuse Roslyn's own polyfills, whose
  # "#if !NETCOREAPP" would hide them under the -d:NETCOREAPP below.
  mkdir -p obj/polyfill; local f
  for f in src/Compilers/Core/Portable/InternalUtilities/{Index,Range}.cs; do
    [ -f "$f" ] && sed 's/^#if !NETCOREAPP/#if true/; /^using Roslyn.Utilities;/d; s/Hash.Combine(\(.*\), \(.*\));/unchecked(\1 * 31 + \2);/' "$f" > obj/polyfill/$(basename "$f")
  done
  # CSharpSyntaxGenerator console mode (#if NETCOREAPP in Program.cs)
  csc gen-syntax -target:exe -d:NETCOREAPP -d:NET -out:obj/gen/CSharpSyntaxGenerator.exe "${TOOLREFS[@]}" -nostdlib \
    $(find $G/CSharpSyntaxGenerator -name '*.cs' ! -name SourceGenerator.cs ! -name CachingSourceGenerator.cs) \
    src/Compilers/CSharp/Portable/Syntax/SyntaxKind.cs src/Compilers/CSharp/Portable/Syntax/SyntaxKindFacts.cs \
    src/Compilers/CSharp/Portable/Declarations/DeclarationModifiers.cs src/Compilers/Core/Portable/Symbols/WellKnownMemberNames.cs \
    ${GEN_POLYFILL:+$(ls obj/polyfill/*.cs 2>/dev/null)}
  csc gen-bound -target:exe -out:obj/gen/BoundTreeGenerator.exe "${TOOLREFS[@]}" -nostdlib $(find $G/BoundTreeGenerator -name '*.cs')
  csc gen-errfacts -target:exe -out:obj/gen/CSharpErrorFactsGenerator.exe "${TOOLREFS[@]}" -nostdlib $(find $G/CSharpErrorFactsGenerator -name '*.cs')
  # regenerate the checked-in generated sources and record any difference
  local GD=src/Compilers/CSharp/Portable/Generated
  mkdir -p obj/gen/syntax
  mono obj/gen/CSharpSyntaxGenerator.exe src/Compilers/CSharp/Portable/Syntax/Syntax.xml obj/gen/syntax
  mono obj/gen/BoundTreeGenerator.exe CSharp src/Compilers/CSharp/Portable/BoundTree/BoundNodes.xml obj/gen/BoundNodes.xml.Generated.cs
  mono obj/gen/CSharpErrorFactsGenerator.exe src/Compilers/CSharp/Portable/Errors/ErrorCode.cs obj/gen/ErrorFacts.Generated.cs
  log "diff of regenerated vs checked-in generated sources:"
  for f in obj/gen/syntax/*.cs; do diff -qBw --strip-trailing-cr "$f" "$GD/CSharpSyntaxGenerator/CSharpSyntaxGenerator.SourceGenerator/$(basename "$f")" || true; done
  diff -qBw --strip-trailing-cr obj/gen/BoundNodes.xml.Generated.cs $GD/BoundNodes.xml.Generated.cs || true
  diff -qBw --strip-trailing-cr obj/gen/ErrorFacts.Generated.cs $GD/ErrorFacts.Generated.cs || true
  # build only from regenerated files
  rm -rf "${GD:?}"/*.cs "${GD:?}"/CSharpSyntaxGenerator
}

IVT_PK=$(tr -d '\n\r' < "$KEYS"/MSShared-*.pubkey.hex)
ivt() { local f=$1; shift; : > "$f"; for a in "$@"; do echo "[assembly: System.Runtime.CompilerServices.InternalsVisibleTo(\"$a, PublicKey=$IVT_PK\")]" >> "$f"; done; }
ver() { printf '[assembly: System.Reflection.AssemblyVersion("%s")]\n[assembly: System.Reflection.AssemblyFileVersion("%s")]\n[assembly: System.Reflection.AssemblyInformationalVersion("%s (m612 bootstrap)")]\n' "${V%.*}.0.0" "$V.0" "$V" > "$1"; }

step_core() {
  cd "$B"
  ivt obj/core-ivt.cs Microsoft.CodeAnalysis.CSharp Microsoft.CodeAnalysis.VisualBasic Microsoft.CodeAnalysis.Scripting \
    Microsoft.CodeAnalysis.CSharp.Scripting csc csi vbc VBCSCompiler Microsoft.Build.Tasks.CodeAnalysis
  ver obj/core-ver.cs
  local extra=(); [ -f obj/CollectionsStrings.g.cs ] && extra+=(obj/CollectionsStrings.g.cs -resource:obj/Microsoft.CodeAnalysis.Collections.Internal.Strings.resources)
  csc core "${COMMON[@]}" "${SIGN[@]}" -target:library -out:obj/Microsoft.CodeAnalysis.dll -d:COMPILERCORE "${REFS[@]}" \
    -resource:obj/Microsoft.CodeAnalysis.CodeAnalysisResources.resources \
    -resource:src/Compilers/Core/Portable/Resources/default.win32manifest,Microsoft.CodeAnalysis.Resources.default.win32manifest \
    obj/CodeAnalysisResources.g.cs obj/core-ivt.cs obj/core-ver.cs "${extra[@]}" \
    $(find src/Compilers/Core/Portable src/Compilers/Core/AnalyzerDriver src/Dependencies/CodeAnalysis.Debugging \
           src/Dependencies/PooledObjects src/Dependencies/Collections -name '*.cs' ! -path '*/obj/*' ! -name '*.Package.*') \
    || die "Microsoft.CodeAnalysis failed (obj/core.log)"
}

step_csharp() {
  cd "$B"
  ivt obj/cs-ivt.cs csc csi Microsoft.CodeAnalysis.CSharp.Scripting VBCSCompiler
  ver obj/cs-ver.cs
  csc csharp "${COMMON[@]}" "${SIGN[@]}" -target:library -out:obj/Microsoft.CodeAnalysis.CSharp.dll "${REFS[@]}" -r:obj/Microsoft.CodeAnalysis.dll \
    -resource:obj/Microsoft.CodeAnalysis.CSharp.CSharpResources.resources \
    obj/CSharpResources.g.cs obj/cs-ivt.cs obj/cs-ver.cs \
    $(find src/Compilers/CSharp/Portable src/Compilers/CSharp/CSharpAnalyzerDriver -name '*.cs') \
    $(find obj/gen -name '*.cs') || die "CSharp failed (obj/csharp.log)"
}

step_csc() {
  cd "$B"
  ver obj/csc-ver.cs
  csc csc "${COMMON[@]}" "${SIGN[@]}" -target:exe -out:obj/csc.exe -d:NET472 -d:NETFRAMEWORK "${REFS[@]}" -r:obj/Microsoft.CodeAnalysis.dll -r:obj/Microsoft.CodeAnalysis.CSharp.dll \
    -main:Microsoft.CodeAnalysis.CSharp.CommandLine.Program obj/csc-ver.cs \
    src/Compilers/CSharp/csc/Program.cs src/Compilers/Shared/RuntimeHostInfo.cs src/Compilers/Shared/NamedPipeUtil.cs \
    src/Compilers/Shared/BuildClient.cs src/Compilers/Shared/BuildServerConnection.cs src/Compilers/Shared/ExitingTraceListener.cs \
    src/Compilers/Shared/Csc.cs $(find src/Compilers/Core/CommandLine -name '*.cs') || die "csc failed (obj/csc.log)"
}

step_install() {
  rm -rf "${OUT:?}"; mkdir -p "$OUT"
  cp "$B"/obj/{Microsoft.CodeAnalysis.dll,Microsoft.CodeAnalysis.CSharp.dll,csc.exe} "$OUT/"
  cp "$B"/obj/*.pdb "$OUT/" 2>/dev/null || true
  cp "$B/src/Compilers/CSharp/csc/App.config" "$OUT/csc.exe.config"
  # runtime dependencies next to csc.exe (they are not in Mono's GAC):
  # the package assemblies from PKGDIR (from source) or the Mono seed.
  local d; for d in $(pkg_runtime_files System.Collections.Immutable System.Reflection.Metadata System.Memory \
    System.Buffers System.Runtime.CompilerServices.Unsafe System.Threading.Tasks.Extensions System.Numerics.Vectors); do
    cp "$d" "$OUT/"; done
  [ -z "${PKGDIR:-}" ] || echo "$PKGDIR" > "$OUT/.pkgdir"
  # default response file without the .NET Framework facade list
  printf '%s\n' -r:System.dll -r:System.Core.dll -r:System.Xml.dll -r:System.Xml.Linq.dll -r:Microsoft.CSharp.dll \
    -r:System.Numerics.dll -r:System.Data.dll -r:System.Net.Http.dll > "$OUT/csc.rsp"
  # (not $OUT/csc: MSYS2 would resolve that name to csc.exe and overwrite it)
  mkdir -p "$OUT/bin"
  printf '#!/bin/sh\nexec mono "%s/csc.exe" "$@"\n' "$(cygpath -m "$OUT")" > "$OUT/bin/csc"; chmod +x "$OUT/bin/csc"
}

step_test() {
  # compile and run every tests/csNN.cs up to the language version of $V
  local T=$B/obj/test maxlang n t rc=0; mkdir -p "$T"; cd "$T"
  case $V in 4.0.*|4.1.*|4.2.*|4.3.*) maxlang=10 ;; 4.4.*|4.5.*|4.6.*|4.7.*) maxlang=11 ;; 4.8.*|4.9.*|4.10.*|4.11.*) maxlang=12 ;; 4.1[2-9].*) maxlang=13 ;; *) maxlang=14 ;; esac
  for t in "$here"/tests/cs[0-9][0-9].cs; do
    n=$(basename "$t" .cs); [ "${n#cs}" -le "$maxlang" ] || continue
    mono "$OUT/csc.exe" -nologo -langversion:latest -out:$n.exe "$(cygpath -m "$t")" "$(cygpath -m "$here/tests/polyfill.cs")" || { rc=1; continue; }
    mono $n.exe || rc=1
  done
  return $rc
}

for s in $steps; do log "roslyn $V: $s"; "step_$s"; done
