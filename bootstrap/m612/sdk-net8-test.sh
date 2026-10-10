#!/bin/bash
# S11 end-to-end proof: the SDK of sdk-layout-net8.sh, alone (dotnet.exe -> hostfxr ->
# sdk/8.0.131/dotnet.dll -> MSBuild.dll/csc.dll on the source-built runtime; no Mono 6.12):
#   info      dotnet --info, --list-sdks, --list-runtimes
#   new       dotnet new console; dotnet build; dotnet run
#   pkgref    dotnet pack a library into a local feed; a console app with a PackageReference to it,
#             restored from that feed only (NuGet.Config <clear/>); build + run
#   multi     dotnet new sln + sln add of 3 projects with ProjectReferences; dotnet build -m (MSBuild
#             nodes + node reuse, compiler server); run
#   roslyncore  a nontrivial project: Microsoft.CodeAnalysis 4.8 (~1500 files) as an SDK-style
#             net8.0 library; the result replaces the compiler's own copy and compiles hello again
#   selfcontained  dotnet publish -r win-x64 --self-contained (runtime pack), the exe runs alone
#   trimmed   dotnet publish -p:PublishTrimmed=true (ILLink from source), the trimmed exe runs
#   generators  [GeneratedRegex]/[LibraryImport]/[JsonSerializable] through the reference pack's generators
#   addpackage  dotnet add package / dotnet list package against the local feed of step pkgref
#             (NuGet.CommandLine.XPlat; the S12 SDKs have it, the S11 roots do not)
#   mono      the same new/roslyncore builds with the mono-based SDK (mono MSBuild.exe), for timings
# Each step prints wall-clock times ("TIME <what> <seconds>").
#
# usage: sdk-net8-test.sh [step...]   (default: info new pkgref multi roslyncore generators selfcontained trimmed)
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
. "$here/../env-m612.sh"
steps=${*:-info new pkgref multi roslyncore generators selfcontained trimmed}
ROOT=${ROOT:-$M612/dotnet-sdk-8.0.131-net8}
D=$ROOT/dotnet.exe
T=${TESTDIR:-$M612/test/sdk-net8}
export DOTNET_CLI_TELEMETRY_OPTOUT=1 DOTNET_NOLOGO=1 DOTNET_SKIP_FIRST_TIME_EXPERIENCE=1
# keep the user's NuGet caches out of the way of the tests
export NUGET_PACKAGES=$(cygpath -w "$T/nuget-packages") DOTNET_CLI_HOME=$(cygpath -w "$T/home")
mkdir -p "$T"
. "$here/common.sh"
timed() { # timed <label> cmd... : run, print TIME <label> <s>
  local l=$1; shift; local t0 t1 rc=0; t0=$(date +%s.%N)
  "$@" || rc=$?
  t1=$(date +%s.%N); printf 'TIME %-34s %6.1f s (rc=%d)\n' "$l" "$(awk "BEGIN{print $t1 - $t0}")" "$rc"
  return $rc
}

step_info() {
  "$D" --info
  "$D" --list-sdks; "$D" --list-runtimes
}
step_new() {
  local W=$T/new; rm -rf "${W:?}"; mkdir -p "$W"; cd "$W"
  timed "dotnet new console" "$D" new console -o hello
  cd hello
  timed "dotnet build (first)" "$D" build
  touch Program.cs
  timed "dotnet build (incremental)" "$D" build
  timed "dotnet run" "$D" run
  "$D" bin/Debug/net8.0/hello.dll
  DOTNET_ROOT=$(cygpath -w "$ROOT") ./bin/Debug/net8.0/hello.exe   # apphost: no global install, so DOTNET_ROOT
}
step_pkgref() {
  local W=$T/pkgref; rm -rf "${W:?}"; mkdir -p "$W/feed"; cp -r "$here"/tests/net8sdk/greeting "$here"/tests/net8sdk/consumer "$W/"
  cd "$W/greeting"
  timed "dotnet pack" "$D" pack -c Release -o ../feed
  unzip -l ../feed/bootstrap.greeting.2.0.0.nupkg
  cd "$W/consumer"
  timed "dotnet build (pkgref)" "$D" build
  grep -o '"Bootstrap.Greeting/2.0.0"' obj/project.assets.json | head -1
  timed "dotnet run (pkgref)" "$D" run --no-build
}
step_multi() {
  local W=$T/multi; rm -rf "${W:?}"; mkdir -p "$W"; cp -r "$here"/tests/net8sdk/multi/* "$W/"; cd "$W"
  "$D" new sln -n multi
  "$D" sln multi.sln add LibA/LibA.csproj LibB/LibB.csproj App/App.csproj
  timed "dotnet build -m:4 (3 projects)" "$D" build multi.sln -m:4
  timed "dotnet build -m:4 (no-op)" "$D" build multi.sln -m:4
  "$D" App/bin/Debug/net8.0/App.dll
  "$D" build-server shutdown
}
step_roslyncore() {
  local W=$T/roslyncore; rm -rf "${W:?}"; mkdir -p "$W"; cp "$here"/tests/net8sdk/roslyncore/* "$W/"; cd "$W"
  local args=(-c Release -p:RoslynSrc="$(cygpath -w "$M612/build/roslyn-4.8.0")" -p:KeyFile="$(keyfile MSShared)")
  timed "dotnet build roslyncore" "$D" build "${args[@]}"
  local out=bin/Release/net8.0/Microsoft.CodeAnalysis.dll; ls -la "$out"
  # use it: a copy of the compiler with this Microsoft.CodeAnalysis.dll compiles and runs hello
  rm -rf cc; cp -r "$ROOT/sdk/8.0.131/Roslyn/bincore" cc; cp "$out" "${out%.dll}.pdb" cc/
  cat > h.cs <<'EOC'
System.Console.WriteLine($"compiled by a Microsoft.CodeAnalysis that the source-built SDK built: {typeof(object).Assembly.GetName().Version}");
EOC
  local refs=() f; for f in "$ROOT"/packs/Microsoft.NETCore.App.Ref/*/ref/net8.0/*.dll; do refs+=("-r:$(cygpath -m "$f")"); done
  timed "csc.dll (rebuilt MCA) hello" "$D" exec cc/csc.dll -nologo -noconfig -nostdlib "${refs[@]}" -out:h.dll h.cs
  printf '{"runtimeOptions":{"tfm":"net8.0","framework":{"name":"Microsoft.NETCore.App","version":"8.0.0"}}}' > h.runtimeconfig.json
  "$D" h.dll
}
step_generators() {
  local W=$T/generators; rm -rf "${W:?}"; mkdir -p "$W"; cp "$here"/tests/net8sdk/generators/* "$W/"; cd "$W"
  timed "dotnet build (generators)" "$D" build
  ls obj/Debug/net8.0/generated/*/* | sed 's|.*/generated/||'
  "$D" run --no-build
}
step_addpackage() {
  local W=$T/pkgref; [ -d "$W/feed" ] || die "run step pkgref first"
  rm -rf "${W:?}/addpkg"; "$D" new console -o "$W/addpkg" > /dev/null; cp "$W/consumer/NuGet.Config" "$W/addpkg/"; cd "$W/addpkg"
  timed "dotnet add package" "$D" add package Bootstrap.Greeting --version 2.0.0
  grep PackageReference addpkg.csproj
  "$D" list package
}
# self-contained publish with the source-built runtime pack (framework.sh runtimepack): the apphost
# finds hostfxr/hostpolicy/coreclr.dll (Mono) and the 168 assemblies next to it, no dotnet root needed
step_selfcontained() {
  local W=$T/selfcontained; rm -rf "${W:?}"; mkdir -p "$W"; cd "$W"
  "$D" new console -o sc > /dev/null; cd sc
  timed "dotnet publish --self-contained" "$D" publish -c Release -r win-x64 --self-contained
  local P=bin/Release/net8.0/win-x64/publish
  echo "publish: $(ls "$P" | wc -l) files, $(du -sh "$P" | cut -f1)"; ls "$P" | grep -i 'sc\.\|coreclr\|host\|CoreLib'
  ( unset DOTNET_ROOT; PATH=/usr/bin; "$P/sc.exe" )
}
# PublishTrimmed: the ILLink package from <root>/library-packs (illink-net8.sh), illink.dll on the runtime
step_trimmed() {
  local W=$T/trimmed; rm -rf "${W:?}"; mkdir -p "$W"; cd "$W"
  "$D" new console -o tr > /dev/null; cd tr
  timed "dotnet publish -p:PublishTrimmed=true" "$D" publish -c Release -r win-x64 --self-contained -p:PublishTrimmed=true
  local P=bin/Release/net8.0/win-x64/publish
  echo "trimmed publish: $(ls "$P" | wc -l) files, $(du -sh "$P" | cut -f1) (untrimmed: see selfcontained)"
  ( unset DOTNET_ROOT; PATH=/usr/bin; "$P/tr.exe" )
}
# the mono-based SDK (dotnet-sdk-8.0.131 + mono MSBuild.exe) on the same projects, for the timings
step_mono() {
  local OLD=$M612/dotnet-sdk-8.0.131 MSB=$M612/msbuild-17.8.49/tools/MSBuild.exe W=$T/mono
  rm -rf "${W:?}"; mkdir -p "$W"; cp -r "$T/new/hello" "$W/" 2>/dev/null || die "run step new first"
  rm -rf "$W/hello/bin" "$W/hello/obj"; cd "$W/hello"
  local P=(-nologo -m:1 -nr:false -v:m -p:EnableNETAnalyzers=false -p:NetCoreRoot="$(cygpath -w "$OLD")\\" -p:NetCoreTargetingPackRoot="$(cygpath -w "$OLD/packs")")
  export MSBuildSDKsPath; MSBuildSDKsPath=$(cygpath -w "$OLD/sdk/8.0.131/Sdks")
  timed "mono msbuild hello (first)" mono "$MSB" "${P[@]}" -restore
  touch Program.cs
  timed "mono msbuild hello (incremental)" mono "$MSB" "${P[@]}"
  mkdir -p "$W/roslyncore"; cp "$here"/tests/net8sdk/roslyncore/* "$W/roslyncore/"; cd "$W/roslyncore"
  # (GenerateResourceMSBuildRuntime=CLR4: MSBuild on Mono does not take its own runtime for "CurrentRuntime", MSB4214)
  timed "mono msbuild roslyncore" mono "$MSB" "${P[@]}" -restore -p:Configuration=Release -p:GenerateResourceMSBuildRuntime=CLR4 \
    -p:RoslynSrc="$(cygpath -w "$M612/build/roslyn-4.8.0")" -p:KeyFile="$(keyfile MSShared)"
  unset MSBuildSDKsPath
}

for s in $steps; do log "sdk net8 test: $s"; "step_$s"; done
