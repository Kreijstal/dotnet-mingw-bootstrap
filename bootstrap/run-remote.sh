#!/bin/bash
# Linux-side driver: sync bootstrap/ and patches/ to win11 and run one stage
# script detached; log goes to ~/dotnetboot/logs/<stage>.log, then a line rc=N.
# usage: run-remote.sh <script> [MSYSTEM]
set -euo pipefail
here=$(cd "$(dirname "$0")/.." && pwd)
s=$1; ms=${2:-UCRT64}; name=$(basename "$s" .sh)
rsync -a --delete "$here/bootstrap" "$here/patches" topkek@win11:dotnetboot/repo/
ssh -n topkek@win11 "cd ~/dotnetboot && nohup env MSYSTEM=$ms bash -c 'bash repo/bootstrap/$s > logs/$name.log 2>&1; echo rc=\$? >> logs/$name.log' > /dev/null 2>&1 < /dev/null &"
echo "started $name ($ms)"
