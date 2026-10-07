#!/bin/bash
# Build the whole C-rooted stage 0 (treecc, pnet, pnetlib) in order.
set -euo pipefail
d=$(dirname "$0")
bash "$d/01-treecc.sh"
bash "$d/02-pnet.sh"
bash "$d/03-pnetlib.sh"
