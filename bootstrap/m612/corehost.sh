#!/bin/bash
# Build the .NET host (src/native/corehost of dotnet/runtime: dotnet.exe
# muxer, hostfxr.dll, hostpolicy.dll, apphost.exe, nethost, comhost) with the
# MSYS2 CLANG64 toolchain (clang/lld, x86_64-w64-mingw32 target) or the UCRT64
# one (GCC/binutils/libstdc++), and install
# it into the source-built SDK of phase 6:
#   $SDKROOT/dotnet.exe, host/fxr/<ver>/hostfxr.dll,
#   packs/Microsoft.NETCore.App.Host.win-x64/<ver>/runtimes/win-x64/native/{apphost.exe,...}
# Upstream drives this CMake project from corehost.proj (Arcade), which also
# generates the version headers; here the script writes them itself.
#
# usage: corehost.sh [step...]      steps: prep configure build install test
# Run with MSYSTEM=CLANG64 or UCRT64.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
. "$here/../env-m612.sh"
steps=${*:-prep configure build install test}
RT_VER=${RT_VER:-8.0.0}
HOST_VER=${HOST_VER:-$RT_VER}          # version directory of host/fxr
PACK_VER=${PACK_VER:-8.0.31}           # apphost pack version known to the SDK (see refpack.sh)
RID=win-x64
SRC=$M612/src/runtime-$RT_VER-native   # full, patched tree (the refpack tree stays pristine)
P=$here/../../patches/m612/runtime-$RT_VER
B=$M612/build/corehost-$RT_VER$NATIVE_SUFFIX
O=$M612/corehost-$RT_VER$NATIVE_SUFFIX               # cmake install prefix
SDKROOT=${SDKROOT:-$M612/dotnet-sdk-8.0.131}
# common.sh (fetch/provenance helpers) also resolves the seed Mono paths, which
# live in UCRT64 only; no managed code is compiled here.
MONO_LIB=/ucrt64/lib/mono/4.5 . "$here/common.sh"
JOBS=${JOBS:-4}

step_prep() {
  local tb=$M612/src/runtime-$RT_VER.tar.gz
  fetch_tarball "https://github.com/dotnet/runtime/archive/refs/tags/v$RT_VER.tar.gz" "$tb"
  if [ ! -f "$SRC/.extracted" ]; then
    log "extract runtime-$RT_VER -> $SRC"
    mkdir -p "$SRC"
    tar -C "$SRC" --strip-components=1 -xzf "$tb"
    delete_binaries "$SRC"
    { zcat "$tb" 2>/dev/null || true; } | git get-tar-commit-id > "$SRC/.commit"
    touch "$SRC/.extracted"
  fi
  apply_patches "$SRC" "$P"
}

# The version headers Arcade generates (GenerateNativeVersionFile,
# GenerateRuntimeVersionFile in eng/versioning.targets and corehost.proj).
gen_versions() {
  local obj=$SRC/artifacts/obj res=$B/hostResourceFiles maj min pat f desc
  IFS=. read -r maj min pat <<< "$RT_VER"
  # RUNTIME_FILE_VERSION: the file version Arcade calculates for a serviced build
  local fv=${RUNTIME_FILE_VERSION:-$maj.$min.$pat.0} fmaj fmin fbuild frev
  IFS=. read -r fmaj fmin fbuild frev <<< "$fv"
  mkdir -p "$obj"
  cat > "$obj/runtime_version.h" <<EOV
#define RuntimeAssemblyMajorVersion $maj
#define RuntimeAssemblyMinorVersion $min
#define RuntimeFileMajorVersion $fmaj
#define RuntimeFileMinorVersion $fmin
#define RuntimeFileBuildVersion $fbuild
#define RuntimeFileRevisionVersion $frev
#define RuntimeProductMajorVersion $maj
#define RuntimeProductMinorVersion $min
#define RuntimeProductPatchVersion $pat
#define RuntimeProductVersion $RT_VER
EOV
  verh() { # verh <description>
    cat <<EOV
#include <windows.h>
#ifndef VER_COMPANYNAME_STR
#define VER_COMPANYNAME_STR         ".NET Foundation (source-built, dotnet-bootstrap)"
#endif
#ifndef VER_FILEDESCRIPTION_STR
#define VER_FILEDESCRIPTION_STR     "$1"
#endif
#ifndef VER_INTERNALNAME_STR
#define VER_INTERNALNAME_STR        VER_FILEDESCRIPTION_STR
#endif
#ifndef VER_ORIGINALFILENAME_STR
#define VER_ORIGINALFILENAME_STR    VER_FILEDESCRIPTION_STR
#endif
#ifndef VER_PRODUCTNAME_STR
#define VER_PRODUCTNAME_STR         ".NET"
#endif
#undef VER_PRODUCTVERSION
#define VER_PRODUCTVERSION          $fmaj,$fmin,$fbuild,$frev
#undef VER_PRODUCTVERSION_STR
#define VER_PRODUCTVERSION_STR      "$RT_VER"
#undef VER_FILEVERSION
#define VER_FILEVERSION             $fmaj,$fmin,$fbuild,$frev
#undef VER_FILEVERSION_STR
#define VER_FILEVERSION_STR         "$fv"
#ifndef VER_LEGALCOPYRIGHT_STR
#define VER_LEGALCOPYRIGHT_STR      "\\xa9 .NET Foundation and Contributors."
#endif
#ifndef VER_DEBUG
#define VER_DEBUG                   0
#endif
EOV
  }
  verh ".NET Host" > "$obj/_version.h"
  for f in "dotnet:.NET Host" "hostfxr:.NET Host Resolver - $RT_VER" "hostpolicy:.NET Host Policy - $RT_VER" \
           "comhost:.NET COM Host" "ijwhost:.NET IJW Host" "nethost:.NET Component Host"; do
    mkdir -p "$res/${f%%:*}"; verh "${f#*:}" > "$res/${f%%:*}/version_info.h"
  done
}

step_configure() {
  gen_versions
  log "configure corehost ($MSYSTEM)"
  cmake -G Ninja -S "$SRC/src/native/corehost" -B "$B/cmake" \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=$NCC -DCMAKE_CXX_COMPILER=$NCXX -DCMAKE_RC_COMPILER=$NRC \
    -DCMAKE_INSTALL_PREFIX="$O" \
    -DCLR_CMAKE_TARGET_ARCH=x64 -DCLR_CMAKE_HOST_ARCH=x64 \
    -DCLI_CMAKE_PKG_RID=$RID -DCLI_CMAKE_FALLBACK_OS=win \
    -DCLI_CMAKE_COMMIT_HASH="$(cat "$SRC/.commit")" \
    -DCLI_CMAKE_RESOURCE_DIR="$B/hostResourceFiles" \
    -DCLR_CMAKE_KEEP_NATIVE_SYMBOLS=ON
}

step_build() { log "build corehost"; ninja -C "$B/cmake" -j"$JOBS"; }

# install into $O, then lay the pieces out in the SDK like the official zip:
#   dotnet.exe, host/fxr/<ver>/hostfxr.dll, packs/Microsoft.NETCore.App.Host.win-x64/<ver>/runtimes/win-x64/native/
# hostpolicy.dll belongs to the runtime (shared/Microsoft.NETCore.App/<ver>/),
# which does not exist yet; it stays in $O/corehost.
step_install() {
  log "install corehost -> $O"
  rm -rf "${O:?}"
  cmake --install "$B/cmake" --prefix "$O"
  $NSTRIP --strip-all "$O"/corehost/*.exe "$O"/corehost/*.dll
  local apk=$SDKROOT/packs/Microsoft.NETCore.App.Host.$RID/$PACK_VER/runtimes/$RID/native f
  rm -rf "${SDKROOT:?}/host/fxr" "${apk:?}"
  mkdir -p "$SDKROOT/host/fxr/$HOST_VER" "$apk"
  cp "$O/corehost/dotnet.exe" "$SDKROOT/"
  cp "$O/corehost/hostfxr.dll" "$SDKROOT/host/fxr/$HOST_VER/"
  for f in apphost.exe comhost.dll nethost.dll nethost.dll.a libnethost.a nethost.h hostfxr.h coreclr_delegates.h; do
    cp "$O/corehost/$f" "$apk/"
  done
  ls -l "$SDKROOT/dotnet.exe" "$SDKROOT/host/fxr/$HOST_VER" "$apk"
}

step_test() {
  local T=$M612/test/corehost-$RT_VER MSBOUT=$M612/msbuild-17.8.49/tools rc
  rm -rf "${T:?}"; mkdir -p "$T/app/emptyfeed"; cp "$here"/tests/apphost/* "$T/app/"
  log "test 1: dotnet --info (muxer + hostfxr; no runtimes, no dotnet.dll SDK expected)"
  "$SDKROOT/dotnet.exe" --info; echo "rc=$?"
  log "test 2: net8.0 console app with UseAppHost=true"
  ( cd "$T/app"; PATH=/ucrt64/bin:$PATH    # the seed Mono runs MSBuild
    MSBuildSDKsPath=$(cygpath -w "$SDKROOT/sdk/8.0.131/Sdks") mono "$MSBOUT/MSBuild.exe" -nologo -m:1 -nr:false -v:${TEST_VERBOSITY:-m} \
      -restore -p:NetCoreRoot="$(cygpath -w "$SDKROOT")\\" -p:NetCoreTargetingPackRoot="$(cygpath -w "$SDKROOT/packs")" apphost.csproj )
  local exe=$T/app/bin/Debug/net8.0/apphost-test.exe
  [ -f "$exe" ] || die "no apphost-test.exe"
  ls -l "$exe"
  # the placeholder (SHA-256 of "foobar") must have been replaced by the app path
  if grep -qa c3ab8ff13720e8ad9047dd39466b3c8974e592c2fa383d4a3960714caef0c4f2 "$exe"; then die "apphost placeholder not patched"; fi
  grep -qa 'apphost-test.dll' "$exe" && echo "apphost bound to apphost-test.dll"
  log "test 3: run it (apphost -> hostfxr via DOTNET_ROOT; no Microsoft.NETCore.App runtime yet)"
  rc=0; DOTNET_ROOT=$(cygpath -w "$SDKROOT") "$exe" || rc=$?; echo "rc=$rc"
  log "test 4: a fake Microsoft.NETCore.App $PACK_VER with only hostpolicy.dll (hostfxr -> hostpolicy -> coreclr.dll missing)"
  local F=$T/fakeroot fw
  fw=$F/shared/Microsoft.NETCore.App/$PACK_VER   # the version the app's runtimeconfig.json asks for
  mkdir -p "$F/host/fxr/$HOST_VER" "$fw"
  cp "$SDKROOT/dotnet.exe" "$F/"; cp "$SDKROOT/host/fxr/$HOST_VER/hostfxr.dll" "$F/host/fxr/$HOST_VER/"
  cp "$O/corehost/hostpolicy.dll" "$fw/"
  printf '{"runtimeTarget":{"name":".NETCoreApp,Version=v8.0/win-x64"},"targets":{".NETCoreApp,Version=v8.0/win-x64":{"Microsoft.NETCore.App.Runtime.win-x64/%s":{"native":{"hostpolicy.dll":{}}}}},"libraries":{"Microsoft.NETCore.App.Runtime.win-x64/%s":{"type":"package","serviceable":false,"sha512":""}}}\n' "$PACK_VER" "$PACK_VER" > "$fw/Microsoft.NETCore.App.deps.json"
  "$F/dotnet.exe" --list-runtimes
  rc=0; COREHOST_TRACE=1 COREHOST_TRACEFILE=$(cygpath -w "$T/trace.txt") "$F/dotnet.exe" "$T/app/bin/Debug/net8.0/apphost-test.dll" || rc=$?
  echo "rc=$rc"
  grep -E 'Loaded library|CoreCLR|coreclr' "$T/trace.txt" | head -20
}

for s in $steps; do "step_$s"; done
