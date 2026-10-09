#!/bin/bash
# S11: NuGet.Client 6.8.2.3 as net8.0 assemblies (what the .NET SDK ships in
# sdk/<ver>/: NuGet.*.dll, NuGet.Build.Tasks(.Console), NuGet.targets,
# Microsoft.Build.NuGetSdkResolver; NuGet.Build.Tasks.Pack for Sdks/NuGet.Build.Tasks.Pack),
# from the tree nuget.sh prepared, with sdkproj.sh (DotNetBuildFromSource=true:
# NETCoreTargetFramework=net8.0, as source-build).
#
# usage: nuget-net8.sh <version> [step...]     steps: libs tasks install   (default: all)
# Needs: msbuild-net8.sh (Microsoft.Build* for the tasks), clideps-net8.sh newtonsoft
# Output: $M612/nuget-<version>-net8/
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
. "$here/../env-m612.sh"
V=${1:?nuget version}; shift
steps=${*:-libs tasks install}
B=$M612/build/nuget-$V
BCSC=${BCSC:-$M612/roslyn-4.8.0/csc.exe}
MSB8=${MSB8:-$M612/msbuild-17.8.49-net8}
. "$here/common.sh"
. "$here/sdkproj.sh"
[ -d "$B/src/NuGet.Core" ] || die "run nuget.sh $V prep first"
OUT=$M612/nuget-$V-net8
SP_OUT=$B/obj/out-net8; SP_GROUP=nuget-net8; mkdir -p "$SP_OUT"
SP_TF=net8.0; SP_KEY=MSShared; SP_VER=$V; SP_FILEVER=$V; SP_INFOVER=$V; SP_DEFS=()
SP_EVAL=(-p DotNetBuildFromSource=true -p Configuration=Release -p IsNetCoreProject=true
  --pre "$B/build/common.project.props" --post "$B/build/common.targets")
SP_REFS=("$M612/net8-pkgs/Newtonsoft.Json.dll")
ng() { sdkproj "$B/src/NuGet.Core/$1/$1.csproj"; }

step_libs() {
  local l
  for l in NuGet.Frameworks NuGet.Common NuGet.Configuration NuGet.Versioning NuGet.Packaging NuGet.LibraryModel \
      NuGet.Protocol NuGet.DependencyResolver.Core NuGet.ProjectModel NuGet.Credentials NuGet.Commands NuGet.Resolver; do
    log "nuget lib $l (net8.0)"; ng "$l"
  done
}
step_tasks() {
  [ -f "$MSB8/Microsoft.Build.dll" ] || die "no net8.0 MSBuild ($MSB8)"
  SP_REFS=("$M612/net8-pkgs/Newtonsoft.Json.dll" "$MSB8/Microsoft.Build.Framework.dll" "$MSB8/Microsoft.Build.Utilities.Core.dll"
    "$MSB8/Microsoft.Build.Tasks.Core.dll" "$MSB8/Microsoft.Build.dll" "$MSB8/Microsoft.NET.StringTools.dll")
  local l; for l in NuGet.Build.Tasks NuGet.Build.Tasks.Console Microsoft.Build.NuGetSdkResolver NuGet.Build.Tasks.Pack; do
    log "nuget $l (net8.0)"; ng "$l"
  done
}
step_install() {
  rm -rf "${OUT:?}"; mkdir -p "$OUT/pack"
  local f
  for f in "$SP_OUT"/*.dll; do case $(basename "$f") in NuGet.Build.Tasks.Pack.dll) cp "$f" "$OUT/pack/" ;; *) cp "$f" "$OUT/" ;; esac; done
  cp "$SP_OUT"/*.pdb "$OUT/" 2>/dev/null || true
  local T=$B/src/NuGet.Core/NuGet.Build.Tasks
  cp "$T/NuGet.targets" "$T/NuGet.props" "$T/NuGet.RestoreEx.targets" "$OUT/" 2>/dev/null || true
  # NuGet.Build.Tasks.Pack: Sdks/NuGet.Build.Tasks.Pack/{build,buildCrossTargeting}/NuGet.Build.Tasks.Pack.targets
  local P=$B/src/NuGet.Core/NuGet.Build.Tasks.Pack
  mkdir -p "$OUT/pack/build" "$OUT/pack/buildCrossTargeting"
  cp "$P/NuGet.Build.Tasks.Pack.targets" "$OUT/pack/build/" 2>/dev/null || true
  cp "$P/NuGet.Build.Tasks.Pack.targets" "$OUT/pack/buildCrossTargeting/" 2>/dev/null || true
  cat > "$OUT/NuGet.Build.Tasks.Console.runtimeconfig.json" <<'EOJ'
{
  "runtimeOptions": {
    "tfm": "net8.0",
    "framework": { "name": "Microsoft.NETCore.App", "version": "8.0.0" },
    "rollForward": "Major"
  }
}
EOJ
  ls "$OUT"
}

for s in $steps; do log "nuget $V net8.0: $s"; "step_$s"; done
