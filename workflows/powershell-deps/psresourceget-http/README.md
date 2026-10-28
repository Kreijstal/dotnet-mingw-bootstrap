# Owned PSResourceGet HTTP V3 integration

The fixture serves original `Publish-PSResource` output packages over a temporary
loopback HTTP V3 feed. The installed source module must handle a real401 challenge,
Basic credentials, registration JSON, latest/exact version selection, byte-identical
NuGet downloads, extracted Unicode execution and404 `PackageNotFound` mapping.
Only GUID-owned repository entries are added/removed; existing XML entries are
checked for preservation. The credentials are fixture literals.

After building/installing the recipe, run in MSYS2 CLANG64:

```bash
bash mingw-w64-powershell-psresourceget/check.sh NEW_CHECK \
  /clang64/lib/dotnet/library-packs \
  /clang64/lib/powershell/Modules/Microsoft.PowerShell.PSResourceGet/1.1.1
/usr/bin/python3 tests/powershell-deps/psresourceget-http/check.py \
  /clang64/lib/powershell/Modules/Microsoft.PowerShell.PSResourceGet/1.1.1/Microsoft.PowerShell.PSResourceGet.psd1 \
  NEW_CHECK/lifecycle-2/repository NEW_HTTP_OUTPUT
```

The output directory must be new. Request paths and authentication booleans are
recorded in `requests.json`. The source host must use source Security, Management
and Utility commands. The original release warns that V3 dependency installation
is unsupported; the separate local lifecycle test exercises dependency installation.
This fixture does not establish real remote/cloud authentication or service parity.

The 2026-10-27 installed run passed with19 challenges,18 authenticated requests,
two downloads and one missing-package request. Full evidence is in
[`../psresourceget-results-20261027.txt`](../psresourceget-results-20261027.txt).
