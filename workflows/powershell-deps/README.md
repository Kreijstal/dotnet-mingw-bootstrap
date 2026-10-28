# PowerShell dependency packaging

The active objective is to package or port all PowerShell dependencies from
source. The first dependency graph is PowerShell **7.4.20**, which targets the
source .NET8.0.31 runtime and serviced SDK8.0.131. Completion remains unproven until the full dependency
graph, native components, required ports and PowerShell integration pass.

`requirements-7.4.20.json` records the actual declarations from the pinned
PowerShell source archive: **53 direct package IDs** across the Windows shipping
graph, conditional GraphicalHost, LocalAccounts, generators, bundled modules and
analyzers. It includes versions, referring projects and the source archive hash.
This records direct requirements, rather than pretending to enumerate every
transitive dependency. `progress.json` tracks verified packages and remaining
work, including transitive packages as they are discovered.

PSResourceGet1.1.1 is packaged from complete pinned source. Current totals are **50/53 direct IDs**,52
verified transitive IDs and106 dependency artifacts including four under
experimental dependency coverage: MMI2, MMI3, the source SqlClient port and the
Windows.Compatibility metapackage whose SqlClient dependency remains experimental. The separate source module test host is a build tool and does not
increase those dependency counts.

The original full SDK and Windows console now build against the source feed.
LocalAccounts also builds with all analyzers enabled after an explicit22-file
formatting/private-static/legacy-serialization annotation adaptation; all26
original inputs remain. The full source inventory and213-reference catalog
regeneration pass. Actual console execution on installed runtime8.0.31-3 passes
CIM, JSON, Add-Type, Authenticode and read-only LocalAccounts enumeration. All six
source gallery modules import; source NuGet discovery, PSReadLine options,
Archive Unicode round trips and ThreadJob/CIM execution pass. An independent
payload audit traces596 staged application files,372 binary assets and14 original project
DLLs to source outputs and installed source packages. The original native `pwsh.exe` now also matches an independent source SDK task
replay and passes actual Start-Job child/CIM/Unicode checks. Conditional desktop
integration and native package/license closure also pass; broader compatibility
remains open. See `powershell_full_sdk_validation` in `progress.json`.

System.Management8.0.0-2 fixes STA WMI proxy security after CLR marshaling. Its
clean package,819-signature API audit,65-export native audit and installed
consumer checks pass. The regression shares scopes between STA/MTA threads,
queries metadata/GetOwner and checks temporary thread-token restoration.
PowerShell7.4.20-2 has been clean-built and installed with this source dependency;
its MMI/CIM/WMI checks pass in both apartments through the installed launcher.
See `management-apartment-results-20261027.txt` and
`../powershell-full-sdk/native-package2-results-20261027.txt`.
The full WMI metadata/method/async/event/privilege/native suite also passes from
an STA entry point. Reproduce it with
`bash tests/mmi-cim-wmi/check-source-wmi-sta.sh NEW_OUTPUT`.

`requirements-7.6.0.json` separately records the pinned official7.6.0 source:
SDK10.0.201,net10.0,C#13 and79 declared IDs. This includes forced native
System.IO.Ports references for other platforms. Eleven IDs keep their7.4
versions;39 are new direct declarations, including some previously transitive
dependencies. This inventory does not verify restored transitives, source.NET10
runtime/SDK builds or7.6 runtime compatibility. Reproduce it with
`python3 tests/powershell-deps/inventory-powershell76.py SOURCE_ARCHIVE OUTPUT_JSON`.

Microsoft.Windows.Compatibility8.0.31 is now built, independently audited and
installed as a source metapackage. All five original framework groups and191
dependency entries match the source project and published metadata; no library
payload exists upstream. Fresh installed-feed checks cover all35 net8 edges,
40 source archive owners and69 consumer DLLs, plus local WMI, owned bitmap/cache,
codepages/CodeDom, SQL metadata and WCF/Syndication without Framework fallback.
This completes its source metadata artifact while full SqlClient/native graph
compatibility remains open. See
[`windows-compatibility-metapackage-results-20261027.txt`](windows-compatibility-metapackage-results-20261027.txt).

Corrected source runtime8.0.31-3 is now clean-built, independently audited and
installed beside8.0.0. All168 managed assemblies and176 installed payload byte
comparisons pass, as do178 actual PE version checks at8.0.3126.42000. The original
Arcade zero-version policy and20 stub signing keys now pass on all44 reference/
runtime shim DLLs. All six source NuGet facade-forwarding regressions pass,
including the lower-version SqlClient target that failed in the historical2
candidate. Fresh default SDK/PowerShell, JSON package binding, MMI3 native
callbacks/lifetimes, WMI and combined-process checks pass on installed3. See
[`../runtime-servicing/package3-results-20261027.txt`](../runtime-servicing/package3-results-20261027.txt).

The serviced targeting tools build all eight original interop/COM/Regex/JS/JSON
projects with no production C# overlays. All99 signed main/satellite DLLs pass
compiler/source and neutral/XLF resource audits. A595-DLL staged pack includes
163 source references and byte-verified source dependency/license closure. The
unchanged upstream Arcade task generates its complete371-entry platform manifest
(342 unique names), including private/native compatibility metadata. Normal source
SDK selection and all JSON variants/generated COM/PInvoke/Regex/JS cases pass on
the complete normalized8.0.31 runtime. The corrected `dotnet-targeting8-servicing`2 now passes a normal clean build,
independent595-DLL/license/metadata artifact audit and fresh installed595-file/
163-reference/six-generator checks. Its provides/conflicts metadata replaces the
old package owner in a normal transaction. See
[`../runtime-servicing/targeting-package2-results-20261027.txt`](../runtime-servicing/targeting-package2-results-20261027.txt). Its generators remain an explicit source
.NET8/Roslyn4.9 host port. Historical/netstandard hosts, browser execution and broader native coverage
remain open. This does not increase dependency completion counts. See
[`../runtime-servicing/targeting-platform-manifest-20261027.json`](../runtime-servicing/targeting-platform-manifest-20261027.json)
and [`../../mingw-w64-dotnet-targeting8-servicing/README.md`](../../mingw-w64-dotnet-targeting8-servicing/README.md).

The replacement `dotnet-sdk8-servicing`8.0.131-3 is also built, independently
audited and installed. It preserves the audited source SDK component, freshly
builds the real8.0.31 apphost/comhost/nethost pack and adds175 source runtime-pack
assets from installed runtime3. The unchanged upstream Arcade task generates
its actual runtime manifest. One exact SDK latest-framework metadata correction
makes ordinary self-contained publishing select8.0.31. Both normal native
apphost and self-contained consumers pass all168 framework/native checks and six
facade regressions; all175 published bytes match source inputs. See
[`../sdk8-servicing/native-package1-results-20261027.txt`](../sdk8-servicing/native-package1-results-20261027.txt).
A fresh original PowerShell full SDK/native console/LocalAccounts build and all
nine CLI source/API/catalog/native/module/child gates pass against these installed
packages. The conditional GraphicalHost/desktop console also builds and passes
CIM, child jobs, all six gallery modules and source WPF raster output. All165
original GUI C# inputs,16 compiled BAML pages and12 original graphics resources
pass an independent audit. The full CLI has601 files/380 binaries; the desktop
variant has596 files/372 binaries and14 original project DLLs, with additional
components supplied by the declared source desktop framework. See
[`../powershell-full-sdk/serviced-build-results-20261027.txt`](../powershell-full-sdk/serviced-build-results-20261027.txt).
Final PowerShell native packaging/license closure, broader experimental desktop/
native/remote compatibility and the.NET10 graph remain incomplete.

PSResourceGet retains all56 original C# inputs, with55 unchanged and one exact
NuGet SemanticVersion constructor qualification, all six original module assets,
original release identities and all460 APIs. The native/PSModule NuGet package
ships26 private source dependencies, including the Windows RID PKCS asset, plus
their original licenses, reference/XML docs and compiler/overlay provenance.
Clean build, fresh restored/relocated/installed checks and an independent audit
against every private library's source-built native package pass. Original local
repository publish/find/dependency save/install/update/uninstall and Unicode
execution pass with owned entries cleaned and pre-existing settings preserved.
An owned HTTP V3 server verifies actual401 Basic authentication, JSON version
discovery, exact archive download/extraction and404 mapping. The upstream V3
protocol does not install dependencies; the original warnings remain, while
local-repository dependency installation passes. Source build:243 existing
warnings,0 errors. See
[`psresourceget-results-20261027.txt`](psresourceget-results-20261027.txt) and
[`psresourceget-artifact-20261027.json`](psresourceget-artifact-20261027.json).
Direct IDs still short of full compatibility verification are MMI3, SqlClient
and Windows.Compatibility, although all now have source package artifacts. Remote/
cloud authentication, broader runtime servicing and complete shipping PowerShell/SDK
integration remain open; these counts do not establish overall completion.

The experimental SqlClient4.9.1 native/NuGet package is now built and installed.
It retains all242 Windows and three generic-facade source inputs, with four
explicit production overlays,407 resource strings and metadata XML. All1020
contract APIs, actual ref/generic/Windows asset selections, fresh installed
transport checks, independent source/artifact audits and source PowerShell
scalar/reader/schema/DONE checks pass. A new MC-SMP regression reproduces the
old sender stalling after sequence3 with WNDW4; an exact three-comparison fix
then passes25 query DATA frames across six stopped/resumed windows, ten response
frames and receive-credit ACKs. This is an additional experimental artifact;
SqlClient's direct requirement stays open for native/database interoperability,
LocalDB/shared-memory/FILESTREAM and broader behavior parity. See
[`sqlclient-package-results-20261027.txt`](sqlclient-package-results-20261027.txt)
and [`sqlclient-package-artifact-20261027.json`](sqlclient-package-artifact-20261027.json).

ThreadJob retains all three original C# files/resource and frozen assembly2.0.1.0
identity, with one current-engine debugger overload adapter. All55 published API
declarations match; all30 upstream non-policy cases and a real child breakpoint
fixture pass in clean build/fresh installed-feed checks. NuGet/loose module bytes
match. Only two child-process launchers use the source host; their original script
bodies remain. The separate7 machine-policy tests remain unverified, with original
production policy code retained. See
[`threadjob-results-20261027.txt`](threadjob-results-20261027.txt) and
[`threadjob-artifact-20261027.json`](threadjob-artifact-20261027.json).
The host7.4.20-3 includes original built-in module manifests under the normal
Modules directories; relocated/fresh child runspaces autoload all four source
modules and pass a live local CIM query. Its private native pwsh.exe uses the
unchanged original console entry source and source SDK apphost. Original
Start-Job now launches real child processes, including PowerShellGet uninstall's
dependency scan. Relocated and installed checks verify child Unicode CLIXML,
source engine/ConsoleHost paths and live CIM. All ten production DLLs remain
byte-identical to pkgrel2. See
[`module-host-console-entry-results-20261027.txt`](module-host-console-entry-results-20261027.txt).

Archive's complete original suite runs against fresh-cache build/installed NuGet
consumers in en-GB and en-US: 98 cases pass per run, with no failures/skips and one
original upstream pending case. All31 reserved-device cases execute after a
missing test-data comma is restored. A fixture uses an owned TestDrive root;
compression/extraction algorithms remain unchanged. One missing fallback warning
string is added to the module's inline data. See
[`module-packages-results-20261027.txt`](module-packages-results-20261027.txt) and
[`module-packages-artifacts-20261027.json`](module-packages-artifacts-20261027.json).

The source test tool builds all 1001 original Windows production C# inputs in ten
PowerShell projects, three original source generators, source Pester 4/Gherkin,
original module manifests and 163 source Add-Type reference assemblies, plus the
unchanged original console entry. All 292
DLL assets pass byte/provenance checks. It uses both SDK analyzer sets and the
original explicit source analyzers. It supports relocated/installed module tests;
the shipping pwsh application and complete SDK catalog remain separate. MMI3
compatibility and runtime 8.0.0 servicing remain open.

PSReadLine's transitive Microsoft.PowerShell.Pager1.0.0 is also packaged and
installed from both unchanged original C# files. All3 public API declarations
and all4 original console-model test bodies pass build/fresh installed checks.
See [`pager-results-20261027.txt`](pager-results-20261027.txt) and
[`pager-artifact-20261027.json`](pager-artifact-20261027.json). PSReadLine itself
is now packaged and installed as described below.

PSReadLine2.3.6 retains48 unchanged main C# files, the original Core forwarder
source,245 resource strings and all9 Core module assets. All457 API declarations,
original identity/informational commits, five source-engine forwarders and bundled
Pager bytes pass build/fresh installed checks. Nine owned editing cases and a
fresh-process owned history save/reopen pass with unchanged upstream MockConsole.
Complete upstream xUnit, broader completion/prediction/help and actual terminal/
ConsoleHost integration remain unverified. See
[`psreadline-results-20261027.txt`](psreadline-results-20261027.txt) and
[`psreadline-artifact-20261027.json`](psreadline-artifact-20261027.json).

PackageManagement1.4.8.1 now packages all six Core source assemblies:277 original
compiler inputs, five original resource classes and295 strings. All2205 public APIs
and original signing/identity/Debug flavor match. Three source qualifiers resolve
the inbox async-enumerable collision; generator changes affect only root paths.
All21 Core module/DSC files and licenses are retained. Owned offline source/package
find, dependency save/install, Unicode extraction, inventory and uninstall pass in
clean/fresh installed checks. Remote/authentication/full DSC and shipping pwsh
coverage remain open. See
[`packagemanagement-results-20261027.txt`](packagemanagement-results-20261027.txt) and
[`packagemanagement-artifact-20261027.json`](packagemanagement-artifact-20261027.json).

PowerShellGet2.2.5 uses the unchanged original module assembler and all166 pinned
module/DSC source inputs. All16 runtime module files match published release
contents after signature/encoding/line-ending normalization; manifest leading
indentation is also normalized for comparison. No production algorithm changes
or vendor implementation binaries are used. Clean build and fresh installed-feed
checks pass repository persistence, version discovery, dependency save/install,
Unicode execution, module/script metadata and update/uninstall operations. Both
NuGet and installed loose-module files pass byte checks. The original local
-AllVersions stable-filter quirk is retained and explicitly checked. Core
Update-Script's deliberate move to CurrentUser is checked with initially absent
fixture filenames and verified cleanup. Remote/authenticated repositories,
network publishing/bootstrap, broad DSC and full application integration remain
open. See [`powershellget-results-20261027.txt`](powershellget-results-20261027.txt)
and [`powershellget-artifact-20261027.json`](powershellget-artifact-20261027.json).

PSResourceGet1.1.1's complete source/dependency map now records56 original C#
files, NuGet6.9.1, Azure.Identity1.11.4 and the published assembly identities.
Azure.Core1.38.0 also references System.ClientModel1.0.0, which is absent from
the published module bundle and must be included in the full source closure.
See [`psresourceget-requirements-1.1.1.json`](psresourceget-requirements-1.1.1.json).
Exact package hashes, nuspec source commits and dependency groups for eight
authentication/JSON libraries are recorded in
[`psresourceget-source-closure-1.1.1.json`](psresourceget-source-closure-1.1.1.json).
System.Text.Json8.0.5 explicitly names runtime commit81cabf2857a01351e5ab578947c7403a5b128ad1;
its pinned source project confirms ServicingVersion5. System.Memory.Data1.0.2
comes from the Azure SDK repository. JSON, IdentityModel.Abstractions, Memory.Data and
ClientModel, Azure.Core, MSAL/Extensions and Azure.Identity are packaged below.
The complete source module now passes the installed local/owned-HTTP integration
and private-closure audits above; real remote/cloud service matrices remain open.

Its first three missing transitive libraries are now packaged and installed:
Microsoft.Extensions.Primitives, FileProviders.Abstractions and FileSystemGlobbing
6.0.0. All56 actual original C# compile inputs,382 public/protected API declarations,
six resource manifests and20 strings are retained. Both source implementation
and reference assemblies are shipped. Clean/fresh installed checks cover real
change callbacks/resubscription/disposal, Unicode values, file-provider contracts
and actual filesystem/in-memory globbing. See
[`extensions6-results-20261027.txt`](extensions6-results-20261027.txt) and
[`extensions6-artifacts-20261027.json`](extensions6-artifacts-20261027.json).

NuGet.Client6.9.1 now packages and installs all eleven complete original Core
projects without production C# patches. The actual compiler manifests prove
all1288 original C# inputs plus22 SDK metadata files; original resource/schema
inputs and sibling source reference bytes are audited. All7845 published Core
APIs, signing/file/informational identities,13 resource manifests and807 strings
match. Source implementation/reference assemblies, compiler XML documentation,
Apache license/notices and exact NuGet/native dependency graphs are shipped.
Clean build and fresh installed-feed checks pass original RestoreRunner dependency
resolution, local protocol, owned HTTP V3 discovery/download, actual401 credential
authentication,404 handling, author signing/CMS verification and tampered-package
rejection. Consumer runtime/reference bytes match the source inventories. The
source build retains82 original XML-comment/CLS warnings; all consumers build
without warnings or errors. The Azure/MSAL/ClientModel/JSON source closure is now
packaged and included in the module milestone above. Remote service/authentication/
signing matrices and separate CLI/build-task/VS projects remain open. See
[`nuget-client-results-20261027.txt`](nuget-client-results-20261027.txt) and
[`nuget-client-artifacts-20261027.json`](nuget-client-artifacts-20261027.json).

System.Text.Json8.0.5 now packages and installs the complete selected .NET8
library/reference/XML docs and all42 generator/satellite assets across the three
original generator variants. All497 original compiler inputs, four generated SR
bindings, eight SDK metadata inputs,1784 API declarations,43 resource sets and1460
strings pass. Production C# is unchanged. The original reflection/resource-key
linker substitutions are embedded and compared structurally. The selected net8
assembly is8.0.0.0/file8.0.1024.46610; this differs from the original module's
net462 bundle and is verified by runtime location/hash to prevent inbox fallback.
All three source generators compile/execute serializers with reflection disabled,
reject invalid nonpartial contexts and load German diagnostic resources. A real
SDK PackageReference sample verifies exact analyzer selection and generated
execution. Generator builds use the source.NET8/Roslyn4.9.2 host explicitly;
older compiler/runtime compatibility is not claimed. Clean build, installed fresh
cache, byte comparison and independent artifact audit pass. Source build retains
ten original XML/nullable warnings; consumers have none. Broad upstream/linker/AOT
matrices and full PSResourceGet/Azure integration remain open. See
[`system-text-json8-results-20261027.txt`](system-text-json8-results-20261027.txt) and
[`system-text-json8-artifact-20261027.json`](system-text-json8-artifact-20261027.json).

Microsoft.IdentityModel.Abstractions6.35.0 now packages and installs the complete
original project/common imports and nine compiler inputs. Eight implementation
files are unchanged; only three version attributes in AssemblyInfo.cs reproduce
the original frozen CI release metadata. All52 published API declarations and
release/signing identities match. Six original telemetry assertion cases retain
their derived helpers, adapting only Moq construction to a concrete subclass.
Additional checks cover the full telemetry interface, live/null/Unicode data,
logger/telemetry singleton contracts and constants. Source implementation/ref/XML
bytes, exact CI overlay, original fixture BOM, native/NuGet metadata and license
pass independent audit. Clean build and fresh installed-feed checks have zero
warnings/errors. Broader framework/trimming and MSAL/Azure/PSResourceGet integration
remain open. See
[`identitymodel-abstractions-results-20261027.txt`](identitymodel-abstractions-results-20261027.txt)
and [`identitymodel-abstractions-artifact-20261027.json`](identitymodel-abstractions-artifact-20261027.json).

System.Memory.Data1.0.2 now packages and installs its complete original project.
Its four-file src tree matches both pinned Git blobs and the full Azure SDK
archive. Both implementation C# files are unchanged; actual compiler inputs and
all20 published APIs/assembly identities pass. Owned Unicode/borrow/copy,
read-only stream seek/EOF/disposal/cancellation, seekable/nonseekable short-read
ownership and source JSON interoperability checks pass. The native/NuGet package
ships source implementation/ref/XML, original MIT license and compiler inventory.
Clean build, fresh installed-feed checks, independent artifact audit and built/
installed/restored NuGet byte equality pass. Source build retains three nullable
warnings; consumers have none. Broader upstream/framework/AOT and ClientModel/
Azure/PSResourceGet integration remain open. See
[`memory-data-results-20261027.txt`](memory-data-results-20261027.txt) and
[`memory-data-artifact-20261027.json`](memory-data-artifact-20261027.json).
System.ClientModel1.0.0 now packages and installs all eleven unchanged original
C# files from its complete pinned twelve-file production tree. Clean build,
fresh installed checks and independent artifact audit pass without warnings or
errors. All21 published API declarations and release/signing identities match.
Generic/object persistence, large pooled-buffer JSON lifetime, private constructor/
proxy activation, wire/custom formats and negative cases pass. All three loaded
ClientModel/BinaryData/JSON hashes and actual compiler/runtime asset selection are
gated. Original license, implementation/ref/XML and source inventory are shipped;
built/installed/restored NuGet bytes match. Azure/MSAL/PSResourceGet integration
remains open. See [`clientmodel-results-20261027.txt`](clientmodel-results-20261027.txt)
and [`clientmodel-artifact-20261027.json`](clientmodel-artifact-20261027.json).

Azure.Core1.38.0 now packages and installs all193 unchanged C# inputs, retaining
the original32-file Shared selection and complete247-file production tree.
All914published APIs and frozen release/signing identities match. The selected
source JSON8.0.5 generator4.4 hash and all12generated context files are audited;
those generated sources are also shipped as build evidence. Fresh installed-feed
checks execute real sync/async HTTP against an owned loopback server,503 retry,
UTF8 request ownership,400 generated error parsing with JSON reflection disabled
and cancellation. Core/ClientModel/BinaryData/JSON loaded hashes, runtime/ref/XML,
licenses, exact dependencies and built/installed/restored NuGet bytes pass.
An initial metadata-helper reflection-setting error was fixed in the test harness;
the final clean build and installed consumers pass, with three original source
serialization-obsolescence warnings and no consumer/tool warnings. Broader HTTP/
authentication and Azure.Identity/MSAL/PSResourceGet integration remain open. See
[`azure-core-results-20261027.txt`](azure-core-results-20261027.txt) and
[`azure-core-artifact-20261027.json`](azure-core-artifact-20261027.json).

MSAL4.61.3 and its cache extension now package and install the original NET6
platform/feature branch on.NET8:421+28 unchanged C# inputs,4SDK metadata files
and45source-generated JSON context files. All1068+69 published APIs, frozen
versions, original repository/trimming/serviceable metadata, full strong names
and exact embedded directives match. Original projects/globs/feature imports and
central package management remain; only build tooling, target/version inputs and
XML directive line endings are adjusted. Fresh installed checks pass real owned
TLS/OIDC, Unicode secrets, independently verified RSA-PSS client assertions and
tamper rejection, cache/force-refresh, DPAPI encrypted persistence/reopen, OAuth
errors/cancellation and reflection-disabled source contexts. All5loaded source
hashes, actual runtime/ref assets, native/NuGet dependencies, licenses/notices and
built/installed/restored NuGet equality pass independent audits. One original
cache exception serialization warning remains; consumers/tools have none.
Broader broker/interactive/remote/cross-process and full Azure integration remain
open. See [`msal-results-20261027.txt`](msal-results-20261027.txt) and
[`msal-artifacts-20261027.json`](msal-artifacts-20261027.json).

Azure.Identity1.11.4 now packages and installs all132 source inputs from its
complete118-file project and15original shared files.131C# files are unchanged;
one default-initializer overlay fixes NET8 definite assignment in the original
authorization-code path. Both the initial exchange and the CAE silent-cache-miss
fallback execute against an owned real TLS/OIDC endpoint. Secret/certificate
acquisition, independently verified RSA-PSS assertions, sync/async cache hits,
DefaultAzureCredential environment selection, error wrapping/cancellation and
reflection-disabled contexts pass. All9source runtime hashes,356published APIs,
release identities, compiler/ref/runtime/XML/license/overlay and exact dependencies
pass clean-build/fresh installed/independent artifact gates. One original source
obsolete-serialization warning remains; consumer/tools have none. All eight mapped
authentication/JSON libraries are packaged; full PSResourceGet module integration
and broader credential/remote/framework matrices remain open. See
[`azure-identity-results-20261027.txt`](azure-identity-results-20261027.txt) and
[`azure-identity-artifact-20261027.json`](azure-identity-artifact-20261027.json).

PSResourceGet1.1.1 now compiles all56 inputs against the installed complete source
engine and dependency graph.55files remain unchanged; a single constructor type is
qualified as NuGet SemanticVersion to resolve modern SMA's additional type. All460
published APIs and release identities match using the correct Framework metadata
baseline (metadata only). Complete original module assets and25private source DLLs
stage successfully. The original initializer imports all17cmdlets/nested script;
new/get/test/update script metadata and private NuGet loaded hashes pass. The
original ACR version helper executes in a real source test cmdlet and passes numeric
ordering, stable/prerelease ranges, exact matching and invalid-version errors.
This is uncounted: repository/package lifecycle/protocol checks, formal packaging,
independent artifact audit and fresh installed/relocated integration remain. The
build retains243original documentation/nullability/value-comparison warnings. See
[`psresourceget-prototype-results-20261027.txt`](psresourceget-prototype-results-20261027.txt).

Regenerate the declaration inventory with:

```bash
python tests/powershell-deps/inventory.py /path/to/PowerShell-7.4.20 \
  --version 7.4.20 --archive /path/to/powershell-7.4.20.tar.gz
```

The first six CLANG64 recipes, in dependency order:

| Package suffix | NuGet ID | Version | Package check |
| --- | --- | --- | --- |
| `dotnet-markdig` | Markdig.Signed | 0.33.0 | HTML, escaping, tables, AST and upstream signing identity |
| `dotnet-powershell-markdownrender` | Microsoft.PowerShell.MarkdownRender | 7.2.1 | HTML, terminal escapes, AST and upstream signing identity |
| `dotnet-humanizer` | Humanizer.Core | 2.14.1 | Case conversion, inflection, words, resources and signing |
| `dotnet-json-more` | Json.More.Net | 2.0.2 | Object/numeric equivalence, hashing, negatives and node round-trip |
| `dotnet-jsonpointer` | JsonPointer.Net | 5.0.2 | Pointer escaping, arrays, missing/invalid paths, Humanizer integration and JSON round-trip |
| `dotnet-jsonschema` | JsonSchema.Net | 7.0.4 | Draft 2020-12 validation, local references, types, ranges, arrays, required and additional properties |

Run `makepkg --cleanbuild --force --noconfirm` in each recipe directory in a
CLANG64 shell and install the resulting package before building its dependents.
Each recipe builds the upstream project from checksum-pinned source, deletes
upstream binary build inputs, restores from the installed source-built feed and
runs a consumer against the newly built NuGet package. Runtime libraries remain
separate packages. SourceLink/versioning tooling and pre-.NET-8 polyfill
generators are removed where their functionality is supplied directly by the
recipe or the .NET 8 reference pack.

Markdig's upstream C# 10 setting needs C# 12 when targeting .NET 8's ref-readonly
APIs. MarkdownRender uses the same Markdig 0.33.0 selected by PowerShell and
declares it as a runtime dependency. The JSON projects replace sibling project
references with exact package references to the separately built dependencies.

The Json.More check exposed a bug in its upstream equivalence hash: decimal
numbers `1` and `1.0` compare equal but are hashed from different raw JSON text.
Its packaging patch hashes decimal-representable numbers by decimal value and
retains raw-text hashing for numbers outside that range. The regression check
requires equal hashes for equivalent documents with reordered properties and
different numeric spellings.

After installing all six packages, run the combined check from outside their
build trees:

```bash
bash tests/powershell-deps/check-installed.sh /tmp/powershell-installed-check
```

The output directory must be new. This restores with an empty package cache and
only `/clang64/lib/dotnet/library-packs` as its source, loads every type in all six
assemblies, and exercises JSON-pointer extraction through MarkdownRender as
well as schema validation and numeric equivalence. It verifies this library
subset; it does not establish that PowerShell itself builds or runs.

A seventh recipe, `mingw-w64-dotnet-mmi`, packages an experimental port of the
archived public MMI **2.0.0-preview.1** sources. Its package and installed-feed
checks pass for local CIM, native timeouts, XML serialization, async queries,
class lookup, read-only method invocation, cancellation and callback cleanup.
It uses the upstream public-signing identity and Windows' native MI API. See
`../mmi-cim-wmi/README.md` for commands, evidence and remaining scope. PowerShell
requires MMI **3.0.0**; the tracker leaves that requirement open until API,
assembly and application compatibility are established.

Audit the seven built package payloads, recipe-local checksums, licenses and
NuGet dependency metadata from the repository root:

```bash
python3 tests/powershell-deps/audit-packages.py
```

`artifacts-20261026.json` records the seven artifact hashes. MMI's source and
version details are also recorded in `../mmi-cim-wmi/artifacts-20261026.json`.

`mingw-w64-dotnet-windows-libraries` adds five split packages built from the
actual runtime **v8.0.31** source archive:

| NuGet ID | NuGet version | Native/package check |
| --- | --- | --- |
| Microsoft.Win32.Registry.AccessControl | 8.0.0 | Read-only native registry owner/DACL |
| System.Security.Cryptography.ProtectedData | 8.0.0 | DPAPI user/machine encryption, entropy and tampering |
| System.IO.Packaging | 8.0.1 | Compressed OPC ZIP parts, Unicode, properties and relationships |
| System.DirectoryServices.Protocols | 8.0.2 | Native Wldap32 BER, LDAP handle/options and control construction |
| System.Net.Http.WinHttpHandler | 8.0.3 | Native loopback redirects, gzip, Unicode, headers, 404 and cancellation |

The Windows-specific implementations now ship with `ref/net8.0` contracts and
`runtimes/win/lib/net8.0` implementations, following NuGet's
[compile/runtime asset selection rules](https://github.com/NuGet/docs.microsoft.com-nuget/blob/main/docs/create-packages/Select-assemblies-referenced-by-projects.md).
The consumer check targets plain `net8.0`, matching PowerShell's common props.
The MSYS2 package versions identify source release 8.0.31. Each NuGet package
keeps its upstream servicing version and signing identity. The package gate
also checks public/protected type/member signatures against the checked-in
upstream reference declarations, including inherited member resolution. It does
not establish attribute/layout/generic-constraint or full binary compatibility.
See `../../mingw-w64-dotnet-windows-libraries/README.md` for details and scope.

All five split packages were clean-built, installed and tested with a fresh
consumer cache and only the installed feed. Reproduce the installed check and
artifact audit from the repository root:

```bash
bash mingw-w64-dotnet-windows-libraries/check.sh /tmp/windows-libraries-installed
python3 tests/powershell-deps/audit-windows-libraries.py
```

`windows-libraries-results-20261026.txt` records commands and logs;
`windows-libraries-artifacts-20261026.json` records the five artifact hashes. NuGet
6.8 emits NU5131 during packing of the `ref`/`runtimes`-only packages. The plain
`net8.0` installed consumer builds without warnings/errors and exercises native
Windows asset resolution; behavior on other OSes is outside these checks.
`mingw-w64-dotnet-windows-configuration` adds ten more split packages from the
runtime and WinForms v8.0.31 sources: SystemEvents, Drawing.Common,
Windows.Extensions, Security.Permissions, EventLog, ConfigurationManager,
Runtime.Caching, PerformanceCounter, ServiceController and DirectoryServices.
NuGet versions match the PowerShell declarations; the two new transitive
packages are SystemEvents and Windows.Extensions. Source package versions,
commands and limits are in `../../mingw-w64-dotnet-windows-configuration/README.md`.

The checked-in binary EventLogMessages.res is discarded. Its 65536 message
entries are rebuilt from MC/RC text using windmc, llvm-rc and source-built csc.
Native FormatMessage checks the boundary IDs and Unicode substitutions. The
recipe also restores 13 Security.Permissions type forwards, and checks **9902**
upstream public/protected signatures and forwards across all ten libraries.
Native checks cover timers, GDI+/PNG/fonts, private XML configuration/DPAPI,
cache expiration/callbacks, event records, service reads, built-in performance
counters and local ADSI WinNT queries. No remote directory or machine write
operations are claimed.

Performance-counter queries are tested separately against a native C
HKEY_PERFORMANCE_DATA baseline, since Windows' registered providers can load
Framework DLLs even without a managed runtime. The managed counter test allows
only modules present in that native baseline; all other functional tests reject
every Framework module. This is distinct from System.Management's mandatory
wminet_utils.dll dependency, whose source port remains open. See the recipe
README for the native baseline and its documented Windows provider model.

All ten packages were clean-built, installed and tested with a fresh plain
net8.0 consumer/cache and only the installed feed:

```bash
bash mingw-w64-dotnet-windows-configuration/check.sh /tmp/windows-configuration-installed
python3 tests/powershell-deps/audit-windows-configuration.py
```

`windows-configuration-results-20261026.txt` records commands and logs;
`windows-configuration-artifacts-20261026.json` records the ten artifact hashes.
`mingw-w64-dotnet-crypto` adds six split packages from runtime v8.0.31:
System.Formats.Asn1 8.0.2, Cryptography.Pkcs 8.0.1, Cryptography.Xml 8.0.4,
DiagnosticSource 8.0.1, Text.Encodings.Web 8.0.0 and Text.Encoding.CodePages 8.0.0.
The ASN.1 -> PKCS -> XML chain uses exact separately built dependencies.
Checks cover BER/CER/DER, CMS signatures/tampering, AES envelopes/wrong keys,
PKCS12 password/MAC/key bags, XML signatures/encryption, diagnostics/metrics,
escaping and legacy encoding mappings, plus **1866** API/forward signatures.

These checks exposed a targeting-pack conflict-resolution bug: the old metadata
claimed runtime file version 8.0.31.0 for implementations actually built from
8.0.0 sources, so MSBuild discarded the new package DLLs. Targeting pack
**8.0.131-2** corrects 107 entries by reading actual runtime PE versions, retains
forwarding shims' own versions, and preserves every byte of all 169 DLLs.
The source generator is repaired too; predictions match the actual 163 managed
entries. This changes metadata, without upgrading the core runtime's sources.
Fresh crypto consumers require each loaded assembly's source/file version and
location under their output directory, preventing silent inbox substitution.

Reproduce the installed consumer, artifact audit and targeting metadata check:

```bash
bash mingw-w64-dotnet-crypto/check.sh /tmp/powershell-crypto-installed
python3 tests/powershell-deps/audit-crypto.py
bash tests/powershell-deps/check-targeting-metadata.sh /tmp/targeting-metadata-check
```

`crypto-results-20261026.txt` and `crypto-artifacts-20261026.json` contain the six
package results/hashes. `targeting-metadata-results-20261026.txt` and
`targeting-metadata-artifact-20261026.json` record the focused metadata repair,
source checksums, unchanged DLL payload and installed regression verification.

`mingw-w64-powershell-native` packages both upstream Windows x64 native assets
from **v700.0.0**: the WinRM plugin and the ETW resource DLL. The separate
`mingw-w64-dotnet-event-manifest-compiler` builds EventTraceKit's resource compiler
from source for .NET 8. GNU windmc, llvm-rc and lld-link generate all binary
resources. No Visual Studio build tools or NuGet implementation binaries are
used. The native port also fixes invalid pointer arithmetic when initialization
XML contains no Param token.

The clean package build and a fresh installed-feed-only net8.0/win-x64 consumer
pass. The gate verifies the actual restored DLLs' versions, 11 exports, startup
errors and plugin messages, then uses Windows TDH to read 194 events and compare
409 property occurrences with the untouched upstream XML. It requires the
provider to be absent before loading, avoiding installed-provider substitution,
and rejects Framework helper modules. Real WinRM sessions and full PowerShell
integration remain unverified. See `native-results-20261026.txt` and
`native-artifacts-20261026.json`; reproduce the installed check and audit with:

```bash
bash mingw-w64-powershell-native/check.sh /tmp/powershell-native-installed
python3 tests/powershell-deps/audit-native.py
```

`mingw-w64-dotnet-roslyn` adds two split compiler packages from the exact
**4.9.2** source commit named in the upstream NuGet listing:
`Microsoft.CodeAnalysis.CSharp` and its exact `Microsoft.CodeAnalysis.Common`
dependency. Both target net8.0, preserve assembly version 4.9.0.0 and the upstream
public-signing identity, and include all 13 localized satellite assemblies.
The source-built SDK compiles the actual 4.9.2 sources; its own compiler DLLs are
not repackaged as 4.9.2. All 151 upstream binary test inputs are removed first.

The package build compiles/runs three upstream generators and requires all five
regenerated files to match the checked-in baselines after the console generator's
specific blank-line cleanup, BOM and line-ending normalization. The consumer
executes emitted C# 12 code, validates deterministic PE/portable PDB output,
diagnostics, compiler options, cancellation, analyzer loading/callbacks and
incremental generation. It checks all **10,299** tracked public API declarations
and **35,750** strings across 39 localized resource sets, including an actual
German diagnostic. The clean build, fresh installed-feed-only check and artifact
audit all pass:

```bash
bash mingw-w64-dotnet-roslyn/check.sh /tmp/roslyn-installed-check
python3 tests/powershell-deps/audit-roslyn.py
```

`roslyn-results-20261026.txt` records exact commands and output;
`roslyn-artifacts-20261026.json` records hashes, dependencies and payload checks.
Native Windows PDB/DIA, full Add-Type/PowerShell integration and a complete ABI
or security audit remain unverified. Microsoft.CodeAnalysis.Analyzers 3.3.4 is
still a separate outstanding package; the compiler's upstream source-build
configuration permits building without those development analyzers.

`mingw-w64-dotnet-objectpool` adds Microsoft.Extensions.ObjectPool **8.0.31**
from the matching aspnetcore source tag, required directly by PowerShell and
by the WCF source port. Its clean package build, installed-feed-only check and
artifact audit pass. The checks cover all 51 upstream public API declarations,
reset/rejection policies, StringBuilder limits, disposal and 20,000 concurrent
leases. See `objectpool-results-20261026.txt` and
`objectpool-artifact-20261026.json`.

`mingw-w64-dotnet-syndication8` adds the source-built **8.0.0** Syndication
NuGet package from runtime v8.0.31 for the WCF legacy shim and
Windows.Compatibility 8. It coexists with the existing Syndication 10 recipe.
Its clean package build, installed-feed-only check and artifact audit pass:
639 API signatures, Atom/RSS Unicode/date/link/extension roundtrips, AtomPub,
malformed XML and all 37 neutral resource strings. This source archive has no
Syndication XLF translations. See `syndication8-results-20261026.txt` and
`syndication8-artifact-20261026.json`.

The WCF **4.10.3** work in `mingw-w64-dotnet-wcf` now builds the implementation,
WSDL library, five reference contracts, five runtime facades and the original
System.ServiceModel compatibility shim from source. Its package-consumer checks
pass for HTTP SOAP, text/binary/MTOM encoders, WSDL, NetTcp request/reply, duplex
callbacks, HTTPS certificate acceptance/rejection, 274 resolving legacy forwards,
and 26 localized resource sets / 12,896 exact strings. The clean seven-package
build, installed-feed-only check and artifact audit all pass. See
`wcf-results-20261026.txt` and `wcf-artifacts-20261026.json`.

The three reference/runtime mismatches were independently reproduced in
published upstream assembly metadata; the source implementation retains those
same omissions. The API gate checks 2,498 matching signatures and exactly three
audited exceptions. It rejects additional missing signatures and stale
exceptions. `wcf-published-metadata-20261026.json` and
`wcf-published-packages-20261026.json` record the evidence and hashes. Published
binaries were read only through PEReader/System.Reflection.Metadata and never
loaded, executed, compiled against or included in package payloads.

`mingw-w64-dotnet-windows-compatibility-libraries` adds nine verified transitive
libraries from runtime v8.0.31: CodeDom, Reflection.Context, MEF Composition and
Registration, ODBC, OLE DB, AccountManagement, serial ports and Speech. The clean
split-package build, installed-feed-only consumer and payload audit pass.
Checks cover 4,339 API signatures, 967 neutral resource strings, generated C#
execution, MEF composition/reflection projection, native ODBC diagnostics,
OLE DB COM enumeration, read-only local SAM lookup/groups, serial enumeration/
failed-open cleanup and native SAPI WAV synthesis. No Framework implementation
modules were loaded after those operations. These tests do not establish database
query interoperability, physical serial I/O, remote directory authentication or
microphone recognition. See `windows-compatibility-libraries-results-20261026.txt`
and `windows-compatibility-libraries-artifacts-20261026.json`.

`windows-compatibility-graph-20261026.json` records all 35 net8.0 dependencies,
verified against the pinned windowsdesktop source project/Versions.props and
published nuspec. **34 of 35 dependency IDs** now have verified packages;
SqlClient remains open. The
Microsoft.Windows.Compatibility meta package itself remains unfinished, as does
the full recursive dependency closure.

`mingw-w64-dotnet-threading-accesscontrol` release **8.0.0-2** fixes the old
Windows-only NuGet compile target: the rebuilt contract is in ref/net8.0 and
implementation in runtimes/win/lib/net8.0. Its clean build, fresh installed-feed
consumer, all 135 API signatures, all 21 resource strings and artifact audit
pass. Native named mutex/event/semaphore ACLs, reopening and contention pass.
Sources are runtime v8.0.31; the NuGet/assembly version remains 8.0.0. See
`threading-accesscontrol-results-20261026.txt`.

`mingw-w64-dotnet-management` replaces Framework's wminet_utils.dll with a
source-built Windows COM backend. Its clean package build, installed-feed-only
gate and artifact audit pass: 819 API signatures, all 79 resource strings and
49 exact native exports using only Windows/UCRT imports. Tests cover local WMI
queries/errors, metadata and in-memory edits, methods, enumerator clones,
asynchronous callbacks/GC/completion, owned-child process events/cancellation,
privilege token setup/reset, native bounds/error-info ownership and exact
managed/native NuGet selection. No Framework implementation module is loaded.
Persistent repository writes, remote credentials/delegation/providers and full
native ownership/ABI/race auditing remain unverified. See
`management-results-20261026.txt` and `management-artifact-20261026.json`.

The installed CIM/MMI and WMI packages also pass a combined single-process,
fresh-cache test in both query orders and after compacting GC. They agree on
the OS build and load Windows mi.dll plus the new WMI backend, with no Framework
implementations. This uses the experimental MMI 2 port and does not prove the
required MMI 3 compatibility. See
`../mmi-cim-wmi/combined-source-results-20261026.txt`.

`mingw-w64-dotnet-asyncinterfaces8` supplies the exact 8.0.0 facade from runtime
v8.0.31 and coexists with the existing version 10 recipe. Its clean build,
installed-feed check and audit pass for all 11 forwarded types, actual source
identity and async iteration/disposal/cancellation. The existing Newtonsoft.Json
13.0.4 recipe also passes its clean build and new installed-feed check: 144
exported types, typed/dynamic/Unicode/JSON-path/XML, DateOnly, BigInteger, ADO.NET,
decimal, depth limits and async reading. The source project's net8.0 feature
branch is retained.

`mingw-w64-dotnet-applicationinsights` adds core 2.23.0 from exact upstream commit
2faa7e8b157a431daa2e71785d68abd5fa817b53. Its clean build, installed-feed consumer
and audit pass all 1,033 PublicAPI declarations and in-memory telemetry,
correlation, initialization, JSON/gzip, clone/disable/flush behavior. It preserves
upstream 2.23.0.29/31bf identity; published DLL identity was read as metadata only,
never loaded, executed, compiled against or packaged. Tests sent no telemetry
externally. Transport/retry/platform integrations and full PowerShell use remain
unverified. See `runtime-core-libraries-results-20261026.txt` and
`runtime-core-libraries-artifacts-20261026.json`.

The `mingw-w64-dotnet-sqlclient` recipe now builds an experimental complete
Windows4.9.1 source package, retaining frozen assembly4.6.1.6 and all1020 contract
APIs. The generic unsupported-platform facade and generated reference assembly
are packaged alongside the full Windows managed-SNI runtime. All242/3 original
compiler inputs, four production overlays,407 resources, original SQL metadata,
framework references,142 TDS fixture inputs and tested package bytes pass audits.
Clean/fresh installed tests pass18 TCP/long/short pipe/TLS/SSPI scenarios, basic
MARS and the new MC-SMP inclusive-window regression, value/clone/record checks,
and actual source PowerShell scalar/reader/schema/DONE integration. Source builds
have0 warnings/errors; consumer obsolescence warnings are retained. See
[`sqlclient-package-results-20261027.txt`](sqlclient-package-results-20261027.txt).

This remains an experimental artifact, **not a completed direct requirement**.
LocalDB, lpc shared memory, real transactional FILESTREAM/database interoperability,
broader TLS/remote authentication, MARS error/cancellation/encryption/full SMP,
full shipping PowerShell/compatibility and runtime servicing remain open.
The historical native-SNI baseline exited2; its Debug managed-SNI path and earlier
three-overlay Release candidate are superseded by this packaged four-overlay
candidate. Their source/protocol evidence remains in
`sqlclient-prototype-results-20261026.txt`,
`sqlclient-managed-prototype-results-20261026.txt` and
`sqlclient-source-sni-results-20261027.txt`.

Two further packages pass clean builds, installed-feed checks and artifact
checks: `Microsoft.NETCore.Platforms 1.1.2` and `System.Security.AccessControl
6.0.1`. Platforms retains all 165 RIDs from the actual 1.1.2 source package
revision (corefx 718443a10dbd456b6c8f08ee594a92cfbf2ef8db); its earlier v1.1.2
source tag has a different graph and package version. A source-built NuGet
runtime model expands every packaged RID against independent closure checks.
The complete ACL6 Windows implementation comes from its published source
revision, runtime 189fbbd88d97dd6d65515ba2da05b62eab4e5039 (6.0.29). Its 560 API
signatures and 47 resources pass, plus native SDDL/ACE/ACL and owned-file
read/persist/reopen tests. An isolated load context proves these tests execute
source ACL6, since ordinary .NET 8 binding can choose inbox ACL8. Windows can
add AutoInherited on persistence; the gate compares exact DACL bytes and
protection state. See `platforms-accesscontrol-results-20261026.txt`.

The published MMI 3.0.0 reference and Windows implementation have equal public
metadata (512 entries), but use assembly version **1.0.0.0**. The experimental
2.0 port differs in that identity, the `CimFlags.Adopt` constant, and the
underlying type of `Options.ImpersonationType`. Its additional native helper
types/finalizers are recorded too. This is a metadata-only review; no published
implementation was loaded or executed. See
`mmi-security-published-metadata-20261026.json`. MMI 3 compatibility stays open,
including the unfinished native features and PowerShell integration.

The experimental MMI package is now `2.0.0.preview1-4`, with seven source
patches. Native destination/proxy credentials and prompt/error/message/progress
callbacks are implemented. Its independent C/mi.h probes, live local sync/async
queries, full existing MI checks, fresh installed feed, mixed CIM/WMI test and
artifact audit pass. Credentials preserve copied SecureStrings; native temporary
passwords are cleared in finally. Inquiry responses precede error cancellation.
Operation option copies retain managed settings and explicitly restore the
timeout omitted by the native clone. Actual remote authentication, provider-origin
extended callback/stream traffic and the MMI 3/ownership work stay
open. See `../mmi-cim-wmi/credential-callback-results-20261026.txt`.

The seventh MMI patch implements native streamed method parameters. C/mi.h
fixtures exercise all 32 scalar/array codes, individually typed flattened results,
nulls/Unicode and empty chunks for all array types. Retained instance/reference
clones keep session metadata; shortened results are disposed after delivery.
Observer/ack errors cancel once and emit one terminal error while queued results
are acknowledged/suppressed. The patch also fixes repeated indirect-wrapper frees,
partial clone cleanup and empty datetime arrays. Its clean package, installed
feed, mixed backend test and artifact audit pass; a real read-only EnumKey method
also accepts streaming-enabled options. Stream traffic itself uses a native C
fixture/operation stub, so provider-origin streaming remains an integration gap.
See `../mmi-cim-wmi/stream-results-20261026.txt`.

A separate `mingw-w64-dotnet-mmi3` candidate now packages the repaired public
source against NuGet **3.0.0** with assembly **1.0.0.0/31bf**, matching the
published MMI3 contract. It corrects Adopt and ImpersonationType and hides the
seven accidental native helper types. All 512 public/protected declarations
match, with only three explicitly permitted cleanup finalizers. Signed minimal
friend consumers validate the internal session-metadata calls in actual
PowerShell sources; these fixtures are not a PowerShell implementation. The
clean package, fresh installed consumer/API gate, full native checks, mixed
MMI3/WMI integration and artifact audit pass. The source remains the archived
2.0.0-preview.1 revision plus patches; no vendor3 implementation is repackaged.
This removes the known public contract/assembly differences but does not prove
remote/ownership/full PowerShell compatibility, so MMI3 remains incomplete.
See `../mmi-cim-wmi/mmi3-results-20261026.txt`.

`mingw-w64-dotnet-security-extensions` now packages a source replacement for
Microsoft.Security.Extensions **1.4.0**, with assembly **10.0.0.0/31bf** and all
29 published public/protected declarations. A source-built Windows C backend
uses WinTrust for catalog/embedded signatures and copies provider signer and
countersigner certificates into independently owned managed objects. The source
ABI attribution is pinned to MIT-licensed PowerShell 6.2.0. No proprietary
implementation DLL is repackaged. The separate `mingw-w64-osslsigncode` 2.14 recipe
provides local signing and offline timestamps for reproducible owned fixtures;
no certificate trust store changes are made.

The clean package and fresh installed-feed consumer/API gate pass for unsigned,
self-signed, timestamped, tampered and catalog-trusted binaries, signed/unsigned
PowerShell scripts, certificate lifetimes, file positions, parallel independent/
shared streams, Unicode rename/handle-only streams, argument validation and
bounded kernel-handle use. A trusted replacement at the original pathname cannot
turn an unsigned open handle into a trusted result. The payload/license/source
checksum audit also passes. Wider trust policy, revoked/distrusted/expired chains,
multiple signatures, provider/file-format behavior and actual PowerShell
Get-AuthenticodeSignature integration remain open. See
`security-extensions-results-20261026.txt` and run
`python3 tests/powershell-deps/audit-security-extensions.py`.

`mingw-w64-dotnet-workspaces-prerequisites` adds the five MEF2 assemblies,
System.IO.Pipelines and the dependency-only System.Composition package, NuGet
8.0.0 built from runtime v8.0.31 sources. These are required by Roslyn Workspaces
4.9.2 for the remaining analyzers and code fixes. MEF2 is distinct from the
already packaged MEF1 System.ComponentModel.Composition. The source graph and
published nuspec dependencies agree; net8.0 supplies Channels through the
source-built shared framework.

All seven split packages build and install. Fresh installed-feed checks pass
for six exact assembly identities, CLS attributes, all exported types, all 70
strings in five resource sets, MEF2 imports/metadata/shared scopes/disposal/
conventions/errors and Pipelines backpressure/advance/cancellation/completion/
stream ownership/error propagation. The independent metadata-only comparison
matches all 317 published public/protected declarations without extras, and the
payload/source/dependency/license audit passes. The meta-package ships its
net8.0 placeholder with no DLL. All installed DLL hashes match the audited
artifacts. Roslyn Workspaces and the direct analyzer requirements are completed in the later milestones below.
See `workspaces-prerequisites-results-20261026.txt`, the corresponding graph/API/
artifact records, and run
`python3 tests/powershell-deps/audit-workspaces-prerequisites.py`.

`mingw-w64-dotnet-roslyn-workspaces` now supplies Workspaces.Common and
CSharp.Workspaces 4.9.2 from the same pinned Roslyn commit as the compiler.
Both preserve assembly 4.9.0.0/31bf and all 1,898 published public/protected
metadata entries without missing/extra entries. The complete 1,467/272 main/
shared source lists, all expanded normal/restricted friend identities/namespaces,
13 satellite cultures and 98 resource sets/3,780 strings are retained and checked.
The upstream source-build configuration excludes optional SQLite caching and
uses Channels from the .NET 8 shared framework.

Clean packages, fresh installed-only consumer/API checks and payload/dependency/
license audits pass. The consumer exercises MEF CSharp services, Unicode documents,
project references/semantics/emit, cross-project references and rename, formatting,
simplification, real analyzer diagnostics, DocumentEditor-backed code fixes,
CodeAction/ApplyChanges operations, incremental edits and cancellation. Consumer
DLL hashes match audited assets. Broader workspace/provider/race and
MSBuild/IDE coverage, full PowerShell integration and servicing remain open.
See `roslyn-workspaces-results-20261026.txt` and run
`python3 tests/powershell-deps/audit-roslyn-workspaces.py`.

`mingw-w64-dotnet-commonmark` adds CommonMark.NET 0.15.1 from its exact source
tag, preserving netstandard1.0 assembly identity 0.1.0.0/001ef881 and all 200
published public/protected declarations. All 43 source items and original nuspec
version handling are retained. Its clean build, fresh installed consumer/API gate
and payload/license/dependency audit pass. All 622 unmodified upstream CommonMark
0.26 specification cases pass through both HTML formatters, alongside Unicode AST
positions, settings, URI resolution, streams, concurrency and null-contract checks.
Only the public component of the upstream CAPI key is used for public signing.
The installed specification data retains its original attribution/CC-BY-SA-4.0
license; implementation/helper code remains BSD-3-Clause. This supplies the
CommonMark dependency of the DocumentationAnalyzers packages described below. See `commonmark-results-20261026.txt` and run
`python3 tests/powershell-deps/audit-commonmark.py`.

`mingw-w64-dotnet-documentation-analyzers` now supplies both the requested
DotNetAnalyzers.DocumentationAnalyzers 1.0.0-beta.59 meta-package and its actual
Unstable 1.0.0.59 implementation dependency, from the exact tag source. All analyzer
and code-fix sources build against source Roslyn/Workspaces/CommonMark. The
published unstable identity 1.0.0.59/2bc5 is preserved with the upstream development
configuration and public-only signing.

Independent metadata-only and live checks cover all 18 analyzers, 14 code-fix
providers, 18 rule IDs, every descriptor and all four/71 resource sets/strings.
The empty public type surface is not used as a completeness gate. All 295 extracted
upstream diagnostic/fix vectors and an owned negative pass, with actual actions
from all 14 providers, including Markdown conversion. Source test corpus is
regenerated; exact fixed-text comparisons normalize only CR/LF. Configured cases
do not reproduce every original compiler-warning span assertion or claim the
original xUnit suite. The new keyword enum's entry without text is covered by the
owned empty-langword negative rather than the recognized-keyword provider.

Clean split packaging, fresh installed-feed checks, source/license/payload audits
and exact published inventories pass. Meta-only SDK/MSBuild asset loading through
the installed source csc49 host requires DOC103 for a negative case and builds
its correction. The CommonMark DLL matches its separate source package; scripts
and all 35/21 source files are included. Source attachments use explicit file
entries after the audit caught NuGet wildcard omission. Consumer DLL hashes match
the audited artifacts. Full PowerShell source/application integration remains.
See `documentation-analyzers-results-20261026.txt` and run
`python3 tests/powershell-deps/audit-documentation-analyzers.py`.

`mingw-w64-dotnet-stylecop-analyzers` supplies StyleCop.Analyzers 1.2.0-beta.556
and its exact Unstable 1.2.0.556 implementation dependency. All 2,811 source files
match the original Git tree and signed commit. The complete seven-file generator,
313 analyzer and 107 code-fix source files build against installed source
dependencies. The port preserves required syntax wrappers and original helper
implementations across newer Roslyn name collisions. Both generators reproduce
all 183 output names; four files have six explicit newer-property adaptations.
An actual upstream regression required an SA1134 guard: malformed attributes
recovered differently by the newer parser are left unchanged by incremental and
Fix All actions. No rule or code-fix implementation is removed.

Published metadata inventories match all 182 analyzer classes, 89 exported
code-fix providers and 178 DiagnosticId constants. Live checks validate 197
descriptors, all 17 resource sets/857 strings and seven cultures. All 2,880
extracted upstream vectors and 16 owned structural probes pass. Actual actions
cover every exported provider, including file rename/split, settings-file
creation, encoding changes and real Fix All. The separate C#12 probe executes
all analyzer classes without exceptions. The adapter is not the original xUnit
suite: unsupported helper/member-data/combined harness cases are recorded, and
compiler diagnostic assertions are excluded while StyleCop diagnostics, hidden
diagnostics and exact fixed text remain checked.

Clean build/check, final license repack, fresh installed-feed checks and payload
auditing pass. Only the requested meta-package is referenced in the actual
MSBuild sample: SA1001 is required for a negative and its correction builds.
All nine consumer DLLs match the audited source artifacts. Packages include all
production source inputs, original resources/XML, generated check data and full
MIT/Apache license material. Full PowerShell integration remains open. See
`stylecop-analyzers-results-20261026.txt` and run
`python3 tests/powershell-deps/audit-stylecop-analyzers.py`. The earlier prototype
records are preserved as historical evidence and superseded in `progress.json`.

`mingw-w64-dotnet-csharp-compiler` now packages the full 11-file csc driver
and two-file private RazorCompiler helper from the pinned Roslyn 4.9.2 source.
It installs a relative dotnet wrapper under compiler-packs/roslyn-4.9.2 and leaves
the SDK's compiler unchanged. Clean packaging and a fresh installed check pass:
actual MSBuild analyzer/incremental-generator callbacks, generated value/compiler
identity, CSharp12, Unicode/space response-file paths, expected analyzer/compiler
errors and exit1, relocation inside compiler-packs and original SDK hash.
Both compiler-library dependencies and all 26 satellites exactly match the
separately source-built packages. Full Toolset/server/IDE coverage and actual
PowerShell build remain open. This is additional build tooling, not a new direct
NuGet requirement. See `csharp-compiler-results-20261026.txt` and run
`python3 tests/powershell-deps/audit-csharp-compiler.py`.

`mingw-w64-dotnet-visualbasic-compiler` now adds the companion source vbc49
command-line host from the same pinned Roslyn source: all eleven original driver
files and the two-file private RazorCompiler helper. It installs under
compiler-packs/roslyn-vb-4.9.2, preserving the SDK compiler. Clean build/check,
focused response-file test correction, repack, installed checks and payload audit
pass. Actual MSBuild runs a source 4.9 analyzer and incremental VB generator,
executes its generated output, rejects the owned VBC4901 case, then builds the
correction. Unicode/space response files, BC30512/error exit, relocation and all
26 dependency satellite DLLs pass. This is additional tooling, not a direct or
transitive dependency count increment. See `visualbasic-compiler-results-20261027.txt`
and run `python3 tests/powershell-deps/audit-visualbasic-compiler.py`.

`mingw-w64-dotnet-roslyn-visualbasic` adds the source-built Visual Basic 4.9.2
compiler library. All 905 original non-generated compiler inputs and five
regenerated files build, with the original three source generators rebuilt.
All 6,176 published API declarations, 1,795 neutral resource values and four
embedded template byte streams match independent published metadata. Original
XLF sources provide thirteen satellites; all 14 resource sets/25,130 strings pass.
Focused checks emit and execute VB IL, portable PDBs, async/await, XML/query syntax,
analyzer and generator callbacks, overflow options and embedded VB runtime
conversions. Clean packaging, fresh installed-feed checks and artifact auditing
pass; all fourteen consumer DLLs match the audited package. The library preserves
the upstream SDK workaround for embedded VB support. Unchecked implementation
arithmetic handles internal bitmask/CRC constants; emitted user arithmetic is
checked independently. Six upstream bootstrap warnings remain. Visual Basic
Workspaces, vbc49 and the full analyzer package are now packaged separately
below; VBCSCompiler and broader Toolset/IDE coverage remain open.
See `roslyn-visualbasic-results-20261026.txt` and run
`python3 tests/powershell-deps/audit-roslyn-visualbasic.py`.

`mingw-w64-dotnet-roslyn-visualbasic-workspaces` adds VisualBasic.Workspaces
4.9.2. All 234 original inputs build without production source patches; the audit
independently matches every original path/hash against the pinned archive.
All 48 MEF export types and exact attribute blobs match independent published
metadata: 33 services, eight factories, six cleanup providers and one editorconfig
generator. The inspector follows export-attribute inheritance, including names
without an Export prefix. All exports activate and all 41 language services/
factories resolve in the real workspace. Empty public API alone is not used as
implementation completeness evidence. All three neutral resource sets/54 strings
match published metadata; 42 resource sets/756 original/XLF strings pass.

Actual VB/CSharp workspaces pass case-insensitive/cross-language reference search
and rename, VB formatting/simplification, project references, compilation and
executed VB/CSharp IL. Real analyzer/code-fix actions and DocumentEditor/
SyntaxGenerator produce changed VB whose emitted field initializer executes.
Clean packaging, fresh installed-feed checks and source/license/dependency/
payload audit pass; all fourteen consumer DLLs match the source package.
These focused gates do not replace full upstream/provider/IDE coverage. The full
Microsoft.CodeAnalysis.Analyzers package is now verified below; PowerShell
integration and runtime servicing remain. See `roslyn-visualbasic-workspaces-results-20261027.txt` and run
`python3 tests/powershell-deps/audit-roslyn-visualbasic-workspaces.py`.

`mingw-w64-dotnet-codeanalysis-analyzers` now packages the complete
Microsoft.CodeAnalysis.Analyzers 3.3.4 payload: 152 Core, 11 CSharp and 11 VB
original inputs from commit22ea6422f85b05ca0793cc3b76375487be407f5d. Two exact
compile substitutions preserve the original Fix All helper and correct an invalid
record-pattern colon in the activated compatibility branch. Independent artifact
auditing checks the complete original project/shared inventories and all source
hashes against the pinned archive. All325 published API declarations,23 analyzer/
13 fixer/46 ID inventories, original assembly/file identity and neutral/banned
resources match published metadata. All14 resource sets/2,030 strings pass.

All analyzers execute across34 declared language combinations. All68 selected
upstream plus10 owned cases pass; actual actions cover all13 providers, including
release-document edits, generated Fix All overrides and real C#/VB Fix All
callbacks. The fixture adapter has explicit omissions and does not replace the
xUnit suite. Newer annotations cause upstream nullable warnings. Corrected owned
MSBuild samples retain RS1004/RS1036 warnings; targeted RS1026 failure and its
correction are checked independently.

The complete15-source generator rebuilds all554 configuration/documentation
assets, matching every independent published text after line-ending normalization.
Original script sources/docs/license/notices and all production/generator source
inputs are retained, without private keys, vendor signatures, compiler/workspace
binaries or PDBs. Clean build/check, installed-feed checks and artifact auditing
pass. Actual CSharp/VB MSBuild select the correct NuGet language assets, require
RS1026 failures, then build corrections through installed source csc49/vbc49.
All30 NuGet DLL assets match the16 tested implementation/satellite DLLs exactly.
Full PowerShell integration and servicing remain open. See
`codeanalysis-analyzers-results-20261027.txt` and run
`python3 tests/powershell-deps/audit-codeanalysis-analyzers.py`. Earlier prototype
records are historical and superseded in `progress.json`.

The original PowerShell7.4.20 Windows core and CimCmdlets now also build from
source and execute together. The independent source audit covers all697 original
SMA C# inputs, nine Eventing inputs,35 CimCmdlets inputs and the original resource,
type-catalog and version generators. A real runspace passes functions, pipelines,
class compilation, XML type resolution and errors. The original CIM cmdlets pass
local OS/process queries, class metadata, GetOwner, caught native errors and CIM
serialization. Source WMI and experimental MMI3 agree in the same runspace,
survive GC and load no Framework implementations. See
[`../powershell-core/README.md`](../powershell-core/README.md) for reproducible
commands and build adjustments, and `powershell-core-results-20261027.txt` for
the recorded checks. This is an application-integration prototype, not a new
dependency package or complete pwsh application. Its core type catalog excludes
the unfinished full SDK graph, and the source SDK's implicit .NET analyzers remain a build-tool gap. The source
CodeStyle SDK integration now restores the original EnforceCodeStyleInBuild flag;
explicit source analyzer packages are enabled.

`mingw-w64-dotnet-codestyle` adds full CSharp/VB CodeStyle packages plus source
SDK integration from Roslyn4.9.2. All1561 original production inputs and the
five-source configuration generator build unchanged, preserving the six original
assembly/file4.9.10.12906/31bf identities. Independent metadata checks cover all100
public/protected declarations,135 analyzers,172 exported fixers and138 diagnostic
ID constants. All210 resource sets/5684 original/XLF strings,137 analyzer/language
callback runs and146 selected upstream cases pass; actual actions execute from11
fixer providers. The fixture adapter's unsupported cases and original harness
assertions outside its scope remain explicit. This is not the full IDE/xUnit suite.

Both NuGet packages carry all four original assemblies/satellites, generated
configuration/targets, original props/scripts/licenses/notices and complete source
provenance. All rule IDs/severities and targets/props match independent published
text; only two exact alternative descriptor-title comments are allowed to vary.
SDK integration installs the original layout without NuGet-only props. Source
csc49/vbc49 are selected explicitly; the original SDK compiler remains unchanged.
See `codestyle-results-20261027.txt`,
[`../../mingw-w64-dotnet-codestyle/README.md`](../../mingw-w64-dotnet-codestyle/README.md)
and `python3 tests/powershell-deps/audit-codestyle.py`. These are three build-tool
packages; the direct/transitive dependency artifact counts below are unchanged.
The separate SDK implicit .NET analyzer and runtime-servicing gaps remain open.

`mingw-w64-dotnet-netanalyzers` now also packages the exact .NET analyzer
source/version selected by SDK8.0.131:8.0.0-preview.26274.2, commit
0963af6bfc2a3d5083faa2b7c1c91a4eee29a6f4. All951 original production and15 generator
inputs build, with four explicit compile overlays preserving original Order helper
calls and CSharp argument-node types. Metadata checks match all3471 API
entries,293 analyzers,189 fixers and232 ID constants. All42 resource sets/15260
source/XLF strings,506 declared analyzer/language runs,922 selected upstream cases
and16 owned CSharp/VB regressions pass. Original empty compatibility declarations
remain explicit:22 analyzer exports and75 fixer exports retain their upstream
empty behavior. This is not the full upstream IDE/xUnit/compiler-diagnostic suite.

All609 generated configuration/documentation files and two original docs match
independent published text. All30 NuGet DLL assets match16 runtime-tested main/
satellite DLLs; SDK copies match those package assets. Source SDK8.0.131-2 keeps
ownership of its two import files and forwards them to the separate analyzer
provider. Conditional compiler selection uses csc49/vbc49 when the caller has
not chosen a compiler. CSharp/VB NuGet and SDK samples reject CA1822 violations
and accept static/Shared corrections. No package file-overwrite flags are needed.

PowerShell's original core/Eventing/CimCmdlets now build with both
EnableNETAnalyzers=true and EnforceCodeStyleInBuild=true, alongside the explicit
source analyzer packages. The original697/9/35 source inventories and selected
SDK analyzer paths pass. Real local CIM/WMI/module/serialization/GC integration and
all27 application payload DLLs pass. The original version generator has three
non-fatal CA1846 suggestions after its.NET8 host retarget. Analyzer-library
builds have recorded upstream nullable/obsolete/narrowing warnings.
See `netanalyzers-results-20261027.txt`,
[`../../mingw-w64-dotnet-netanalyzers/README.md`](../../mingw-w64-dotnet-netanalyzers/README.md)
and run `python3 tests/powershell-deps/audit-netanalyzers.py`. This supersedes the
earlier implicit .NET analyzer implementation gap, without changing the direct/
transitive artifact counts. Full PowerShell application/modules/native behavior
and runtime servicing remain open.

An earlier checkpoint recorded **75** built and checked dependency artifacts:
**44 of 53 direct IDs**, 29 transitive packages, and two experimental MMI
ports (the original preview and a new MMI3 contract candidate). Full MMI3
behavior/application compatibility stays open. The updated targeting pack is
additional tooling work, alongside the new manifest compiler, osslsigncode and csc49 host. The full transitive
graph and full PowerShell application integration remain incomplete.

Remaining work includes the other .NET satellite libraries and their servicing
versions, SqlClient,
PowerShell's native WinRM integration and all bundled modules. MMI authentication,
remote CIM/WSMan, provider-origin extended callbacks, streamed results, event delivery and
complete ownership/API compatibility still need work. The WMI backend still
needs persistent-write, remote/provider and complete ownership/ABI testing.
Microsoft.Security.Extensions has a source replacement; broader policy/format
coverage and full application integration still need work.

The installed core runtime now uses actual **8.0.31 sources**, with audited
source host/runtime packs, targeting generators and SDK framework metadata.
Earlier8.0.31 directory aliases were insufficient to prove that servicing scope;
the source-servicing package evidence supersedes that bootstrap limitation.
PowerShell7.6 still requires a source.NET10 toolchain and its own complete
dependency/compatibility validation.

The first .NET10 component is now packaged and installed separately:
`mingw-w64-clang-x86_64-dotnet-host10-bootstrap 10.0.5-1`. Its clean source
build, archive audit and installed checks pass for seven native host images,
ten support assets and all 28 owned files. See
[`../runtime10-bootstrap/native-package1-results-20261027.txt`](../runtime10-bootstrap/native-package1-results-20261027.txt).
CoreCLR10, managed runtime, SDK10 and PowerShell7.6 remain open. A mechanical
review of the old CoreCLR patches records 31 rejected hunks and one removed
assembly path; that review is not a usable CoreCLR10 port. Dependency counts
remain 50/53 direct, 52 transitive and 106 artifacts including four experimental.

CoreCLR10 and RyuJIT now also compile in a private source bootstrap. Native
startup/ABI checks pass, including 120 JIT host callbacks, four PE versions
and a loaded cDAC descriptor generated from original sources. The 64-file
port retains vxsort and secure DLL delay loading. See
[`../runtime10-coreclr/native-results-20261027.txt`](../runtime10-coreclr/native-results-20261027.txt).
This remains a private bootstrap rather than an installed runtime10 package
or completed SDK10/PowerShell7.6 integration.

The matching source CoreLib10 now passes private managed execution as well:
basic JIT/generics/Unicode work, compacting GC, exceptions/finally and threading.
All1,932 original C# inputs remain unchanged, all1,421 resources match, and a
fresh managed-output rebuild is byte-identical to the tested input. See
[`../runtime10-corelib/managed-results-20261027.txt`](../runtime10-corelib/managed-results-20261027.txt).
The full framework/reference packs and source
SDK10/WindowsDesktop10/PowerShell7.6 closure remain open. Dependency counts
are unchanged.

The original ILLink10 library rewrite now passes as well. Seven complete source
projects build, including the exact pinned Cecil revision and the original
supported-net8 Immutable10/Metadata10 inputs. The linked CoreLib preserves all
31,189 checked public surface entries, resources and readable symbols, and
passes all four native CLR10 managed probes without the earlier fcall helper.
See [`../runtime10-illink/managed-results-20261027.txt`](../runtime10-illink/managed-results-20261027.txt).
This remains a private tool/bootstrap milestone; the full .NET10 framework,
reference packs, SDK, desktop and PowerShell7.6 package closure are incomplete.

The complete declared .NET10 public reference-contract graph now compiles:
167 public reference assemblies and 19 original-target generated private
facade stubs. Every identity, key, version, reference marker and dependency
matches, including 22 original facade version rules. All203 selected C# inputs
and286 project/property inputs match the pinned source archive. A consumer
requiring newer .NET10 APIs compiles; the same source/compiler is rejected by
the net8 reference pack. See
[`../runtime10-reference/reference-results-20261027.txt`](../runtime10-reference/reference-results-20261027.txt).
The original generator bundle and final targeting10 package remain open,
alongside the complete framework/SDK/desktop/PowerShell7.6 closure.

The original generator dependency work now adds ten private net8 NuGets:
eight source9 libraries and the Roslyn4.14 Common/CSharp compiler libraries.
Fresh consumers, all10,479 compiler public API declarations,42 localized
compiler resource sets and actual compiler dependency-byte audits pass.
Selected source inputs independently match the pinned archives. See
[`../runtime10-generator-deps/source-results-20261027.txt`](../runtime10-generator-deps/source-results-20261027.txt).
Workspaces4.4, the required generator TFMs and final clean packaging
remain open; no compiler/runtime pacman installation or dependency-count
increase is claimed by this private bootstrap.

The subsequent source Workspaces4.14 bootstrap passes fresh-consumer checks for
all1,890 public API declarations,133 friend attributes and98 original resource
sets. Real MEF composition, cross-project rename, formatting, code fixes,
incremental edits and source Channels9 lifetime checks pass. All four private
NuGets and26 actual compiler dependency references match their source feeds.
See [the workflow and scope](../runtime10-workspaces414/README.md) and
[recorded results](../runtime10-workspaces414/source-results-20261027.txt).
The full dependency goal and established package counts remain unchanged.
