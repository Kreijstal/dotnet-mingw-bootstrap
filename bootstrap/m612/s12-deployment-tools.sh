#!/bin/bash
# S12: dotnet/deployment-tools 5255d40e (Microsoft.Deployment.DotNet.Releases 2.0.0-rtm.1.25064.1, the
# version SDK 8.0.131 pins; OfficialBuildId 20250114.1) with `dotnet build/pack` of its project.
# usage: s12-deployment-tools.sh [prep] [build]
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
. "$here/../env-m612.sh"
steps=${*:-prep build}
B=$M612/s12
. "$here/common.sh"
. "$here/s12-common.sh"
SHA=5255d40e228ea1d4b624781b5b97ec16484a3b4b
X=$WORK/deployment-tools
TAR=$M612/src/deployment-tools-$SHA.tar.gz
step_prep() { s12_prep_repo "$TAR" "$X" "$here/../../patches/m612/s12-deployment-tools"; }
step_build() {
  s12_build deployment-tools "$X" src/Microsoft.Deployment.DotNet.Releases/src/Microsoft.Deployment.DotNet.Releases.csproj \
    -p:OfficialBuildId=20250114.1 -p:RepositoryCommit=$SHA
}
for s in $steps; do log "s12 deployment-tools (stage $STAGE): $s"; "step_$s"; done
