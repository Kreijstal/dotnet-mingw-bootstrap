#!/bin/bash
# S12: dotnet/command-line-api 02fe27cd (System.CommandLine 2.0.0-beta4.23307.1, what SDK 8.0.131, runtime
# 8.0.0 (crossgen2) and the templating repo pin) with `dotnet build/pack` of its sourcebuild.slnf
# (UseBetaVersion=true as its official build; OfficialBuildId 20230607.1).
# usage: s12-cmdline.sh [prep] [build]
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
. "$here/../env-m612.sh"
steps=${*:-prep build}
B=$M612/s12
. "$here/common.sh"
. "$here/s12-common.sh"
X=$WORK/command-line-api
TAR=$M612/src/command-line-api-02fe27cd.tar.gz
step_prep() { s12_prep_repo "$TAR" "$X" "$here/../../patches/m612/s12-command-line-api"; }
step_build() {
  s12_build cmdline "$X" sourcebuild.slnf -p:UseBetaVersion=true -p:OfficialBuildId=20230607.1 -p:RepositoryCommit=$(repo_commit "$TAR") -p:SourceRevisionId=$(repo_commit "$TAR") -p:RepositoryUrl=https://github.com/dotnet/command-line-api
}
for s in $steps; do log "s12 command-line-api (stage $STAGE): $s"; "step_$s"; done
