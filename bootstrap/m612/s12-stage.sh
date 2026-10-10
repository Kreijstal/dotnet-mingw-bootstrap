#!/bin/bash
# S12: one stage of the self-rebuild, in dependency order (STAGE=2: built by the stage-1 SDK;
# STAGE=3: built by the stage-2 SDK, the determinism check).  The shared, stage-independent inputs
# (source-build-reference-packages, the fixed targeting pack, the seed feed) are made in stage 2.
# usage: s12-stage.sh [first-step]     (STAGE=2|3; steps are numbered below)
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
. "$here/../env-m612.sh"
STAGE=${STAGE:-2}; export STAGE
start=${1:-1}
step() { local n=$1; shift; [ "$n" -ge "$start" ] || return 0; log "s12 stage $STAGE step $n: $*"; local t0=$SECONDS; "$@"; echo "STEPTIME $n $((SECONDS - t0)) s: $*"; }
# a stage starts from nothing of its own (its feed, package cache, trees and bootstrap packages)
if [ "$start" -le 4 ]; then
  SN=${S12:-$M612/s12}/s$STAGE
  for d in "$SN/work" "$SN/feed" "$SN/packages" "$SN/boot"; do rm -rf "${d:?}"; done; rm -f "$SN/versions.props"
fi
if [ "$STAGE" = 2 ]; then
  step 1 bash "$here/s12-sbrp.sh" prep targetpacks textonly
  step 2 bash "$here/s12-packs.sh"
  step 3 bash "$here/s12-feed.sh"
fi
step 4 bash "$here/s12-arcade.sh"
step 5 bash "$here/s12-xliff.sh"
step 6 bash "$here/s12-roslyn.sh"
step 7 bash "$here/s12-msbuild.sh"
step 8 bash "$here/s12-newtonsoft.sh"
step 9 bash "$here/s12-xdt.sh"
step 10 bash "$here/s12-nuget.sh"
step 11 bash "$here/s12-cmdline.sh"
step 12 bash "$here/s12-templating.sh"
step 13 bash "$here/s12-deployment-tools.sh"
step 14 bash "$here/s12-sourcelink.sh"
step 15 bash "$here/s12-sdk.sh"
step 16 bash "$here/s12-layout.sh"
