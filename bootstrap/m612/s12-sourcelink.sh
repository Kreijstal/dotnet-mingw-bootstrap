#!/bin/bash
# S12: dotnet/sourcelink v8.0.0 (public tag; SDK 8.0.131 pins 8.0.0-beta.26351.4 from the internal mirror)
# with `dotnet build/pack` of the projects the SDK bundles: Microsoft.Build.Tasks.Git and
# Microsoft.SourceLink.{Common,GitHub,GitLab,AzureRepos.Git,Bitbucket.Git}.
# usage: s12-sourcelink.sh [prep] [build]
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
. "$here/../env-m612.sh"
steps=${*:-prep build}
B=$M612/s12
. "$here/common.sh"
. "$here/s12-common.sh"
X=$WORK/sourcelink
TAR=$M612/src/sourcelink-8.0.0.tar.gz
PROJS="Microsoft.Build.Tasks.Git SourceLink.Common SourceLink.GitHub SourceLink.GitLab SourceLink.AzureRepos.Git SourceLink.Bitbucket.Git"
step_prep() {
  s12_prep_repo "$TAR" "$X" "$here/../../patches/m612/s12-sourcelink"
  python3 -S - "$X/s12.slnf" $PROJS <<'EOPY'
import json, sys
def proj(d):
    return 'src\\%s\\%s.csproj' % (d, d if d.startswith('Microsoft.') else 'Microsoft.' + d)
json.dump({'solution': {'path': 'SourceLink.sln', 'projects': [proj(p) for p in sys.argv[2:]]}},
          open(sys.argv[1], 'w', newline='\n'), indent=2)
EOPY
}
step_build() { s12_build sourcelink "$X" s12.slnf -p:OfficialBuildId=20230828.1 -p:RepositoryCommit=$(repo_commit "$TAR") -p:SourceRevisionId=$(repo_commit "$TAR") -p:RepositoryUrl=https://github.com/dotnet/sourcelink; }
for s in $steps; do log "s12 sourcelink (stage $STAGE): $s"; "step_$s"; done
