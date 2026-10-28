# .NET 10 source bootstrap

This workflow starts the .NET 10 toolchain needed by PowerShell 7.6.0. The
first component builds the seven native host products from runtime v10.0.5,
commit `081d220c0a773ffb7c6bea6b48727833576a65ef`, using CLANG64 and UASM.
The package recipe is in `../../mingw-w64-dotnet-host10-bootstrap`.

Package `10.0.5-1` passed a clean makepkg build, archive audit, normal pacman
installation and installed native API/ownership checks. All 28 installed
files match the source payload manifest. Evidence is in
`native-package1-results-20261027.json`, `native-package1-results-20261027.txt`
and `native-package1-artifact-20261027.json`.

The source workflow archive is frozen independently of this directory's
later inspection tools. Its exact eleven inputs and hashes are defined by
the recipe's `make-source.py`. The recipe README describes the port and
its explicit bootstrap limitations.

From the recipe directory on the Windows CLANG64 builder:

```sh
makepkg --verifysource
makepkg --cleanbuild --force --noconfirm
```

The build runs original native version and explicit/delay-load nethost
tests. It also checks the actual apphost CET flag, AMD64 architecture,
native imports, six PE versions, Unicode host discovery and the hostfxr
source version/commit callback. No managed runtime is needed for these
native API checks. The component installs under
`/clang64/lib/dotnet10-bootstrap`.

Audit the resulting archive independently on Linux, then validate its
normal pacman installation on Windows:

```sh
python3 tests/runtime10-bootstrap/audit-native-package.py PACKAGE NEW_REPORT.json
bash tests/runtime10-bootstrap/check-installed-host.sh NEW_OUTPUT_DIRECTORY
```

The installed check compares the complete pacman file ownership set and
every payload hash before exercising the installed native APIs. It writes
only its new output directory and uses process-local environment variables.

The existing CoreCLR 8 patch series also has a reproducible mechanical
applicability review against the pinned original 10.0.5 archive:

```sh
python3 tests/runtime10-bootstrap/review-coreclr-patches.py RUNTIME_ARCHIVE NEW_SCRATCH_DIRECTORY
```

This applies patches only in disposable scratch files and reports
rejections; it does not build or validate CoreCLR 10. The current review
is `coreclr-port-review-20261027.json`: 31 rejected hunks and one removed
original assembly source path. Two of the nine CoreCLR patches apply
cleanly after the new host port.

CoreCLR 10, managed runtime and reference assemblies, native interop,
generators, SDK 10.0.201, WindowsDesktop and PowerShell 7.6's dependency
closure and application checks remain open. The native host component
does not complete the full dependency goal or increase its verified
dependency counts.
