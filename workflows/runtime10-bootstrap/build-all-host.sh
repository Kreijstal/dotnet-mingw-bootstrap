#!/usr/bin/env bash
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
archive=$(realpath "${1:?usage: build-all-host.sh ARCHIVE NEW_ROOT [JOBS]}")
root=$(realpath -m "${2:?missing new root}")
mkdir "$root"
cp "$archive" "$root/runtime-v10.0.5.tar.gz"
/usr/bin/python3 "$here/prepare-host.py" "$root/runtime-v10.0.5.tar.gz" "$root"
/usr/bin/python3 "$here/prepare-version.py" "$root"
/usr/bin/python3 "$here/generate-host-resources.py" "$root"
bash "$here/build-host.sh" "$root" "${3:-2}"
