# MMI / CIM / WMI source ports and checks

Latest focused retest (2026-10-27, 22:01 UTC): all seven checks passed on
installed PowerShell 7.4.20-2, System.Management 8.0.0-2 and .NET 8.0.31-3.
This covers actual PowerShell MMI/CIM/WMI in STA and MTA, the MMI3 native suite,
the full WMI suite from both apartments and a combined MMI3/WMI process.
The earlier STA access-denied regression passes. Exact commands, exit codes
and full logs are in
[`installed-retest-results-20261027-2201Z.txt`](installed-retest-results-20261027-2201Z.txt).
Authenticated remote/provider behavior and complete ownership parity remain open.


Latest user-requested retest (2026-10-27, started17:32UTC): all seven
runtime/API checks and five source/package/payload audits passed on installed
source-built.NET8.0.31. All512 MMI3 declarations and27 PowerShell consumer DLL
byte comparisons passed with fresh installed-feed caches. The PowerShell rebuild
reported three existing CA1846 warnings and no errors. Exact commands and logs:
[`installed-retest-results-20261027-1732Z.txt`](installed-retest-results-20261027-1732Z.txt).
Local coverage is established; remote/provider, complete native ownership/MMI3
compatibility and shipping PowerShell integration remain open. That retest used
8.0.31-2. Corrected runtime3 is now installed and passes additional MMI3 native,
WMI, combined-process and original full-console CIM checks; see
[`../runtime-servicing/package3-results-20261027.txt`](../runtime-servicing/package3-results-20261027.txt).

The preceding runtime-servicing regression results are retained in
[`serviced-runtime-retest-results-20261027.txt`](serviced-runtime-retest-results-20261027.txt).

Tested on `win11`, Windows 11 build 26200, on 2026-10-26 with the installed
`mingw-w64-clang-x86_64-dotnet-{sdk,runtime}` 8.0.131-1 packages. The runtime
reports .NET 8.0.0. MMI and the new WMI checks restore offline with fresh caches.

A fresh installed-package retest passed for MMI2, MMI3, all 512 MMI3 contract
declarations, source WMI and both combined CIM/WMI processes. Exact commands,
exit codes and package audits are in
[`focused-retest-results-20261026.txt`](focused-retest-results-20261026.txt).
The latest user-requested run on 2026-10-27 also passed all six commands and
three package audits with fresh installed-feed consumers; see
[`installed-retest-results-20261027.txt`](installed-retest-results-20261027.txt).
The subsequent 02:21 UTC retest also passed the original PowerShell CimCmdlets:
seven runtime/API checks and five source/package/payload audits passed. A
payload-audit invocation initially selected Windows Python; rerunning it with
`/usr/bin/python3` matched all 27 consumer DLLs. Exact commands, the initial
failure and the successful correction are retained in
[`installed-retest-results-20261027-0221Z.txt`](installed-retest-results-20261027-0221Z.txt).
The 04:08 UTC user-requested retest passed all seven runtime/API checks
and five audits on the first run, using explicit MSYS Python for the audits.
See [`installed-retest-results-20261027-0408Z.txt`](installed-retest-results-20261027-0408Z.txt)
for the exact commands, exit codes and logs.
The earlier [`fresh-retest-results-20261026.txt`](fresh-retest-results-20261026.txt)
retains the preceding run.

The original PowerShell7.4.20 core and CimCmdlets now also build and run against
the installed source MMI3/WMI packages. Actual Get-CimInstance, Get-CimClass and
Invoke-CimMethod pass local OS/process queries, class metadata and GetOwner.
Caught native errors, PowerShell CIM serialization, same-runspace WMI/CIM
agreement and GC lifetime checks pass without Framework implementations.
This covers the original697 SMA, nine Eventing and35 CimCmdlets C# inputs,
independently checked against the source archive. See
[`../powershell-core/README.md`](../powershell-core/README.md) and
[`../powershell-deps/powershell-core-results-20261027.txt`](../powershell-deps/powershell-core-results-20261027.txt).
Both source-built SDK analyzer sets are now enabled; this rebuild reports three
existing CA1846 suggestions in the original version generator. The full pwsh
application, remote/provider and complete ownership compatibility remain open;
MMI3 stays experimental.

| Path | Observed result |
| --- | --- |
| Unpatched archived MMI, retargeted to .NET 8 | Builds; session creation throws `NotImplementedException` in `MI_Session.AssertValidInternalState` |
| Seven-patch MMI port | Local DCOM/default OS/process queries, invalid-class errors, disposal/reopen and class metadata pass |
| Native timeouts | Operation/session round-trips, microsecond precision, infinity, negative validation and a timed query pass |
| Native MI_XML | Instance/class round-trips, offsets, Unicode, arrays, intervals, capacity checks, cached classes, malformed input and disposal pass |
| Native async callbacks | Queries, class lookup, read-only method invocation, errors, observer exceptions, compacting GC and independent result lifetimes pass |
| Native cancellation and cleanup | Unsubscription, CancellationToken errors, callback-root release and session close pass |
| Installed experimental MMI package | Fresh consumer restores only from the installed feed; all local/port/capability checks pass |
| Original source-built .NET 8 `System.Management` baseline | Local queries/errors pass but load Framework wminet_utils.dll; historical limitation, superseded by the new backend |
| Source-built WMI backend package | Local queries, metadata/in-memory edits, methods, async callbacks/events, privilege tokens, API/resources and installed-feed checks pass with no Framework implementation |
| Combined installed CIM/MMI + WMI | Both query orders in one process agree on OS build, survive compacting GC and load no Framework implementation |

MMI uses Windows' native `C:\Windows\System32\mi.dll`. The new WMI package
uses locally compiled `msys-wmi-native.dll` against Windows COM/WMI APIs, replacing
Framework's `wminet_utils.dll`. Its 49-entry-point backend, 819 API signatures,
79 resource strings, live local operations and installed NuGet asset selection
pass. The mixed-process test also passes. See
`../powershell-deps/management-results-20261026.txt` and
`combined-source-results-20261026.txt`.

`retest-results-20261026.txt` retains the earlier Framework-dependent baseline
(exit 2) and MMI installed retest. The new source-backend results supersede
that WMI limitation. MMI's required 3.0 compatibility remains open.

MMI source is pinned to
[`cf32fc695b32c2eae848b693395c09222fe1d2ae`](https://github.com/PowerShell/MMI/tree/cf32fc695b32c2eae848b693395c09222fe1d2ae),
SHA-256 `acef7ee0e811d3b3c6c0885d34cd4e0f8d28dc4518c1d5550044be1522cdf357`.
Its project declares **2.0.0-preview.1**. The recipe retains that version and
assembly version 2.0.0.0, public-signed with the upstream identity
`31bf3856ad364e35`. The private source probe is unsigned. PowerShell 7.4.20
requires **3.0.0**; matching the public key token does not establish API,
assembly or application compatibility with that dependency.

A metadata-only comparison with the published 3.0.0 reference and Windows
runtime now confirms 512 matching reference/runtime public entries and assembly
version **1.0.0.0**. The preview2 port has assembly 2.0.0.0; it also differs in
`CimFlags.Adopt` (negative rather than positive 2147483648) and
`Options.ImpersonationType` (uint rather than int). The separate MMI3 candidate below corrects these public contract differences;
unfinished behavior and full application integration remain open.
See `../powershell-deps/mmi-security-published-metadata-20261026.json`; the
inspection tool reads metadata without executing published assemblies.

A separate `mingw-w64-dotnet-mmi3` recipe now produces an experimental NuGet
3.0.0 contract port from the same repaired public source. It matches assembly
1.0.0.0/31bf and all 512 published public/protected declarations, corrects the two
enum differences and makes accidental public native helpers internal. Three
cleanup finalizers are the only allowed API extras. The clean build, installed
native checks, signed friend-name/key access fixtures, installed metadata gate,
MMI3/WMI combined process and artifact audit pass. The newer original SMA/CimCmdlets
integration above adds real PowerShell local-operation coverage. Full application
and vendor3 behavior compatibility remain open, so the direct MMI requirement
stays open. The original preview2 recipe remains available.

    bash mingw-w64-dotnet-mmi3/check.sh NEW_OUTPUT
    bash mingw-w64-dotnet-mmi3/check-api.sh NEW_OUTPUT NEW_API_OUTPUT
    bash tests/mmi-cim-wmi/check-mmi3-combined.sh NEW_COMBINED_OUTPUT
    python3 tests/mmi-cim-wmi/audit-mmi3.py

See `mmi3-results-20261026.txt`, `mmi3-source-api-20261026.json` and
`mmi3-artifact-20261026.json`. The production candidate ships one source-built
MMI assembly calling Windows mi.dll, rather than the vendor's private bridge
layout. Its provenance is explicit in package metadata and informational version.

The seven patches implement:

1. Live native object/function-table validation in three former stubs.
2. Empty callback containers for synchronous operations.
3. Native timeout conversion and MI_XML instance/class serialization,
   deserialization, buffer validation and cleanup.
4. Native result callbacks with persistent unmanaged callback storage, correct
   Boolean ABI, rooted delegates, independent result clones, cancellation and
   operation/session cleanup. Missing English resources are reconstructed from
   upstream designer documentation.
5. Native password/certificate/impersonated credentials for destination and proxy
   options, correct native union/pointer ABI, independent SecureString copying,
   zeroed temporary passwords, destination-options cloning and native error
   propagation. Credential storage and C/native transport behavior pass;
   authenticated remote connections remain unverified.
6. Prompt, error, message and progress callbacks for synchronous and asynchronous
   operations, exactly-once inquiry responses and exception containment. A
   C/mi.h driver verifies Unicode values, error clones, reply errors and compacting
   GC. Both real sync/async local queries accept extended callback handlers.
   The options-copy constructor preserves native timeout and managed settings
   after original disposal; native clone's timeout omission is independently
   reproduced in C.
7. Streamed parameter callback slot 8 and managed CimMethodStreamedResult delivery.
   Scalars and array chunks become individual items, with cloned instance/reference
   values and session metadata. The C/mi.h driver covers all 32 type codes, every
   empty array type, Unicode/null values, source storage poisoned after use, short
   and retained lifetimes, errors/ack failures/cancellation and queued-result
   suppression. It uses a native operation stub; a real read-only EnumKey method
   also accepts streaming-enabled options and returns a final method result.
   Indirect-wrapper disposal is now idempotent, partial instance-array cloning
   cleans up on failure, and cancellation cannot suppress terminal internal errors.

The latest package is `2.0.0.preview1-4`. Its clean build, fresh installed-feed
gate, mixed CIM/WMI test and payload audit pass. See
`stream-results-20261026.txt` and run
`python3 tests/mmi-cim-wmi/audit-mmi.py` from the repository root. These checks
exercise native credential storage, extended callbacks and streamed-result ABI,
including C test drivers. Actual remote authentication and provider-origin extended
message/progress/prompt/stream traffic still require integration coverage.

The tests include a read-only `StdRegProv.EnumKey` call and temporary event
subscriptions that are cancelled; they do not write WMI properties or change
Windows service, firewall or machine configuration. Event payload delivery,
authenticated remote credentials, remote CIM/WSMan, MOF/include-callback deserialization and
provider-origin streamed method results remain unverified. A complete
ownership/API audit and full PowerShell application integration
remain.

Run in MSYS2 CLANG64 with the source-built SDK installed:

```bash
bash tests/mmi-cim-wmi/check-mmi.sh /tmp/mmi-check
```

The output directory must be new. The script downloads and verifies the pinned
source, stages independent baseline/repaired builds, requires the baseline
failure, and requires **every repaired local, port and capability check to exit
0**. Logs remain in `baseline/` and `repaired/`.

Build the experimental package in `mingw-w64-dotnet-mmi`:

```bash
makepkg --cleanbuild --force --noconfirm
```

Install the resulting package with `pacman -U`, then run a consumer outside the
build tree, using only the installed feed and a new cache/output directory:

```bash
bash tests/mmi-cim-wmi/check-installed.sh /tmp/mmi-installed-check
```

The reproducible source probe, clean package build and installed-feed check all
exited **0** under `/home/topkek/mmi-port.iKfuzV`. See
`port-results-20261026.txt` for exact commands and recorded logs, and
`artifacts-20261026.json` for package/source hashes and payload audit results.
`results-20261026.txt` preserves the earlier feasibility results before these
ports; its timeout/serialization/async failures are superseded by the new checks.

Build the source WMI package in `mingw-w64-dotnet-management`, install it, then
use the updated WMI test entrypoint with a new output directory:

```bash
bash tests/mmi-cim-wmi/check-wmi.sh /tmp/wmi-installed-check
bash tests/mmi-cim-wmi/check-combined-source.sh /tmp/cim-wmi-combined
```

The WMI entrypoint runs the installed source package's consumer gate. It checks
metadata, in-memory edits, native GetOwner invocation, enumerator clones,
asynchronous queries/GC/completion, process events for an owned child and
cancellation, temporary privilege tokens, native bounds/error-info ownership,
source versions, resources and absence of Framework implementations. It does
not write persistent WMI objects, registry, accounts, firewall or certificate
stores. Remote/provider/persistent-write and complete native ownership/ABI/race
validation remain open.

The combined test restores exact experimental MMI 2.0.0-preview.1 and WMI 8.0.0
from the installed source-built feed. It queries both in one process before/after
compacting GC, requires matching OS builds and both native modules, and rejects
Framework implementations. It does not establish MMI 3.0 compatibility.

The historical Framework-dependent reproduction remains available explicitly:

```bash
bash tests/mmi-cim-wmi/build-wmi.sh \
  /path/to/dotnet-bootstrap/bootstrap/m612 \
  /path/to/existing/m612 /tmp/wmi-baseline/wmi-build-root
bash tests/mmi-cim-wmi/check-wmi.sh --framework-baseline /tmp/wmi-baseline
```

That baseline uses the original runtime 8.0.0 sources and intentionally reports
exit 2 when Framework's helper is loaded. Its native junctions should be removed
with Windows `rmdir` without `/s` before deleting an output tree. It is retained
as a comparison, while the default WMI entrypoint verifies the new source port.
