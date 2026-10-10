#!/bin/bash
# Build the Microsoft.NETCore.App.Ref targeting pack (net8.0 reference
# assemblies + facades + data/FrameworkList.xml) from the dotnet/runtime
# sources (src/libraries/*/ref and src/libraries/shims), with direct csc
# command lines on the Mono 6.12 seed.  Nothing here runs on .NET: these are
# compile-time reference assemblies only.
#
# usage: refpack.sh <csc.exe> [step...]       steps: plan refs pack test
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
. "$here/../env-m612.sh"
BCSC=${1:?csc.exe}; shift
steps=${*:-plan refs pack test}
RT_VER=${RT_VER:-8.0.0}; PACK_VER=${PACK_VER:-8.0.31}; TFM=net${RT_VER%%.*}.0
RT=$M612/src/runtime-$RT_VER
L=$RT/src/libraries
B=${REFPACK_B:-$M612/build/refpack-$RT_VER}
O=$B/out
SDKROOT=${SDKROOT:-$M612/dotnet-sdk-8.0.131}
PACK=$SDKROOT/packs/Microsoft.NETCore.App.Ref/$PACK_VER
. "$here/common.sh"
mkdir -p "$B" "$O"

# plan: list the targeting pack assemblies with their project, kind and
# (transitive) project references, in build order -> $B/plan.tsv
step_plan() {
  python3 -S - "$L" "$TFM" "$B/plan.tsv" "$M612_TOOLS_SRC" <<'EOPY'
import re, os, sys, subprocess
L, tfm, out, tools = sys.argv[1:5]
sys.path.insert(0, tools)
import csproj_items as ci
t = open(os.path.join(L, 'NetCoreAppLibrary.props')).read()
netfx = re.search(r'<NetFxReference>(.*?)</NetFxReference>', t, re.S).group(1)
names = [x.strip() for x in re.search(r'<NetCoreAppLibrary>(.*?)</NetCoreAppLibrary>', t, re.S).group(1)
         .replace('$(NetFxReference)', netfx).split(';') if x.strip() and not x.strip().startswith('System.Private.')]
proj = {}
for n in names:
    for kind, p in (('ref', f'{n}/ref/{n}.csproj'), ('shimref', f'shims/{n}/ref/{n}.csproj'), ('shim', f'shims/{n}/src/{n}.csproj')):
        if os.path.exists(os.path.join(L, p)):
            proj[n] = (kind, os.path.join(L, p)); break
deps = {}
stubs = {}
for n, (kind, p) in list(proj.items()):
    props = {'TargetFramework': tfm, 'NetCoreAppCurrent': tfm, 'TargetOS': 'windows', 'TargetFrameworkIdentifier': '.NETCoreApp',
             'LibrariesProjectRoot': L + '/', 'CommonPath': L + '/Common/src/', 'MSBuildProjectName': n}
    ev = ci.Evaluator(props, props.keys())
    ev.load(p, {'Compile'}, False)
    prefs = [r for r, _ in ev.items.get('ProjectReference', [])]
    refs = [os.path.splitext(os.path.basename(r))[0] for r in prefs]
    deps[n] = [r for r in refs if r in proj and r != n]
    # shims forward some types into OOB packages; they compile against generated stubs
    for r in prefs:
        if '/shims/stubs/' in r.replace('\\', '/'):
            sn = 'stub:' + os.path.splitext(os.path.basename(r))[0]
            stubs[sn] = r; deps[n].append(sn)
for sn, r in stubs.items():
    proj[sn] = ('stub', r); deps[sn] = []
order, seen = [], set()
def visit(n, stack=()):
    if n in seen: return
    if n in stack: raise SystemExit('cycle ' + ' -> '.join(stack + (n,)))
    for d in deps[n]: visit(d, stack + (n,))
    seen.add(n); order.append(n)
for n in sorted(proj, key=lambda n: (proj[n][0] == 'shim', n)): visit(n)
def closure(n, acc=None):
    acc = set() if acc is None else acc
    for d in deps[n]:
        if d not in acc: acc.add(d); closure(d, acc)
    return acc
with open(out, 'w', newline='\n') as f:
    for n in order:
        kind, p = proj[n]
        f.write('\t'.join([n, kind, p, ' '.join(sorted(closure(n)))]) + '\n')
print(len(order), 'assemblies planned')
EOPY
}

ver_of() { # AssemblyVersion of a project (default MajorVersion.0.0.0); the library's own
  # Directory.Build.props may change it (Microsoft.VisualBasic.Core: MajorVersion + 5 = 13.0.0.0)
  local pre=() dbp; dbp=$(dirname "$(dirname "$1")")/Directory.Build.props; [ -f "$dbp" ] && pre=(--pre-local "$dbp")
  local d; d=$(cd "$(dirname "$1")" && csproj_items -p TargetFramework=$TFM -p NetCoreAppCurrent=$TFM --init MajorVersion=${RT_VER%%.*} --init MinorVersion=0 \
    "${pre[@]}" --prop AssemblyVersion "$(basename "$1")" 2>/dev/null | cut -d= -f2)
  echo "${d:-${RT_VER%%.*}.0.0.0}"
}

build_stub() { # build_stub <stub:Name> <csproj>: empty public types for the shims' type forwards
  local n=${1#stub:} p=$2 o="$B/obj/stub-${1#stub:}"; rm -rf "${o:?}"; mkdir -p "$o" "$B/stubs"
  (cd "$(dirname "$p")" && CSPROJ_NO_FOLLOW=1 csproj_items -p TargetFramework=$TFM --kind ForwardedType --meta InnerClass "$(basename "$p")" 2>/dev/null) | python3 -S -c '
import sys
for line in sys.stdin:
    t, _, inner = line.rstrip("\n").partition("\t")
    ns, _, name = t.rpartition(".")
    body = " ".join("public static class %s { }" % i for i in inner.split(";") if i)
    print("namespace %s { public static class %s { %s } }" % (ns, name, body))' > "$o/ForwardedTypes.cs"
  local key; key=$(grep -ho '<StrongNameKeyId>[^<]*' "$L/$n/Directory.Build.props" "$L/$n/src/$n.csproj" 2>/dev/null | head -1 | cut -d'>' -f2 || true)
  key=${key:-Open}; [ "$key" = MicrosoftShared ] && key=MSShared
  printf '[assembly: System.Reflection.AssemblyVersion("%s")]\n[assembly: System.Runtime.CompilerServices.ReferenceAssembly]\n' "${RT_VER%%.*}.0.0.0" > "$o/AssemblyInfo.g.cs"
  csc "stub-$n" -target:library -nostdlib -noconfig -deterministic -debug- -out:"$(winpath "$B/stubs/$n.dll")" \
    -publicsign -keyfile:"$(keyfile "$key")" -r:"$(winpath "$O/System.Runtime.dll")" "$o/AssemblyInfo.g.cs" "$o/ForwardedTypes.cs" \
    || die "stub $n failed"
}

build_one() { # build_one <name> <kind> <csproj> <deps...>
  local n=$1 kind=$2 p=$3; shift 3
  local pd; pd=$(dirname "$p"); local o="$B/obj/$n"; rm -rf "${o:?}"; mkdir -p "$o"
  local props=(-p TargetFramework=$TFM -p NetCoreAppCurrent=$TFM -p TargetOS=windows -p TargetFrameworkIdentifier=.NETCoreApp
    -p LibrariesProjectRoot=$L/ -p CommonPath=$L/Common/src/ -p CoreLibSharedDir=$L/System.Private.CoreLib/src/ -p MSBuildProjectName=$n)
  local dflag=(); [ "$kind" = shim ] && dflag=(--default-items)
  local items=() dl=() dargs=() d refs=()
  mapfile -t items < <(cd "$pd" && csproj_items "${dflag[@]}" "${props[@]}" "$(basename "$p")" 2>/dev/null | sed "s|^|$pd/|")
  # shims (src): the checked-in forwards; "ref" source of a shim with a ref project is used instead
  IFS=';' read -ra dl <<< "$(cd "$pd" && csproj_items "${props[@]}" --pre "$L/Directory.Build.props" --define "$(basename "$p")" 2>/dev/null || true)"
  for d in "${dl[@]}"; do [ -n "$d" ] && dargs+=("-d:$d"); done
  if [ "$kind" = shim ]; then
    for d in "$O"/*.dll; do refs+=("-r:$(winpath "$d")"); done      # facades forward into the whole pack
    for d in "$@"; do case $d in stub:*) refs+=("-r:$(winpath "$B/stubs/${d#stub:}.dll")") ;; esac; done
  else
    for d in "$@"; do case $d in stub:*) refs+=("-r:$(winpath "$B/stubs/${d#stub:}.dll")") ;; *) refs+=("-r:$(winpath "$O/$d.dll")") ;; esac; done
  fi
  local key; key=$(grep -ho '<StrongNameKeyId>[^<]*' "$L/$n/Directory.Build.props" "$L/shims/$n/Directory.Build.props" "$p" 2>/dev/null | head -1 | cut -d'>' -f2 || true)
  case $n in Microsoft.Extensions.*) key=${key:-MicrosoftAspNetCore} ;; esac
  key=${key:-Open}; [ "$key" = MicrosoftShared ] && key=MSShared
  local av; av=$(ver_of "$p")
  printf '[assembly: System.Reflection.AssemblyVersion("%s")]\n[assembly: System.Reflection.AssemblyFileVersion("%s")]\n[assembly: System.Reflection.AssemblyInformationalVersion("%s+m612.bootstrap.from.source")]\n' \
    "$av" "$PACK_VER.0" "$PACK_VER" > "$o/AssemblyInfo.g.cs"
  # reference assemblies carry [ReferenceAssembly] (Arcade adds it for ref projects)
  [ "$kind" != shim ] && [ "$n" != System.Runtime ] && echo '[assembly: System.Runtime.CompilerServices.ReferenceAssembly]' >> "$o/AssemblyInfo.g.cs"
  # [CLSCompliant(true)] as eng/versioning.targets adds it (src/libraries/Directory.Build.props: CLSCompliant
  # defaults to true; System.Reflection.Metadata opts out).  Without it every CLS-checked compile against the
  # pack fails (CS3009 "base type 'object' is not CLS-compliant"), found by S12's MSBuild build.
  local cls; cls=$(cd "$pd" && csproj_items "${props[@]}" --prop CLSCompliant "$(basename "$p")" 2>/dev/null | cut -d= -f2 || true)
  [ "${cls,,}" != false ] && echo '[assembly: System.CLSCompliant(true)]' >> "$o/AssemblyInfo.g.cs"
  local sign=(-publicsign -keyfile:"$(keyfile "$key")")
  local tok; tok=$(basename "$(ls "$KEYS/$key"-*.pubkey.hex)" .pubkey.hex); tok=${tok##*-}
  printf '%s %s %s\n' "$n" "$av" "$tok" > "$O/$n.meta"
  # -runtimemetadataversion: the metadata version string of the corlib (System.Runtime in this set) is
  # what Roslyn writes into everything compiled against the pack; without it, it is empty and Cecil
  # (illink) writes broken portable PDBs for such assemblies
  csc "$n" -target:library -nostdlib -noconfig -optimize+ -deterministic -unsafe -nullable:enable -debug- \
    -runtimemetadataversion:v4.0.30319 -out:"$(winpath "$O/$n.dll")" "${sign[@]}" -d:NETCOREAPP -d:NET -d:NET8_0 -d:NET8_0_OR_GREATER -d:NET7_0_OR_GREATER \
    -d:NET6_0_OR_GREATER -d:NET5_0_OR_GREATER -d:NETCOREAPP3_0_OR_GREATER -d:NETCOREAPP2_0_OR_GREATER -d:TARGETS_WINDOWS \
    -nowarn:CS0618,CS0809,CS3021,CS0067,CS0169,CS0649,CS8597,CS8625,CS8617,CS0436,CS1591,CS0108,CS0114,CS3016,CS8618,CS0672,CS0612,CS8603,CS8600,CS8601,CS8602,CS8604,CS0419,SYSLIB0003,SYSLIB0004 \
    "${dargs[@]}" "${refs[@]}" "$o/AssemblyInfo.g.cs" "${items[@]}" || die "$n failed ($B/obj/$n.log)"
}

step_refs() {
  local n kind p deps
  while IFS=$'\t' read -r n kind p deps; do
    if [ "$kind" = stub ]; then
      [ -f "$B/stubs/${n#stub:}.dll" ] && [ -z "${REBUILD:-}" ] && continue
      log "stub ${n#stub:}"; build_stub "$n" "$p"; continue
    fi
    [ -f "$O/$n.dll" ] && [ -z "${REBUILD:-}" ] && continue
    log "ref $n ($kind)"; build_one "$n" "$kind" "$p" $deps
  done < "$B/plan.tsv"
}

# targeting pack layout: packs/Microsoft.NETCore.App.Ref/<ver>/{ref/net8.0,data/FrameworkList.xml}
step_pack() {
  local n av tok f
  rm -rf "${PACK:?}"; mkdir -p "$PACK/ref/$TFM" "$PACK/data" "$PACK/analyzers/dotnet/cs"
  cp "$O"/*.dll "$PACK/ref/$TFM/"
  # the source generators the pack ships for C# (the SDK passes them to csc as analyzers):
  # [LibraryImport], [GeneratedComInterface], [JSImport], [GeneratedRegex], [JsonSerializable];
  # built from source by netfx-pkgs.sh against Roslyn 4.8 (the compiler of SDK 8.0.1xx)
  local g gens="Microsoft.Interop.SourceGeneration Microsoft.Interop.LibraryImportGenerator Microsoft.Interop.ComInterfaceGenerator
    Microsoft.Interop.JavaScript.JSImportGenerator System.Text.RegularExpressions.Generator System.Text.Json.SourceGeneration"
  for g in $gens; do
    [ -f "$M612/pkgs/analyzers/$g.dll" ] && cp "$M612/pkgs/analyzers/$g.dll" "$PACK/analyzers/dotnet/cs/" || echo "  note: generator $g not built (netfx-pkgs.sh)"
  done
  { echo "<FileList TargetFrameworkIdentifier=\".NETCoreApp\" TargetFrameworkVersion=\"${TFM#net}\" FrameworkName=\"Microsoft.NETCore.App\" Name=\".NET Runtime\">"
    for f in "$O"/*.meta; do read -r n av tok < "$f"
      echo "  <File Type=\"Managed\" Path=\"ref/$TFM/$n.dll\" AssemblyName=\"$n\" PublicKeyToken=\"$tok\" AssemblyVersion=\"$av\" FileVersion=\"$PACK_VER.0\" />"
    done
    for f in "$PACK"/analyzers/dotnet/cs/*.dll; do [ -e "$f" ] || continue; n=$(basename "$f" .dll)
      echo "  <File Type=\"Analyzer\" Language=\"cs\" Path=\"analyzers/dotnet/cs/$n.dll\" AssemblyName=\"$n\" PublicKeyToken=\"cc7b13ffcd2ddd51\" AssemblyVersion=\"${RT_VER%%.*}.0.0.0\" FileVersion=\"$PACK_VER.0\" />"
    done
    echo "</FileList>"; } > "$PACK/data/FrameworkList.xml"
  # data/PlatformManifest.txt: what the shared framework carries, for the SDK's package conflict resolution
  # (without it a net8.0 publish copies lower package versions of framework assemblies next to the app)
  for f in "$O"/*.meta; do read -r n av tok < "$f"; echo "$n.dll|Microsoft.NETCore.App.Ref|$av|$PACK_VER.0"; done | sort > "$PACK/data/PlatformManifest.txt"
  # tell the SDK about the pack (KnownFrameworkReference) and where packs live
  local bv="$SDKROOT/sdk/8.0.131/Microsoft.NETCoreSdk.BundledVersions.props"
  [ -f "$bv" ] || return 0
  python3 -S - "$bv" "$PACK_VER" "$TFM" "$RT_VER" <<'EOPY'
import sys
p, ver, tfm, rtver = sys.argv[1:5]
s = open(p).read()
items = {
 'KnownFrameworkReference': f"""    <KnownFrameworkReference Include="Microsoft.NETCore.App" TargetFramework="{tfm}" RuntimeFrameworkName="Microsoft.NETCore.App"
      DefaultRuntimeFrameworkVersion="{rtver}" LatestRuntimeFrameworkVersion="{rtver}" TargetingPackName="Microsoft.NETCore.App.Ref"
      TargetingPackVersion="{ver}" RuntimePackNamePatterns="Microsoft.NETCore.App.Runtime.**RID**" RuntimePackRuntimeIdentifiers="win-x64" IsTrimmable="true" />""",
 # the apphost pack is native (MinGW host build, not done yet); listed so the SDK's
 # ResolveAppHosts task gets its input; projects must set UseAppHost=false for now
 'KnownAppHostPack': f"""    <KnownAppHostPack Include="Microsoft.NETCore.App" TargetFramework="{tfm}" AppHostPackNamePattern="Microsoft.NETCore.App.Host.**RID**"
      AppHostPackVersion="{ver}" AppHostRuntimeIdentifiers="win-x64" />""",
}
for k, v in items.items():
    if '<' + k + ' ' not in s:
        s = s.replace('</Project>', '  <ItemGroup>\n' + v + '\n  </ItemGroup>\n</Project>')
open(p, 'w', newline='\n').write(s)
EOPY
}

step_test() {
  local T=$M612/test/refpack-$RT_VER-net8lib MSBOUT=$M612/msbuild-17.8.49/tools
  rm -rf "${T:?}"; mkdir -p "$T/emptyfeed"; cp "$here"/tests/net8lib/* "$T/"
  cd "$T"
  MSBuildSDKsPath=$(cygpath -w "$SDKROOT/sdk/8.0.131/Sdks") mono "$MSBOUT/MSBuild.exe" -nologo -m:1 -nr:false -v:${TEST_VERBOSITY:-m} \
    -restore -p:NetCoreRoot="$(cygpath -w "$SDKROOT")\\" -p:NetCoreTargetingPackRoot="$(cygpath -w "$SDKROOT/packs")" net8lib.csproj
  monodis --assemblyref bin/Debug/$TFM/net8lib.dll | grep -A2 'Name=System.Runtime$' || true
}

for s in $steps; do log "refpack $RT_VER: $s"; "step_$s"; done
