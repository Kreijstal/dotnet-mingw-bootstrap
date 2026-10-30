# Original full PowerShell SDK and console

The native `mingw-w64-powershell` package now builds and installs as
PowerShell7.4.20-2. Its826 files include596 application files and208 licenses,
with14 original project DLLs,163 references and all six gallery modules.
The clean build, original source/API/catalog/apphost audits, actual package
archive audit, normal pacman installation and installed-entry checks pass.
Release2 is a full clean rebuild with source System.Management8.0.0-2 and
both STA/MTA integration gates. See
[`native-package2-results-20261027.txt`](native-package2-results-20261027.txt).
Release1's checker-only repackage and initial STA failure remain historical
evidence in `native-package-results-20261027.txt`.

Reproduce the frozen source workflow with
`python3 mingw-w64-powershell/make-source.py` from the repository checkout,
then run `makepkg --cleanbuild --force --noconfirm` in that recipe directory.
The installed check is:

```sh
bash $MINGW_PREFIX/share/doc/powershell/checks/check-package.sh \
  /clang64 NEW_OUTPUT $MINGW_PREFIX/share/doc/powershell/checks
```

The direct MMI/local CIM/System.Management WMI fixture now passes with both
`-STA` and `-MTA`. The standalone `WmiStaProbe.ps1` regression also passes.
The source backend secures the actual service proxy after CLR marshaling, in
the same native call that invokes the operation. The initial MMI fixture needed a
CLR null string and explicit native query-enumerator disposal. Two isolated
STA experiments did not resolve the failure and are not shipped. Release2
supersedes the original failure recorded in
[`cim-wmi-runtime-results-20261027.txt`](cim-wmi-runtime-results-20261027.txt).
The separate `tests/mmi-cim-wmi/check-source-wmi-sta.sh` runner also passes the
full source WMI metadata/method/async/event/privilege/native suite from STA,
with exact installed managed/native asset-byte verification.

This harness builds the original PowerShell7.4.20 SDK catalog, Windows console
project and separate LocalAccounts project from the pinned source archive and
installed source-built dependency feed. It retains the original project graph,
source generators, explicit analyzers, four SDK CodeStyle analyzers and both SDK
.NET analyzers. The original console entry point runs the integration fixtures.

LocalAccounts retains all26 original C# files. Its22 explicitly adapted files
contain formatting/accessibility/private-static changes and17 obsolete legacy
serialization-constructor annotations. The owned Roslyn check compares original
source declarations/attributes, adapted declarations and actual emitted DLL
metadata, including implicit constructors, private property accessors, original
public key and version. All459 public/protected entries match. It never emits
or executes an unpatched baseline assembly.

Run from the harness checkout in MSYS2 CLANG64 after placing the verified archive
and pristine `PowerShell-7.4.20` tree in a new build root:

```bash
/usr/bin/python3 tests/powershell-full-sdk/prepare.py "$root/PowerShell-7.4.20"
bash tests/powershell-full-sdk/build.sh "$root"
bash tests/powershell-full-sdk/check-localaccounts-api.sh "$root" "$root/api-check"
/usr/bin/python3 tests/powershell-full-sdk/audit-source.py "$root"
/usr/bin/python3 tests/powershell-full-sdk/audit-catalog.py "$root"
/usr/bin/python3 tests/powershell-full-sdk/stage.py "$root" "$root/app"
/usr/bin/python3 tests/powershell-full-sdk/audit-payload.py "$root" "$root/app"
```

Use the original console with its child-process execution-policy argument to
allow the owned fixture scripts:

```bash
"$root/app/pwsh.exe" \
  -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass \
  -File "$(cygpath -am tests/powershell-full-sdk/Smoke.ps1)"
"$root/app/pwsh.exe" \
  -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass \
  -File "$(cygpath -am tests/powershell-full-sdk/GallerySmoke.ps1)" \
  -Output "$(cygpath -am "$root/gallery-output")"
```

The early muxer candidate had604 staged files,383 binary assets,13 original
project DLLs,163 source reference assemblies and all six source gallery modules.
The213-reference original SDK catalog independently regenerates exactly. Tests
cover local CIM/GetOwner/serialization, JSON, Add-Type, Authenticode, read-only
LocalAccounts enumeration, gallery module discovery, source NuGet provider,
PSReadLine options, Unicode archive round trips and ThreadJob/CIM execution on
installed runtime8.0.31-3. See
[`integration-results-20261027.txt`](integration-results-20261027.txt) for exact
commands, passing gates and retained initial failures.

A subsequent prototype native-console candidate had605 files/384 binaries. Its original
project `pwsh.exe` exactly matches an independent unchanged source SDK task replay
and launches a real native Start-Job child with Unicode CLIXML and live CIM.
See [`native-console-results-20261027.txt`](native-console-results-20261027.txt).
The real source SDK host/runtime and targeting replacements are installed, and
the fresh original build and integration gates against them pass. The later
conditional WindowsDesktop/GraphicalHost and native package/license gates also
pass, as documented above. Remote/provider and broader experimental dependency
compatibility remain open. PowerShell7.6/.NET10 remains a separate required graph.
These integration milestones do not increase the verified dependency counts or
establish completion of the full packaging goal.


The latest fresh build uses installed source SDK8.0.131-3, targeting8.0.31-2 and
runtime8.0.31-3. The original full core pipeline and all nine CLI gates pass.
Run `bash tests/powershell-full-sdk/build-desktop.sh "$root"` after the core
build to include the original conditional GraphicalHost. Its source desktop SDK
activation uses `ImportWindowsDesktopTargets=true`; the original top-level
project otherwise imports the core SDK while declaring a transitive WPF edge.
The upstream GraphicalHost `RunAnalyzers=false` setting remains unchanged.

The desktop variant has596 files/372 source binary assets and14 original project
DLLs. Native console/CIM/LocalAccounts, real child jobs and all six gallery
modules pass. A real STA render of the original compiled Show-Command control
produces133905 visible pixels. `audit-graphical-source.py` separately verifies all
165 original C# inputs,16 original XAML/compiled BAML pages and12 byte-exact
original graphics resources without loading the target assembly in its metadata
inspector. See [`serviced-build-results-20261027.txt`](serviced-build-results-20261027.txt).
The desktop framework remains the experimental WPF8/interim WinForms7 source
port; these gates do not claim complete native desktop compatibility. Final
PowerShell native-package/license closure now passes; broader compatibility
and.NET10 remain open.
