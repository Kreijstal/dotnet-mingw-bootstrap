#!/usr/bin/env bash
# Build references and generators from source; no cached production DLL inputs.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
runtime=${1:?usage: build-targeting-pack.sh RUNTIME_ARCHIVE BOOTSTRAP_ARCHIVE NEW_ROOT NEW_PACK NEW_LICENSES}
bootstrap=${2:?missing bootstrap archive}
root=$(realpath -m "${3:?missing new prepared root}")
pack=$(realpath -m "${4:?missing new targeting pack}")
licenses=$(realpath -m "${5:?missing new license directory}")
/usr/bin/python3 "$here/prepare.py" "$runtime" "$bootstrap" "$root"
/usr/bin/python3 "$here/prepare-version.py" "$root"
/usr/bin/python3 "$here/prepare-file-version.py" "$root"
/usr/bin/python3 "$here/prepare-framework.py" "$root"
bash "$here/build-framework.sh" "$root" refs
/usr/bin/python3 "$here/audit-framework.py" "$root" --references-only
/usr/bin/python3 "$here/audit-shim-identities.py" "$root"
/usr/bin/python3 "$here/prepare-managed.py" "$root"
/usr/bin/python3 "$here/prepare-targeting-generators.py" "$root"
/usr/bin/python3 "$here/prepare-targeting-common.py" "$root"
bash "$here/build-targeting-generators.sh" "$root" \
  Microsoft.Interop.SourceGeneration Microsoft.Interop.LibraryImportGenerator \
  Microsoft.Interop.ComInterfaceGenerator System.Text.RegularExpressions.Generator \
  'JSImport' 'Json3.11' 'Json4.0' 'Json4.4'
bash "$here/inspect-targeting-generators.sh" "$root" "$here/InspectAssemblyMetadata.cs"
/usr/bin/python3 "$here/audit-targeting-generators.py" "$root" "$root/targeting-generators/pe-metadata.json"
/usr/bin/python3 "$here/stage-targeting-pack.py" "$root" "$root" "$pack-raw"
/usr/bin/python3 "$here/complete-targeting-manifest.py" "$root" "$pack-raw" "$pack"
/usr/bin/python3 "$here/collect-targeting-licenses.py" "$root" "$pack" "$licenses"
echo 'PASS original-source references/generators, complete platform manifest and source license closure; SDK/native package checks remain required'
