#!/bin/bash
# crossgen2 (the ReadyToRun compiler, runtime 8.0.0 src/coreclr/tools/aot) from source, as a
# framework-dependent net8.0 app that runs on the CoreCLR shared framework:
#   native   clrjit_win_x64_x64.dll (RyuJIT built as crossgen2's JIT) + jitinterface_x64.dll,
#            from the coreclr.sh CMake tree (clang/MinGW)
#   managed  ILCompiler.DependencyAnalysisFramework, ILCompiler.TypeSystem, ILCompiler.Diagnostics,
#            ILCompiler.ReadyToRun, crossgen2.dll (sdkproj.sh); System.CommandLine from net8-pkgs
#   layout   $M612/crossgen2-8.0.0
#   test     R2R-compile a hello app; run it on the CoreCLR root with the R2R code in use
#   r2rroot  copy the CoreCLR root to $R2RROOT (dotnet-sdk-8.0.131-net8-coreclr-r2r), the IL roots stay
#   r2rfx    R2R-compile the shared framework of $R2RROOT (from the IL of $ROOT), one image per assembly
#   r2rsdk   R2R-compile the SDK's managed tools the same way: sdk/<ver>/*.dll, Roslyn/bincore, the
#            net8.0 SDK tasks (Microsoft.NET.Build.Tasks), NuGet pack tasks, the MSBuild SDK resolver
#
# usage: crossgen2.sh [step...]     steps: native managed layout test r2rroot r2rfx r2rsdk
#                                   (default: native managed layout test)
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
. "$here/../env-m612.sh"
steps=${*:-native managed layout test}
RT=$M612/src/runtime-8.0.0-native
AOT=$RT/src/coreclr/tools/aot
# The native step runs in the toolchain of coreclr.sh's tree: NATIVE_MSYSTEM=CLANG64 (default) or UCRT64
NATIVE_MSYSTEM=${NATIVE_MSYSTEM:-CLANG64}
case "$NATIVE_MSYSTEM" in CLANG64) _ns= ;; UCRT64) _ns=-ucrt64 ;; *) die "NATIVE_MSYSTEM=$NATIVE_MSYSTEM" ;; esac
CB=$M612/build/coreclr-8.0.0$_ns/cmake          # coreclr.sh's CMake tree
B=$M612/build/crossgen2-8.0.0; mkdir -p "$B/obj"
OUT=$M612/crossgen2-8.0.0
ROOT=${ROOT:-$M612/dotnet-sdk-8.0.131-net8-coreclr}   # framework-coreclr.sh's root
FX=$ROOT/shared/Microsoft.NETCore.App/8.0.0
R2RROOT=${R2RROOT:-$M612/dotnet-sdk-8.0.131-net8-coreclr-r2r}
SDKVER=8.0.131
BCSC=${BCSC:-$M612/roslyn-4.8.0/csc.exe}
step_native() {
  # (the managed steps use the UCRT64 tools of the chain; the native build is coreclr.sh's tree)
  [ -n "${CROSSGEN2_NATIVE:-}" ] || { env MSYSTEM="$NATIVE_MSYSTEM" CROSSGEN2_NATIVE=1 bash "$here/crossgen2.sh" native; return; }
  ninja -C "$CB" -j"${JOBS:-4}" clrjit_win_x64_x64 jitinterface_x64
}
if [ -n "${CROSSGEN2_NATIVE:-}" ]; then   # (common.sh is for the UCRT64 seed environment)
  [ "$steps" = native ] || die "with CROSSGEN2_NATIVE only the native step runs"
  log "crossgen2: native"; step_native; exit
fi
. "$here/common.sh"
. "$here/sdkproj.sh"
SP_OUT=$B/out; SP_GROUP=crossgen2; SP_DEFS=(); SP_REFS=("$M612/net8-pkgs/System.CommandLine.dll")
RTPROPS=(-p NetCoreAppToolCurrent=net8.0 -p NetCoreAppCurrent=net8.0 -p DotNetBuildFromSource=true -p Configuration=Release
  -p RepoRoot=$RT/ -p LibrariesProjectRoot=$RT/src/libraries/ -p CommonPath=$RT/src/libraries/Common/src/
  -p ToolsProjectRoot=$RT/src/tools/ -p TargetOS=windows -p TargetArchitecture=x64 -p TargetsWindows=true)

step_managed() {
  SP_TF=net8.0; SP_KEY=Open; SP_VER=8.0.0.0; SP_FILEVER=8.0.0.0; SP_INFOVER=8.0.0
  SP_EVAL=("${RTPROPS[@]}")
  local p
  for p in ILCompiler.DependencyAnalysisFramework ILCompiler.TypeSystem ILCompiler.Diagnostics ILCompiler.ReadyToRun; do
    sdkproj "$AOT/$p/$p.csproj"
  done
  sdkproj "$AOT/crossgen2/crossgen2.csproj"
}

step_layout() {
  rm -rf "${OUT:?}"; mkdir -p "$OUT"
  local f
  for f in ILCompiler.DependencyAnalysisFramework ILCompiler.TypeSystem ILCompiler.Diagnostics ILCompiler.ReadyToRun crossgen2; do
    cp "$SP_OUT/$f.dll" "$OUT/"
  done
  cp "$M612/net8-pkgs/System.CommandLine.dll" "$OUT/"
  cp "$CB/jit/clrjit_win_x64_x64.dll" "$CB/tools/aot/jitinterface/jitinterface_x64.dll" "$OUT/"
  llvm-strip --strip-debug "$OUT"/*_x64.dll
  # crossgen2.csproj/AotCompilerCommon.props: server GC, no tiering
  cat > "$OUT/crossgen2.runtimeconfig.json" <<'EOJ'
{
  "runtimeOptions": {
    "tfm": "net8.0",
    "framework": { "name": "Microsoft.NETCore.App", "version": "8.0.0" },
    "configProperties": {
      "System.GC.Server": true,
      "System.Runtime.TieredCompilation": false,
      "System.Xml.XmlResolver.IsNetworkingEnabledByDefault": false
    }
  }
}
EOJ
  ls -l "$OUT"
}

# crossgen2 <args...>: run crossgen2 on the CoreCLR root
crossgen2() { "$ROOT/dotnet.exe" "$(cygpath -m "$OUT/crossgen2.dll")" "$@"; }

# r2rcheck <dll>: the COR header's flags and ManagedNativeHeader (the READYTORUN_HEADER of an R2R image)
r2rcheck() {
  python3 -S - "$1" <<'EOPY'
import struct, sys
d = open(sys.argv[1], 'rb').read()
pe = struct.unpack_from('<I', d, 0x3c)[0]
nsec = struct.unpack_from('<H', d, pe + 6)[0]; opt = pe + 24
dd = opt + (112 if struct.unpack_from('<H', d, opt)[0] == 0x20b else 96)
cor_rva = struct.unpack_from('<I', d, dd + 14 * 8)[0]
secs = [struct.unpack_from('<IIII', d, opt + struct.unpack_from('<H', d, pe + 20)[0] + 40 * k + 8) for k in range(nsec)]
def off(rva):
    for vs, va, rs, ro in secs:
        if va <= rva < va + max(vs, rs): return rva - va + ro
if not cor_rva: print('native'); sys.exit()
c = off(cor_rva)
flags = struct.unpack_from('<I', d, c + 16)[0]
mnh_rva, mnh_size = struct.unpack_from('<II', d, c + 64)
sig = struct.unpack_from('<I', d, off(mnh_rva))[0] if mnh_rva else 0
print('flags=0x%x ManagedNativeHeader=0x%x/%d %s' % (flags, mnh_rva, mnh_size,
      'READYTORUN (RTR) v%d.%d' % struct.unpack_from('<HH', d, off(mnh_rva) + 4) if sig == 0x00525452 else 'IL only'))
EOPY
}

step_test() {
  local T=$M612/test/crossgen2; rm -rf "${T:?}"; mkdir -p "$T/r2r"
  local H=$M612/test/fx-8.0.0-coreclr/hello/bin/Release/net8.0
  [ -f "$H/hello.dll" ] || die "no hello app; run framework-coreclr.sh test first"
  cp -r "$H"/. "$T/r2r/"
  log "crossgen2 hello.dll"
  time crossgen2 -r "$(cygpath -m "$FX")/" --targetos:windows --targetarch:x64 -O \
    "$(cygpath -m "$H/hello.dll")" -o "$(cygpath -m "$T/r2r/hello.dll")"
  log "IL:  $(r2rcheck "$H/hello.dll")"
  log "R2R: $(r2rcheck "$T/r2r/hello.dll")"
  log "run R2R hello"
  "$ROOT/dotnet.exe" "$(cygpath -m "$T/r2r/hello.dll")"
}

step_r2rroot() {
  rm -rf "${R2RROOT:?}"; cp -r "$ROOT" "$R2RROOT"; du -sh "$R2RROOT"
}

# r2r_dir <IL dir> <out dir> <reference globs...>: compile every IL-only assembly of <IL dir>
# (native DLLs, R2R images and reference assemblies are left as they are), JOBS at a time
r2r_dir() {
  local in=$1 out=$2; shift 2
  local refs=() g; for g in "$@"; do refs+=(-r "$(cygpath -m "$(dirname "$g")")/$(basename "$g")"); done   # (cygpath drops a *)
  local L=$M612/build/crossgen2-8.0.0/r2r-$(basename "$(dirname "$out")")-$(basename "$out").log; : > "$L"
  local f n=0 ok=0 bad=0 skip=0
  for f in "$in"/*.dll; do
    case "$(r2rcheck "$f")" in *"IL only"*) ;; *) skip=$((skip+1)); continue ;; esac
    n=$((n+1))
    while [ "$(jobs -rp | wc -l)" -ge "${JOBS:-4}" ]; do wait -n || true; done
    ( if crossgen2 "$(cygpath -m "$f")" -o "$(cygpath -m "$out/$(basename "$f").r2r.tmp")" "${refs[@]}" \
           --targetos:windows --targetarch:x64 -O --parallelism 2 > "$out/$(basename "$f").r2r.log" 2>&1
      then mv -f "$out/$(basename "$f").r2r.tmp" "$out/$(basename "$f")"; echo "OK   $(basename "$f")" >> "$L"
      else rm -f "$out/$(basename "$f").r2r.tmp"; echo "FAIL $(basename "$f")" >> "$L"; fi
      cat "$out/$(basename "$f").r2r.log" >> "$L"; rm -f "$out/$(basename "$f").r2r.log" ) &
  done
  wait
  ok=$(grep -c '^OK ' "$L" || true); bad=$(grep -c '^FAIL ' "$L" || true)
  log "$out: $n IL assemblies: $ok R2R, $bad failed (kept IL), $skip skipped (native/R2R/ref) -- $L"
  grep '^FAIL ' "$L" || true
}

step_r2rfx() {
  local out=$R2RROOT/shared/Microsoft.NETCore.App/8.0.0
  time r2r_dir "$FX" "$out" "$FX/*.dll"
}

R2R_SDK_DIRS=${R2R_SDK_DIRS:-. Roslyn/bincore Sdks/Microsoft.NET.Sdk/tools/net8.0 Sdks/NuGet.Build.Tasks.Pack/CoreCLR SdkResolvers/Microsoft.DotNet.MSBuildSdkResolver}
step_r2rsdk() {
  local S=$ROOT/sdk/$SDKVER d
  for d in $R2R_SDK_DIRS; do   # references: the framework, the SDK directory, the directory itself
    time r2r_dir "$S/$d" "$R2RROOT/sdk/$SDKVER/$d" "$FX/*.dll" "$S/*.dll" "$S/$d/*.dll"
  done
}

for s in $steps; do log "crossgen2: $s"; "step_$s"; done
