#!/bin/bash
# S12: the bootstrap proof.  Compares what two stages produced from the same sources, scripts and
# package inputs (V1=2 V2=3: stage 2 built by the stage-1 SDK vs stage 3 built by the stage-2 SDK;
# V1=3 V2=4: the fixed point):
#   bins    every managed .dll of the repos' output folders (artifacts/bin; NuGet's artifacts/*/bin;
#           Newtonsoft's bin), byte for byte; every differing managed assembly is run through
#           tools/pecompare.cs: NORMALIZED-SAME = identical once the content-hash-derived fields (MVID,
#           COFF stamp, PDB id/checksum) are blanked, with the portable-PDB fields that differ
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
V1=${V1:-2}; V2=${V2:-3}
A=$S12/s$V1/work; Z=$S12/s$V2/work
asminfo() { CSC_DOTNET=$BUILD_SDK/dotnet.exe net8tool asminfo "$@"; }

# compare_tree <label> <dir2> <dir3> <find-args...>: one line per file: SAME / DIFF / ONLY2 / ONLY3
compare_tree() {
  local label=$1 d2=$2 d3=$3; shift 3
  local out=$S12/logs/verify-s$V1-s$V2-$label.txt; : > "$out"
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
  # why the differing ones differ (one assembly per output directory is enough: copies are the same file)
  CSC_DOTNET=$BUILD_SDK/dotnet.exe net8tool pecompare > /dev/null 2>&1 || true      # (compiles the tool)
  local f out=$S12/logs/verify-s$V1-s$V2-why.txt; : > "$out"
  for f in "$S12"/logs/verify-s$V1-s$V2-bins-*.txt; do
    local r=${f#*-bins-} base; r=${r%.txt}
    case $r in nuget) base=nuget/artifacts ;; newtonsoft) base=newtonsoft-json/Src/Newtonsoft.Json/bin ;; *) base=$r/artifacts/bin ;; esac
    grep '^DIFF' "$f" | awk '{print $2}' | grep -E '\.(dll|exe)$' | grep -v '/publish/' | while read -r rel; do
      local a=$A/$base/$rel z=$Z/$base/$rel
      "$BUILD_SDK/dotnet.exe" "$(m "$M612/tools/pecompare.dll")" "$(m "$a")" "$(m "$z")" "$(m "${a%.*}.pdb")" "$(m "${z%.*}.pdb")" 2>&1 \
        | sed "s|^NORMALIZED|$r/$rel NORMALIZED|"
    done >> "$out"
  done
  echo "differences explained: $(grep -c NORMALIZED-SAME "$out" || true) assemblies identical apart from their PDB-derived fields, $(grep -c NORMALIZED-DIFF "$out" || true) not ($out)"
  grep -h "pdb option\|pdb document" "$out" | sed 's/: .*//' | sort | uniq -c | sort -rn | head -20 || true
}

step_sdk() {
  compare_tree sdk "$M612/dotnet-sdk-8.0.131-stage$V1-il/sdk" "$M612/dotnet-sdk-8.0.131-stage$V2-il/sdk"
}

for s in $steps; do log "s12 verify: $s"; "step_$s"; done
