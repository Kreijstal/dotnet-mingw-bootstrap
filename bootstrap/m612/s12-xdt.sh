#!/bin/bash
# S12: dotnet/xdt (Microsoft.Web.Xdt 7.0.0-preview.22423.2, the version SDK 8.0.131 pins; commit 9a1c3e1b),
# the XML-document-transform library NuGet uses, with `dotnet build/pack` of its project under our Arcade.
# usage: s12-xdt.sh [prep] [build]
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
. "$here/../env-m612.sh"
steps=${*:-prep build}
B=$M612/s12
. "$here/common.sh"
. "$here/s12-common.sh"
SHA=9a1c3e1b7f0c8763d4c96e593961a61a72679a7b
X=$WORK/xdt
TAR=$S12/src/xdt-$SHA.tar.gz
step_prep() {
  fetch_tarball "https://github.com/dotnet/xdt/archive/$SHA.tar.gz" "$TAR"
  s12_prep_repo "$TAR" "$X" "$here/../../patches/m612/s12-xdt"
}
step_build() { s12_build xdt "$X" src/Microsoft.Web.XmlTransform/Microsoft.Web.XmlTransform.csproj -p:OfficialBuildId=20220823.2 -p:RepositoryCommit=$SHA; }
for s in $steps; do log "s12 xdt (stage $STAGE): $s"; "step_$s"; done
