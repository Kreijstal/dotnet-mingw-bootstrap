#!/bin/bash
# S12: dotnet/xliff-tasks (Microsoft.DotNet.XliffTasks 1.0.0-beta.23475.1, the version Arcade 8.0 and
# the repos pin): the MSBuild tasks that turn the repos' .xlf translations into satellite .resx/.resources
# (Arcade's UsingToolXliff).  Built from its project file under the stage's Arcade SDK.
# usage: s12-xliff.sh [prep] [build]
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
. "$here/../env-m612.sh"
steps=${*:-prep build}
B=$M612/s12
. "$here/common.sh"
. "$here/s12-common.sh"
X=$WORK/xliff-tasks
TAR=$S12/src/xliff-tasks-$XLIFF_SHA.tar.gz
step_prep() {
  fetch_tarball "https://github.com/dotnet/xliff-tasks/archive/$XLIFF_SHA.tar.gz" "$TAR"
  s12_prep_repo "$TAR" "$X" "$here/../../patches/m612/xliff-tasks"
}
step_build() {
  s12_build xliff "$X" src/XliffTasks/XliffTasks.csproj -p:OfficialBuildId=20230925.1 -p:RepositoryCommit=$XLIFF_SHA -p:SourceRevisionId=$XLIFF_SHA -p:RepositoryUrl=https://github.com/dotnet/xliff-tasks
}
for s in $steps; do log "s12 xliff-tasks (stage $STAGE): $s"; "step_$s"; done
