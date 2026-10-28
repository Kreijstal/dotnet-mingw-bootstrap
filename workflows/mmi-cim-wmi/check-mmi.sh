#!/usr/bin/env bash
# Run in MSYS2 CLANG64 against the installed source-built SDK/runtime.
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
out=${1:?usage: check-mmi.sh NEW_OUTPUT_DIRECTORY}
mkdir "$out"
out=$(cd "$out" && pwd)
sdk=/clang64/lib/dotnet
dotnet=$sdk/dotnet.exe
export PATH=/clang64/bin:/usr/bin:$PATH
export DOTNET_ROOT=$(cygpath -m "$sdk") DOTNET_CLI_TELEMETRY_OPTOUT=1
export DOTNET_ROOT_X64=$DOTNET_ROOT MSBuildEnableWorkloadResolver=false
export DOTNET_CLI_HOME=$(cygpath -m "$out/cli-home")
export NUGET_PACKAGES=$(cygpath -m "$out/empty-packages")

commit=cf32fc695b32c2eae848b693395c09222fe1d2ae
curl -fsSL "https://codeload.github.com/PowerShell/MMI/tar.gz/$commit" -o "$out/mmi.tar.gz"
printf '%s  %s\n' acef7ee0e811d3b3c6c0885d34cd4e0f8d28dc4518c1d5550044be1522cdf357 "$out/mmi.tar.gz" | sha256sum -c -
tar -xzf "$out/mmi.tar.gz" -C "$out"

for mode in baseline repaired; do
  mkdir "$out/$mode"
  options=()
  [ "$mode" != repaired ] || options+=(--patched)
  python3 "$here/prepare-mmi.py" "$out/MMI-$commit" "$out/$mode/mmi" "${options[@]}"
  /clang64/bin/clang -std=c11 -shared -Wall -Wextra -Werror "$here/check-extended-callbacks.c" -o "$out/$mode/mmi-callback-driver.dll"
  /clang64/bin/clang -std=c11 -shared -Wall -Wextra -Werror "$here/check-streamed-results.c" -o "$out/$mode/mmi-stream-driver.dll"
  cp "$here/MmiStreamChecks.cs" "$here/MmiExtendedCallbackChecks.cs" "$here/MmiCredentialChecks.cs" "$here/MmiProbe.cs" "$here/MmiPortChecks.cs" "$here/MmiProbe.csproj" "$here/NativeDependencies.cs" "$here/NuGet.config" "$out/$mode/"
  (
    cd "$out/$mode"
    "$dotnet" build MmiProbe.csproj -c Release --configfile NuGet.config -p:UseSharedCompilation=false -v:minimal > build.log 2>&1 || { cat build.log; exit 1; }
    result=0
    timeout 45 "$dotnet" bin/Release/net8.0/MmiProbe.dll > run.log 2>&1 || result=$?
    cat run.log
    if [ "$mode" = baseline ]; then
      [ "$result" = 1 ]
      grep -q 'MI_Session.AssertValidInternalState' run.log
      echo 'Baseline reproduced the upstream unimplemented handle check.'
    else
      [ "$result" = 0 ]
      timeout 90 "$dotnet" bin/Release/net8.0/MmiProbe.dll --port-checks > port-checks.log 2>&1 || { cat port-checks.log; exit 1; }
      cat port-checks.log
      timeout 30 "$dotnet" bin/Release/net8.0/MmiProbe.dll --capabilities > capabilities.log 2>&1 || { cat capabilities.log; exit 1; }
      cat capabilities.log
    fi
  )
done
echo "MMI probe completed; logs: $out/{baseline,repaired}"
