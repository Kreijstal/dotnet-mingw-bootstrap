#!/bin/bash
# Build the whole C-rooted stage 0 (treecc, pnet, pnetlib) in order.
set -euo pipefail
d=$(dirname "$0")
export WITH_BDWGC=${WITH_BDWGC:-$([ "${MSYSTEM:-UCRT64}" = MINGW32 ] && echo 1)}
if [ -n "${WITH_BDWGC:-}" ]; then bash "$d/00-bdwgc.sh"; fi
bash "$d/01-treecc.sh"
bash "$d/02-pnet.sh"
bash "$d/03-pnetlib.sh"
