#!/usr/bin/env bash
set -euo pipefail
root=$(cd "${1:?usage: check-eha.sh PREPARED_ROOT NEW_OUTPUT}" && pwd)
out=$(realpath -m "$2")
here=$(cd "$(dirname "$0")" && pwd)
test ! -e "$out"
mkdir -p "$out"
clang++ -O2 -fms-extensions -frtti -c \
  "$root/r/src/coreclr/minipal/Windows/mingw_eha.cpp" -o "$out/eha.o"
clang++ -O2 -fms-extensions -fno-rtti "$here/inputs/eha.cpp" "$out/eha.o" \
  -o "$out/eha.exe" -static -Wl,--wrap=__gxx_personality_seh0 \
  -Wl,--wrap=_Unwind_Resume -Wl,--wrap=__cxa_rethrow -Wl,--wrap=__C_specific_handler
/clang64/bin/python.exe - "$(cygpath -am "$out")" <<'PY'
from pathlib import Path
import json,subprocess,sys
out=Path(sys.argv[1])
result=subprocess.run([str(out/'eha.exe')],capture_output=True,text=True,timeout=60)
(out/'eha.log').write_text(result.stdout+result.stderr)
assert result.returncode==0,(result.returncode,result.stdout,result.stderr)
lines=result.stdout.splitlines()
# The original fixture prints failures but always returns0. Inspect every case.
assert len(lines)==11 and all(' ok ' in line and 'FAIL' not in line for line in lines),lines
(out/'results.json').write_text(json.dumps(dict(exit_code=0,all_printed_cases_passed=True,
    case_count=11,stress_round_trips=200000,cases=lines,full_coreclr10_execution_verified=False),indent=2)+'\n')
print('PASS source EHa shim: eleven original cases and200000 stress round trips; printed failures checked')
PY
