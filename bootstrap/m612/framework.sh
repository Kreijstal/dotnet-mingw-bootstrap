#!/bin/bash
# Assemble the Microsoft.NETCore.App 8.0.0 shared framework from the
# from-source parts and install it into the SDK root, next to the host:
#   shared/Microsoft.NETCore.App/8.0.0/
#     System.Private.CoreLib.dll + the libraries   (corelib.sh, libraries.sh)
#     coreclr.dll (the Mono VM), System.IO.Compression.Native.dll  (mono-runtime.sh)
#     hostpolicy.dll                               (corehost.sh)
#     Microsoft.NETCore.App.deps.json              (generated here, upstream layout)
# and make the SDK target it: KnownFrameworkReference DefaultRuntimeFrameworkVersion /
# LatestRuntimeFrameworkVersion = 8.0.0, so a framework-dependent net8.0 app's
# runtimeconfig.json asks for 8.0.0 (the reference pack keeps its 8.0.31 version).
#
# FLAVOR=coreclr (framework-coreclr.sh): the same libraries with CoreCLR instead
# (coreclr.sh: coreclr.dll, clrjit.dll, mscorrc.dll; corelib-coreclr.sh: its CoreLib),
# installed into a separate SDK root, dotnet-sdk-8.0.131-net8-coreclr (step newroot
# copies dotnet-sdk-8.0.131-net8 without its shared framework), so the Mono one stays.
#
# usage: framework.sh [step...]     steps: [newroot] layout install runtimepack [check] test
#   check: tools/rvaalign.cs over the framework (CreateSpan data CoreCLR would reject)
#   runtimepack: packs/Microsoft.NETCore.App.Runtime.win-x64/<ver> (the same files as the shared
#   framework + hostfxr, data/RuntimeList.xml) in the SDK root, for self-contained publish
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
. "$here/../env-m612.sh"
. "$here/common.sh"    # (net8tool)
steps=${*:-layout install runtimepack test}
RT_VER=${RT_VER:-8.0.0}; TFM=net${RT_VER%%.*}.0
FLAVOR=${FLAVOR:-mono}
LIBS=$M612/netcoreapp-$RT_VER/lib            # the libraries (flavor-independent) + the Mono CoreLib
MONO_OUT=$M612/mono-$RT_VER
CLR_OUT=$M612/coreclr-$RT_VER
HOSTPOLICY=$M612/corehost-$RT_VER/corehost/hostpolicy.dll
RID=win-x64
case $FLAVOR in
  mono)    FX=${FXOUT:-$M612/netcoreapp-$RT_VER}; SDKROOT=${SDKROOT:-$M612/dotnet-sdk-8.0.131}
           RUNTIME_FILES=("$MONO_OUT/lib/coreclr.dll") ;;
  coreclr) FX=${FXOUT:-$M612/netcoreapp-$RT_VER-coreclr}; SDKROOT=${SDKROOT:-$M612/dotnet-sdk-8.0.131-net8-coreclr}
           RUNTIME_FILES=("$CLR_OUT/coreclr.dll" "$CLR_OUT/clrjit.dll" "$CLR_OUT/mscorrc.dll") ;;
  *) die "FLAVOR must be mono or coreclr" ;;
esac
FW=$FX/shared/Microsoft.NETCore.App/$RT_VER
NATIVE_NAMES="coreclr.dll clrjit.dll mscorrc.dll hostpolicy.dll System.IO.Compression.Native.dll"

# a new SDK root for the CoreCLR flavor: everything of the net8.0 SDK root but its shared framework
step_newroot() {
  local src=$M612/dotnet-sdk-8.0.131-net8
  [ "$FLAVOR" = coreclr ] || die "newroot is for FLAVOR=coreclr"
  rm -rf "${SDKROOT:?}"; mkdir -p "$SDKROOT"
  local e
  for e in "$src"/*; do
    [ "$(basename "$e")" = shared ] && continue
    cp -r "$e" "$SDKROOT/"
  done
  rm -rf "${SDKROOT:?}/packs/Microsoft.NETCore.App.Runtime.$RID"
  du -sh "$SDKROOT"
}

step_layout() {
  rm -rf "${FW:?}"; mkdir -p "$FW"
  cp "$LIBS"/*.dll "$FW/"
  cp "$FX"/lib/System.Private.CoreLib.dll "$FW/"     # the CoreLib of this flavor
  cp "${RUNTIME_FILES[@]}" "$MONO_OUT/libs/System.IO.Compression.Native.dll" "$HOSTPOLICY" "$FW/"
  python3 -S - "$FW" "$RT_VER" "$TFM" "$RID" "$NATIVE_NAMES" <<'EOPY'
import os, sys, json, subprocess
fw, ver, tfm, rid, natives = sys.argv[1:6]
natives = natives.split()
tv = '.NETCoreApp,Version=v' + tfm[3:]
pkg = 'Microsoft.NETCore.App.Runtime.%s/%s' % (rid, ver)
runtime, native = {}, {}
for f in sorted(os.listdir(fw)):
    if not f.endswith('.dll'): continue
    if f in natives:
        native[f] = {'fileVersion': ver + '.0'}
    else:
        # assembly version from the metadata (monodis --assembly)
        out = subprocess.run(['monodis', '--assembly', os.path.join(fw, f)], capture_output=True, text=True).stdout
        av = next((l.split(':', 1)[1].strip() for l in out.splitlines() if l.startswith('Version:')), ver[:1] + '.0.0.0')
        runtime[f] = {'assemblyVersion': av, 'fileVersion': ver + '.0'}
deps = {
  'runtimeTarget': {'name': tv + '/' + rid, 'signature': ''},
  'compilationOptions': {},
  'targets': {tv: {}, tv + '/' + rid: {pkg: {'runtime': runtime, 'native': native}}},
  'libraries': {pkg: {'type': 'package', 'serviceable': True, 'sha512': '',
                      'path': pkg.lower(), 'hashPath': pkg.lower().replace('/', '.') + '.nupkg.sha512'}},
}
json.dump(deps, open(os.path.join(fw, 'Microsoft.NETCore.App.deps.json'), 'w', newline='\n'), indent=2)
print('deps.json: %d managed, %d native' % (len(runtime), len(native)))
EOPY
  ls "$FW" | wc -l
}

step_install() {
  local dst=$SDKROOT/shared/Microsoft.NETCore.App/$RT_VER
  rm -rf "${dst:?}"; mkdir -p "$(dirname "$dst")"; cp -r "$FW" "$dst"
  local bv="$SDKROOT/sdk/8.0.131/Microsoft.NETCoreSdk.BundledVersions.props"
  sed -i -E "/KnownFrameworkReference Include=\"Microsoft.NETCore.App\"/,/\/>/{s/(DefaultRuntimeFrameworkVersion|LatestRuntimeFrameworkVersion)=\"[^\"]*\"/\1=\"$RT_VER\"/g}" "$bv"
  grep -A3 'KnownFrameworkReference Include="Microsoft.NETCore.App"' "$bv" | grep -o 'De[a-zA-Z]*RuntimeFrameworkVersion="[^"]*"\|Lat[a-zA-Z]*RuntimeFrameworkVersion="[^"]*"'
  "$SDKROOT/dotnet.exe" --list-runtimes
}

# runtime pack (what NuGet would download as Microsoft.NETCore.App.Runtime.win-x64): the SDK takes it
# from <root>/packs for RuntimeIdentifier builds / self-contained publish (KnownFrameworkReference
# RuntimePackNamePatterns, version = LatestRuntimeFrameworkVersion)
step_runtimepack() {
  local RP=$SDKROOT/packs/Microsoft.NETCore.App.Runtime.$RID/$RT_VER HOSTFXR=$M612/corehost-$RT_VER/corehost/hostfxr.dll
  rm -rf "${RP:?}"; mkdir -p "$RP/runtimes/$RID/lib/$TFM" "$RP/runtimes/$RID/native" "$RP/data"
  local f
  for f in "$FW"/*.dll; do
    case $(basename "$f") in
      coreclr.dll|clrjit.dll|mscorrc.dll|hostpolicy.dll|System.IO.Compression.Native.dll|System.Private.CoreLib.dll) cp "$f" "$RP/runtimes/$RID/native/" ;;
      *) cp "$f" "$RP/runtimes/$RID/lib/$TFM/" ;;
    esac
  done
  cp "$HOSTFXR" "$RP/runtimes/$RID/native/"
  python3 -S - "$RP" "$RT_VER" "$TFM" "$RID" <<'EOPY'
import os, sys, subprocess, re
rp, ver, tfm, rid = sys.argv[1:5]
out = ['<FileList Name=".NET %s - %s" TargetFrameworkIdentifier=".NETCoreApp" TargetFrameworkVersion="%s" FrameworkName="Microsoft.NETCore.App">' % (tfm[3:], rid, tfm[3:])]
def meta(path):
    o = subprocess.run(['monodis', '--assembly', path], capture_output=True, text=True).stdout
    v = re.search(r'^Version:\s*(\S+)', o, re.M)
    return v.group(1) if v else None
for sub, kind in (('lib/' + tfm, 'Managed'), ('native', 'Native')):
    d = os.path.join(rp, 'runtimes', rid, sub)
    for f in sorted(os.listdir(d)):
        rel = 'runtimes/%s/%s/%s' % (rid, sub, f)
        if kind == 'Managed' or f == 'System.Private.CoreLib.dll':
            av = meta(os.path.join(d, f))
            out.append('  <File Type="Managed" Path="%s" AssemblyName="%s" AssemblyVersion="%s" FileVersion="%s.0" />' % (rel, f[:-4], av, ver))
        else:
            out.append('  <File Type="Native" Path="%s" FileVersion="%s.0" />' % (rel, ver))
out.append('</FileList>')
open(os.path.join(rp, 'data', 'RuntimeList.xml'), 'w', newline='\n').write('\n'.join(out) + '\n')
print('RuntimeList.xml: %d files' % (len(out) - 2))
EOPY
}

step_check() {
  log "RVA data alignment (RuntimeHelpers.CreateSpan)"
  net8tool rvaalign "$(cygpath -m "$FW")"
}

# build tests/<name>/*.csproj with the SDK (MSBuild on the seed Mono) -> its bin/Release/net8.0/
build_test_app() {
  local name=$1 T=$M612/test/fx-$RT_VER/$1 MSBOUT=$M612/msbuild-17.8.49/tools
  rm -rf "${T:?}"; mkdir -p "$T/emptyfeed"; cp "$here/tests/$name"/* "$T/"
  ( cd "$T"; PATH=/ucrt64/bin:$PATH
    MSBuildSDKsPath=$(cygpath -w "$SDKROOT/sdk/8.0.131/Sdks") mono "$MSBOUT/MSBuild.exe" -nologo -m:1 -nr:false -v:${TEST_VERBOSITY:-m} \
      -restore -p:Configuration=Release -p:NetCoreRoot="$(cygpath -w "$SDKROOT")\\" -p:NetCoreTargetingPackRoot="$(cygpath -w "$SDKROOT/packs")" ./*.csproj ) >&2
  echo "$T/bin/Release/$TFM"
}

# the CoreCLR root is a net8.0 SDK: it builds its test apps itself (dotnet build on CoreCLR)
build_test_app_dotnet() {
  local name=$1 T=$M612/test/fx-$RT_VER-$FLAVOR/$1
  rm -rf "${T:?}"; mkdir -p "$T"; cp "$here/tests/$name"/* "$T/"
  ( cd "$T"; DOTNET_CLI_TELEMETRY_OPTOUT=1 DOTNET_CLI_HOME=$(cygpath -w "$M612/test/fx-$RT_VER-$FLAVOR/home") \
      NUGET_PACKAGES=$(cygpath -w "$M612/test/fx-$RT_VER-$FLAVOR/nuget-packages") \
      "$SDKROOT/dotnet.exe" build -nologo -v:${TEST_VERBOSITY:-q} -c Release ./*.csproj ) >&2
  echo "$T/bin/Release/$TFM"
}

step_test() {
  local d rc
  for t in ${TESTS:-hello}; do
    log "test $t"
    if [ "$FLAVOR" = coreclr ]; then
      d=$(build_test_app_dotnet "$t")
    else
      d=$(build_test_app "$t")
    fi
    cat "$d"/*.runtimeconfig.json
    rc=0; "$SDKROOT/dotnet.exe" "$d/$t.dll" ${TEST_ARGS:-} || rc=$?; echo "rc=$rc"
  done
}

for s in $steps; do log "framework $RT_VER ($FLAVOR): $s"; "step_$s"; done
