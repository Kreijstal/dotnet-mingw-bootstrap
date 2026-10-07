#!/bin/bash
# Developer helper: turn the uncommitted diff of a scratch git work tree into
# patches/<stage>/NNNN-<name>.patch with an explanatory header, then commit it
# in the work tree so the next patch starts from there.
# usage: mkpatch.sh <worktree> <patchdir> <name> <why-text>
set -euo pipefail
wt=$1 pd=$2 name=$3 why=$4
shopt -s nullglob; existing=("$pd"/[0-9][0-9][0-9][0-9]-*.patch); n=${#existing[@]}; mkdir -p "$pd"
out=$(printf '%s/%04d-%s.patch' "$pd" $((n+1)) "$name")
{ printf '%s\n' "$why" | fold -s -w 76; echo; git -C "$wt" diff --no-color -U3 --no-prefix --src-prefix=a/ --dst-prefix=b/; } > "$out"
git -C "$wt" commit -qam "$name"
echo "$out"
