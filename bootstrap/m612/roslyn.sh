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
SRC=$M612/src/roslyn-$V.tar.gz
B=$M612/build/roslyn-$V
OUT=$M612/roslyn-$V
P=$here/../../patches/m612/roslyn-$V
mkdir -p "$M612/src" "$M612/build"
# csc <log-name> args... : compile via a response file (Windows' 32K command
# line limit), keep the full log in obj/<log-name>.log, show errors, fail on error.
csc() {
  local name=$1; shift
  mkdir -p "$B/obj"; local rsp="$B/obj/$name.rsp" a
  : > "$rsp"; for a in "$@"; do printf '"%s"\n' "$a" >> "$rsp"; done
  local rc=0
  mono "$BCSC" -nologo -noconfig -langversion:preview -nowarn:1701,1702,8632,8669,1573,1591 @"$rsp" > "$B/obj/$name.log" 2>&1 || rc=$?
  grep -E 'error CS' "$B/obj/$name.log" | head -${SHOW_ERRORS:-60} || true
  echo "csc $name: rc=$rc, $(grep -c 'error CS' "$B/obj/$name.log" || true) errors"
  return $rc
}

# References: the .NET Framework 4.7.2 reference assemblies shipped by the
# MSYS2 mono package (lib/mono/4.7.2-api), like a real net472 build.  Mono's
# runtime mscorlib (lib/mono/4.5) has a public Span<T> of its own, which
# clashes with System.Memory's (CS0433/CS0012), so it is not used for
# compiling.  The netstandard2.0 "package" assemblies (SCI/SRM 5.0,
# System.Memory, Unsafe, ...) come from lib/mono/4.5 of the same package.
ML=$(cygpath -m "$MONO_LIB"); RA=$(cygpath -m "$MONO_LIB/../4.7.2-api")
REFS=(-r:"$RA/mscorlib.dll" -r:"$RA/System.dll" -r:"$RA/System.Core.dll"
  -r:"$RA/System.Xml.dll" -r:"$RA/System.Xml.Linq.dll" -r:"$RA/System.Numerics.dll"
  -r:"$RA/System.IO.Compression.dll" -r:"$RA/System.Runtime.Serialization.dll"
  -r:"$RA/System.Security.dll" -r:"$RA/System.Configuration.dll"
  -r:"$RA/Facades/netstandard.dll" -r:"$RA/Facades/System.Runtime.dll" -r:"$RA/Facades/System.ValueTuple.dll"
  -r:"$RA/Facades/System.Threading.Tasks.dll" -r:"$RA/Facades/System.Collections.dll"
  ${EXTRA_PKG_REFS:-
  -r:"$ML/System.Collections.Immutable.dll" -r:"$ML/System.Reflection.Metadata.dll"}
  -r:"$ML/System.Memory.dll" -r:"$ML/System.Runtime.CompilerServices.Unsafe.dll"
  -r:"$ML/System.Threading.Tasks.Extensions.dll" -r:"$ML/System.Numerics.Vectors.dll"
  -r:"$ML/Facades/System.Text.Encoding.CodePages.dll")
COMMON=(-nostdlib -optimize+ -debug:portable -deterministic -d:TRACE -nullable:enable -unsafe)

step_prep() {
  [ -f "$SRC" ] || curl -sSL -o "$SRC" "https://github.com/dotnet/roslyn/archive/refs/tags/v$V.tar.gz"
  rm -rf "${B:?}"; mkdir -p "$B"
  tar -C "$B" --strip-components=1 -xzf "$SRC"
  # provenance: the tarball ships test-resource PE files (and nothing used by
  # the compiler build); delete every binary before building.
  scan_binaries "$B" > "$B/.deleted-binaries.txt" || true
  log "deleting $(wc -l < "$B/.deleted-binaries.txt") binary files from the source tree"
  tr '\n' '\0' < "$B/.deleted-binaries.txt" | xargs -0 -r rm -f
  [ -z "$(scan_binaries "$B")" ] || die "binaries left in $B"
  apply_patches "$B" "$P"
  mkdir -p "$B/obj"
}

resxgen() { mono "$M612/tools/ResxGen.exe" "$@"; }
step_res() {
  cd "$B"
  mkdir -p "$M612/tools"
  [ "$M612/tools/ResxGen.exe" -nt "$here/tools/ResxGen.cs" ] || \
    mono "$SEED_CSC" -nologo -out:"$M612/tools/ResxGen.exe" -r:System.Xml.dll -r:System.Core.dll "$here/tools/ResxGen.cs"
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
  # CSharpSyntaxGenerator console mode (#if NETCOREAPP in Program.cs)
  csc gen-syntax -target:exe -d:NETCOREAPP -out:obj/gen/CSharpSyntaxGenerator.exe "${REFS[@]}" -nostdlib \
    $(find $G/CSharpSyntaxGenerator -name '*.cs' ! -name SourceGenerator.cs ! -name CachingSourceGenerator.cs) \
    src/Compilers/CSharp/Portable/Syntax/SyntaxKind.cs src/Compilers/CSharp/Portable/Syntax/SyntaxKindFacts.cs \
    src/Compilers/CSharp/Portable/Declarations/DeclarationModifiers.cs src/Compilers/Core/Portable/Symbols/WellKnownMemberNames.cs
  csc gen-bound -target:exe -out:obj/gen/BoundTreeGenerator.exe "${REFS[@]}" -nostdlib $(find $G/BoundTreeGenerator -name '*.cs')
  csc gen-errfacts -target:exe -out:obj/gen/CSharpErrorFactsGenerator.exe "${REFS[@]}" -nostdlib $(find $G/CSharpErrorFactsGenerator -name '*.cs')
  # regenerate the checked-in generated sources and record any difference
  local GD=src/Compilers/CSharp/Portable/Generated
  mkdir -p obj/gen/syntax
  mono obj/gen/CSharpSyntaxGenerator.exe src/Compilers/CSharp/Portable/Syntax/Syntax.xml obj/gen/syntax
  mono obj/gen/BoundTreeGenerator.exe CSharp src/Compilers/CSharp/Portable/BoundTree/BoundNodes.xml obj/gen/BoundNodes.xml.Generated.cs
  mono obj/gen/CSharpErrorFactsGenerator.exe src/Compilers/CSharp/Portable/Errors/ErrorCode.cs obj/gen/ErrorFacts.Generated.cs
  log "diff of regenerated vs checked-in generated sources:"
  for f in obj/gen/syntax/*.cs; do diff -q --strip-trailing-cr "$f" "$GD/CSharpSyntaxGenerator/CSharpSyntaxGenerator.SourceGenerator/$(basename "$f")" || true; done
  diff -q --strip-trailing-cr obj/gen/BoundNodes.xml.Generated.cs $GD/BoundNodes.xml.Generated.cs || true
  diff -q --strip-trailing-cr obj/gen/ErrorFacts.Generated.cs $GD/ErrorFacts.Generated.cs || true
  # build only from regenerated files
  rm -rf "${GD:?}"/*.cs "${GD:?}"/CSharpSyntaxGenerator
}

ivt() { local f=$1; shift; : > "$f"; for a in "$@"; do echo "[assembly: System.Runtime.CompilerServices.InternalsVisibleTo(\"$a\")]" >> "$f"; done; }
ver() { printf '[assembly: System.Reflection.AssemblyVersion("%s")]\n[assembly: System.Reflection.AssemblyFileVersion("%s")]\n[assembly: System.Reflection.AssemblyInformationalVersion("%s (m612 bootstrap)")]\n' "${V%.*}.0.0" "$V.0" "$V" > "$1"; }

step_core() {
  cd "$B"
  ivt obj/core-ivt.cs Microsoft.CodeAnalysis.CSharp Microsoft.CodeAnalysis.VisualBasic Microsoft.CodeAnalysis.Scripting \
    Microsoft.CodeAnalysis.CSharp.Scripting csc csi vbc VBCSCompiler Microsoft.Build.Tasks.CodeAnalysis
  ver obj/core-ver.cs
  local extra=(); [ -f obj/CollectionsStrings.g.cs ] && extra+=(obj/CollectionsStrings.g.cs -resource:obj/Microsoft.CodeAnalysis.Collections.Internal.Strings.resources)
  csc core "${COMMON[@]}" -target:library -out:obj/Microsoft.CodeAnalysis.dll -d:COMPILERCORE "${REFS[@]}" \
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
  csc csharp "${COMMON[@]}" -target:library -out:obj/Microsoft.CodeAnalysis.CSharp.dll "${REFS[@]}" -r:obj/Microsoft.CodeAnalysis.dll \
    -resource:obj/Microsoft.CodeAnalysis.CSharp.CSharpResources.resources \
    obj/CSharpResources.g.cs obj/cs-ivt.cs obj/cs-ver.cs \
    $(find src/Compilers/CSharp/Portable src/Compilers/CSharp/CSharpAnalyzerDriver -name '*.cs') \
    $(find obj/gen -name '*.cs') || die "CSharp failed (obj/csharp.log)"
}

step_csc() {
  cd "$B"
  ver obj/csc-ver.cs
  csc csc "${COMMON[@]}" -target:exe -out:obj/csc.exe -d:NET472 -d:NETFRAMEWORK "${REFS[@]}" -r:obj/Microsoft.CodeAnalysis.dll -r:obj/Microsoft.CodeAnalysis.CSharp.dll \
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
  # runtime dependencies next to csc.exe (they are not in Mono's GAC).
  # Default: the netstandard "package" assemblies of the trusted Mono seed;
  # RUNTIME_DEPS overrides with from-source builds (see sci-srm.sh).
  local d; for d in ${RUNTIME_DEPS:-$MONO_LIB/System.Collections.Immutable.dll $MONO_LIB/System.Reflection.Metadata.dll} \
    $MONO_LIB/System.Memory.dll $MONO_LIB/System.Runtime.CompilerServices.Unsafe.dll $MONO_LIB/System.Threading.Tasks.Extensions.dll \
    $MONO_LIB/System.Numerics.Vectors.dll; do cp "$d" "$OUT/"; done
  # default response file without the .NET Framework facade list
  printf '%s\n' -r:System.dll -r:System.Core.dll -r:System.Xml.dll -r:System.Xml.Linq.dll -r:Microsoft.CSharp.dll \
    -r:System.Numerics.dll -r:System.Data.dll -r:System.Net.Http.dll > "$OUT/csc.rsp"
  # (not $OUT/csc: MSYS2 would resolve that name to csc.exe and overwrite it)
  mkdir -p "$OUT/bin"
  printf '#!/bin/sh\nexec mono "%s/csc.exe" "$@"\n' "$(cygpath -m "$OUT")" > "$OUT/bin/csc"; chmod +x "$OUT/bin/csc"
}

step_test() {
  local T=$B/obj/test; mkdir -p "$T"; cd "$T"
  cp "$here/tests/cs10.cs" .
  mono "$OUT/csc.exe" -nologo -langversion:latest -out:cs10.exe "$(cygpath -m "$here/tests/cs10.cs")" "$(cygpath -m "$here/tests/polyfill.cs")"
  mono cs10.exe
}

for s in $steps; do log "roslyn $V: $s"; "step_$s"; done
