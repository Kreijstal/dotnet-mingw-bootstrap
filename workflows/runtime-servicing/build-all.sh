#!/usr/bin/env bash
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
runtime=${1:?usage: build-all.sh RUNTIME_ARCHIVE BOOTSTRAP_ARCHIVE NEW_ROOT NEW_LAYOUT [JOBS]}
bootstrap=${2:?missing pinned bootstrap archive}
root=$(realpath -m "${3:?missing new root}")
layout=$(realpath -m "${4:?missing new layout}")
/usr/bin/python3 "$here/prepare.py" "$runtime" "$bootstrap" "$root"
/usr/bin/python3 "$here/prepare-version.py" "$root"
/usr/bin/python3 "$here/prepare-file-version.py" "$root"
bash "$here/build-native.sh" "$root" "${5:-2}"
/usr/bin/python3 "$here/prepare-managed.py" "$root"
bash "$here/build-generators.sh" "$root"
/usr/bin/python3 "$here/prepare-corelib.py" "$root"
bash "$here/build-corelib.sh" "$root"
/usr/bin/python3 "$here/prepare-framework.py" "$root"
bash "$here/build-framework.sh" "$root" all
bash "$here/build-compression.sh" "$root" "${5:-2}"
/usr/bin/python3 "$here/audit-build.py" "$root"
/usr/bin/python3 "$here/audit-framework.py" "$root"
/usr/bin/python3 "$here/audit-shim-identities.py" "$root"
/usr/bin/python3 "$here/stage-runtime.py" "$root" "$layout"
