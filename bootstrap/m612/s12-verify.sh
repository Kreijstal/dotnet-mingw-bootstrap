#!/bin/bash
# S12: the bootstrap proof.  Compares what stage 2 (built by the stage-1 SDK) and stage 3 (the same
# sources, scripts and package inputs, built by the stage-2 SDK) produced:
#   bins    every managed .dll of the repos' output folders (artifacts/bin; NuGet's artifacts/*/bin;
#           Newtonsoft's bin), byte for byte; for the ones that differ: MVID, size, and the first
#           differing PE region (asminfo + cmp)
#   sdk     the IL SDK roots dotnet-sdk-8.0.131-stage2-il / -stage3-il, file by file
# Report: $S12/logs/verify-<what>.txt and a summary on stdout.
# usage: s12-verify.sh [bins] [sdk]
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
. "$here/../env-m612.sh"
steps=${*:-bins sdk}
B=$M612/s12
. "$here/common.sh"
STAGE=2 . "$here/s12-common.sh"
A=$S12/s2/work; Z=$S12/s3/work
asminfo() { CSC_DOTNET=$BUILD_SDK/dotnet.exe net8tool asminfo "$@"; }

# compare_tree <label> <dir2> <dir3> <find-args...>: one line per file: SAME / DIFF / ONLY2 / ONLY3
compare_tree() {
  local label=$1 d2=$2 d3=$3; shift 3
  local out=$S12/logs/verify-$label.txt; : > "$out"
  local rel same=0 diff=0 only=0
  while IFS= read -r rel; do
    if [ ! -f "$d3/$rel" ]; then echo "ONLY2 $rel" >> "$out"; only=$((only + 1))
    elif cmp -s "$d2/$rel" "$d3/$rel"; then echo "SAME  $rel" >> "$out"; same=$((same + 1))
    else echo "DIFF  $rel $(stat -c %s "$d2/$rel") $(stat -c %s "$d3/$rel") first-byte=$(cmp "$d2/$rel" "$d3/$rel" | awk '{print $5}' | tr -d ,)" >> "$out"; diff=$((diff + 1)); fi
  done < <(cd "$d2" && find . "$@" -type f | sed 's|^\./||' | sort)
  (cd "$d3" && find . "$@" -type f | sed 's|^\./||' | sort) | while IFS= read -r rel; do
    [ -f "$d2/$rel" ] || echo "ONLY3 $rel" >> "$out"; done
  echo "$label: $same identical, $diff different, $only only in stage 2, $(grep -c '^ONLY3' "$out" || true) only in stage 3  ($out)"
}

step_bins() {
  local r
  for r in arcade xliff-tasks roslyn msbuild xdt command-line-api templating deployment-tools sourcelink sdk; do
    [ -d "$A/$r/artifacts/bin" ] || continue
    compare_tree "bins-$r" "$A/$r/artifacts/bin" "$Z/$r/artifacts/bin" \( -name '*.dll' -o -name '*.exe' \) ! -path '*/obj/*'
  done
  compare_tree bins-nuget "$A/nuget/artifacts" "$Z/nuget/artifacts" \( -name '*.dll' -o -name '*.exe' \) -path '*/bin/*'
  compare_tree bins-newtonsoft "$A/newtonsoft-json/Src/Newtonsoft.Json/bin" "$Z/newtonsoft-json/Src/Newtonsoft.Json/bin" -name '*.dll'
  # the differing managed assemblies: identity, MVID (the hash of the content when Deterministic)
  local f list=()
  for f in "$S12"/logs/verify-bins-*.txt; do
    local r=${f#*verify-bins-}; r=${r%.txt}
    local base=$r; case $r in nuget) base=nuget/artifacts ;; newtonsoft) base=newtonsoft-json/Src/Newtonsoft.Json/bin ;; *) base=$r/artifacts/bin ;; esac
    while read -r _ rel _; do list+=("$(m "$A/$base/$rel")" "$(m "$Z/$base/$rel")"); done < <(grep '^DIFF' "$f" | grep -E '\.(dll|exe) ')
  done
  if [ ${#list[@]} -gt 0 ]; then asminfo "${list[@]:0:400}" | grep -E ' mvid=' > "$S12/logs/verify-bins-mvid.txt" || true; fi
}

step_sdk() {
  compare_tree sdk "$M612/dotnet-sdk-8.0.131-stage2-il/sdk" "$M612/dotnet-sdk-8.0.131-stage3-il/sdk"
}

for s in $steps; do log "s12 verify: $s"; "step_$s"; done
