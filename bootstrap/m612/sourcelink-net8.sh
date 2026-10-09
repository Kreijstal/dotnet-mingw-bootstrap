#!/bin/bash
# S11: dotnet/sourcelink 8.0.0 for the net8.0 SDK: Microsoft.Build.Tasks.Git and the
# Microsoft.SourceLink.{Common,GitHub,GitLab,AzureRepos.Git,Bitbucket.Git} MSBuild tasks
# (net8.0 = their "tools/core" flavour), laid out like the SDK bundles them in
# sdk/<ver>/Sdks/<package>/{build,buildMultiTargeting,tools/core}.  The SDK imports them
# from Microsoft.NET.Sdk.SourceLink.props unless SuppressImplicitGitSourceLink=true.
#
# usage: sourcelink-net8.sh [step...]    steps: build install   (default: both)
# (the SDK pins 8.0.0-beta.26351.4 from the internal mirror; the public 8.0.0 tag is used)
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
. "$here/../env-m612.sh"
steps=${*:-build install}
SL=$M612/src/sourcelink-8.0.0
B=$M612/build/sourcelink-net8; mkdir -p "$B/obj"
OUT=$M612/sourcelink-8.0.0-net8
BCSC=${BCSC:-$M612/roslyn-4.8.0/csc.exe}
MSB8=${MSB8:-$M612/msbuild-17.8.49-net8}
. "$here/common.sh"
. "$here/sdkproj.sh"
[ -d "$SL/src" ] || { mkdir -p "$SL"; tar -C "$SL" --strip-components=1 -xzf "$M612/src/sourcelink-8.0.0.tar.gz"; }
[ -f "$SL/.deleted-binaries.txt" ] || delete_binaries "$SL"
SP_OUT=$B/out; SP_GROUP=sourcelink-net8; SP_TF=net8.0; SP_KEY=MSShared; SP_VER=8.0.0.0; SP_FILEVER=8.0.0.0; SP_INFOVER=8.0.0; SP_DEFS=()
SP_EVAL=(-p NetCurrent=net8.0 -p NetMinimum=net6.0 -p RepoRoot=$SL/ -p Configuration=Release --pre "$SL/src/Directory.Build.props")
SP_REFS=("$MSB8/Microsoft.Build.Framework.dll" "$MSB8/Microsoft.Build.Utilities.Core.dll" "$MSB8/Microsoft.Build.Tasks.Core.dll" "$MSB8/Microsoft.Build.dll" "$MSB8/Microsoft.NET.StringTools.dll")
PKGS="Microsoft.Build.Tasks.Git:Microsoft.Build.Tasks.Git Microsoft.SourceLink.Common:SourceLink.Common Microsoft.SourceLink.GitHub:SourceLink.GitHub
  Microsoft.SourceLink.GitLab:SourceLink.GitLab Microsoft.SourceLink.AzureRepos.Git:SourceLink.AzureRepos.Git Microsoft.SourceLink.Bitbucket.Git:SourceLink.Bitbucket.Git"

step_build() {
  local e id dir; for e in $PKGS; do id=${e%%:*}; dir=${e#*:}
    sdkproj "$(ls "$SL/src/$dir"/*.csproj)"
  done
}
step_install() {
  rm -rf "${OUT:?}"; local e id dir
  for e in $PKGS; do id=${e%%:*}; dir=${e#*:}
    mkdir -p "$OUT/$id/tools/core"
    cp -r "$SL/src/$dir/build" "$SL/src/$dir/buildMultiTargeting" "$OUT/$id/"
    cp "$SP_OUT/$id.dll" "$OUT/$id/tools/core/"; cp "$SP_OUT/$id.pdb" "$OUT/$id/tools/core/" 2>/dev/null || true
  done
  ls "$OUT"
}

for s in $steps; do log "sourcelink net8.0: $s"; "step_$s"; done
