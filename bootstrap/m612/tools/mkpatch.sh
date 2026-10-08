#!/bin/bash
# mkpatch.sh <src-tarball> <build-dir> <out.patch> <why-text> <file>...
# Diff the edited <file>s in <build-dir> against the pristine copies in the
# tarball (top-level directory stripped) and write a -p1 patch with a Why: header.
set -euo pipefail
tb=$1 bd=$2 out=$3 why=$4; shift 4
tmp=$(mktemp -d); trap 'rm -rf "${tmp:?}"' EXIT
top=$(set +o pipefail; tar -tzf "$tb" | head -1 | cut -d/ -f1)
tar -C "$tmp" -xzf "$tb" $(printf "$top/%s " "$@") 2>/dev/null || true
{ printf 'Why: %s\n\n' "$why"
  for f in "$@"; do
    a="$tmp/$top/$f"; [ -e "$a" ] || a=/dev/null
    diff -u --label "a/$f" --label "b/$f" "$a" "$bd/$f" || true
  done; } > "$out"
echo "wrote $out ($(grep -c '^@@' "$out") hunks)"
