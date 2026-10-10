#!/bin/bash
# S12: managed parts of dotnet/runtime v8.0.0 with `dotnet build` of their own project files (the
# out-of-band library packages: Microsoft.Extensions.*, System.* OOB), under our Arcade SDK.
# usage: s12-runtime.sh [prep] [build]       (PROJECTS="src/libraries/X/src/X.csproj ..." for build)
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
. "$here/../env-m612.sh"
steps=${*:-prep build}
B=$M612/s12
. "$here/common.sh"
. "$here/s12-common.sh"
X=$WORK/runtime
TAR=$M612/src/runtime-8.0.0.tar.gz
step_prep() { s12_prep_repo "$TAR" "$X" "$here/../../patches/m612/s12-runtime-8.0.0"; }
step_build() {
  local p
  for p in ${PROJECTS:?}; do
    s12_build runtime-$(basename "$p" .csproj) "$X" "$p" -p:OfficialBuildId=20231031.4 -p:RepositoryCommit=$(repo_commit "$TAR") -p:SourceRevisionId=$(repo_commit "$TAR") -p:RepositoryUrl=https://github.com/dotnet/runtime \
      ${RUNTIME_EXTRA:-}
  done
}
for s in $steps; do log "s12 runtime (stage $STAGE): $s"; "step_$s"; done
