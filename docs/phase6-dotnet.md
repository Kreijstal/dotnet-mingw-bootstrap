# Phase 6: from Mono 6.12 to a source-built .NET SDK on MSYS2

Status as of 2026-10-09. Host: `ssh topkek@win11`, MSYS2 UCRT64, 4 cores.
Work tree on win11: `~/dotnetboot/m612/`. Scripts: `bootstrap/m612/`,
environment `bootstrap/env-m612.sh`, patches `patches/m612/`.

## 0. Policy for this phase (changed 2026-10-08)

The user changed the trust boundary. **Upstream MSYS2 packages are trusted**,
including `mingw-w64-ucrt-x86_64-mono 6.12.0.199`, even though MSYS2 seeds
that package with monolite and ships a Roslyn 3.9 binary blob as `csc.exe`.
That Mono is the seed of this phase. Everything above it must be built from
source:

- no Microsoft SDK or runtime zips, and no `dotnet-install`;
- no prebuilt Roslyn, MSBuild or NuGet packages containing IL from the
  dotnet org;
- no monolite;
- plain source tarballs and git trees are fine;
- NuGet packages that contain only reference-free data are allowed only
  with a justification. In practice none was needed.

Every binary input that was even considered is listed in §7. The old
`bootstrap/env.sh` keeps poisoning the host Mono for the phase 1–3 chain.
`bootstrap/env-m612.sh` is a separate environment that deliberately allows
`/ucrt64/bin/mono`.

What the seed provides, and what this phase uses from it:

- `mono.exe`, the runtime with its 4.5 class libraries;
- `csc.exe` 3.9.0-6.21124.20, used once, to compile Roslyn 4.0.1;
- `resgen`;
- `ilasm`, used once, to assemble `System.Runtime.CompilerServices.Unsafe` from IL source;
- `monodis`, used to read assembly versions and public keys;
- `lib/mono/4.7.2-api`, the .NET Framework 4.7.2 reference assemblies, used as compile references;
- `lib/mono/xbuild-frameworks`, the framework redist lists.

## 1. How the official bootstrap works, and why it does not help on Windows

Sources: the research notes behind this section. Every claim was checked
against the repositories.

- **source-build / VMR (dotnet/dotnet).**
  - Stage 1 is `prep-source-build.sh`. It downloads a *Microsoft-built*
    portable SDK and the "previously source-built" artifacts tarball
    `Private.SourceBuilt.Artifacts.*.tar.gz`, then runs `build.sh --source-only`.
  - Stage 2 rebuilds with `--with-sdk <stage1-sdk> --with-packages <stage1-artifacts>`.
  - New distros or architectures still start from a Microsoft SDK, either
    with a patched RID graph or cross-compiled.
  - The version rule is that each major needs an SDK of the same major.
    v8.0.0 needs `8.0.100-rc.2`, v9.0.0 needs `9.0.100-rc.2`, and v10.0.100
    needs `10.0.100-rc.2.25502.107`. In the words of
    `Documentation/bootstrapping-guidelines.md`, "you cannot use a 8.0
    version of the SDK to build a 9.0 SDK".
- **Prebuilt detection.** Anything not built in the current source-build
  counts as a prebuilt.
  - Usage is written to `artifacts/log/Release/prebuilt-usage.xml`.
  - Before .NET 10, each repo had a per-repo baseline in
    `eng/SourceBuildPrebuiltBaseline.xml`; from .NET 10 the check runs only
    in the VMR.
  - `dotnet/source-build-reference-packages`, now `source-build-assets`,
    holds reference packages *generated as C# source* (GenAPI) and compiled.
    Target packs are rebuilt from IL source.
  - It has **no net472 targeting pack**.
- **Windows.**
  - `system-requirements.md` says "Windows is not currently supported" for
    source-only builds (source-build#2910).
  - The Windows "unified build" of .NET 10 is explicitly *not* prebuilt-free.
  - So the official machinery cannot be used on Windows at all. We have to
    build our own way up to the point where some SDK exists that can drive
    the real build.
- **Nobody has done this before.**
  - Guix stops at Mono 6.12 + Roslyn 3.9 + MSBuild 15.7.
  - Gentoo, Fedora, Alpine, NixOS, Arch, Ubuntu and FreeBSD all seed with a
    Microsoft SDK plus the PSB tarball.
  - source-build#1930 ("Full source bootstrap", 2020) was closed with
    "Roslyn takes dependencies on new C# features very quickly… a lot of
    build tooling is self-hosting, like MSBuild, NuGet, Arcade".
  - No "bootstrap dotnet from mono" project exists.

## 2. What an SDK is made of (what has to be produced)

| Component | Repo | Language | Needed as | Status |
|---|---|---|---|---|
| C# compiler (`csc`, Microsoft.CodeAnalysis*) | dotnet/roslyn | C# | net472 build on Mono now; netX build in the SDK later | **built (4.0 → 4.12)** |
| MSBuild engine + tasks + Common targets | dotnet/msbuild | C# | net472 on Mono now; netX in the SDK later | **built (17.8.49, net472)** |
| Roslyn MSBuild task (Csc) + Microsoft.CSharp.Core.targets | dotnet/roslyn | C# | with MSBuild | **built** |
| netfx "package" assemblies (SCI, SRM, System.Memory, STJ, …) | dotnet/runtime, dotnet/maintenance-packages | C#/IL | MSBuild/Roslyn deps on Mono | **built** |
| NuGet client (NuGet.Build.Tasks, NuGet.* libs, restore) | NuGet/NuGet.Client | C# | restore and package resolution in SDK builds | **built** (net472 §6.5, net8.0 §6.11) |
| .NET SDK tasks and targets (Microsoft.NET.Build.Tasks, Sdks/Microsoft.NET.Sdk) | dotnet/sdk | C# + targets | SDK-style projects | **built** (net472 §6.6, net8.0 §6.11) |
| dotnet CLI (`dotnet.dll`, Cli.Utils, TemplateEngine, System.CommandLine, resolvers) | dotnet/sdk, templating, command-line-api | C# | `dotnet build/new/run/pack/publish` | **built** (net8.0, §6.11) |
| Reference packs (Microsoft.NETCore.App.Ref) | dotnet/runtime `src/libraries/*/ref` | C# | compiling anything for netX | **built** (§6.7; generators in `analyzers/dotnet/cs`, runtime pack, §6.11) |
| System.Private.CoreLib + framework libraries | dotnet/runtime | C# | the shared framework | **built** (Mono flavor, 168 assemblies, §6.10) |
| Runtime: CoreCLR (`coreclr.dll`, `clrjit.dll`) **or** the Mono flavor of dotnet/runtime | dotnet/runtime | C/C++/asm | the shared framework | Mono flavor native runtime built (§6.9); CoreCLR not started (§3) |
| Host: `dotnet.exe`, `hostfxr.dll`, `hostpolicy.dll`, `apphost.exe` | dotnet/runtime `src/native/corehost` | C/C++ | running anything | **done** with CLANG64 (§6.8) |
| Native shims (System.IO.Compression.Native, Globalization) | dotnet/runtime `src/native` | C | framework | **built** (§6.9; Globalization is in the runtime) |
| Arcade SDK, source generators, ILLink, GenAPI … | dotnet/arcade, runtime | C# | only to run the upstream build | avoided (direct csc); generators built from source; GenFacades/PNSE replaced by small scripts (§6.10); ILLink built for PublishTrimmed (§6.11) |

## 3. Native parts on MinGW

The source is dotnet/runtime `main` and `release/9.0`, read from shallow
clones. Nothing was built.

- **CMake assumes MSVC on Windows.**
  - The Windows branches of `eng/native/configurecompiler.cmake` are all
    `if (MSVC)`. They set `/W`, `/guard:cf`, `/guard:ehcont`, `/CETCOMPAT`
    and MASM `/Zi`.
  - Static `libcmt`, `libvcruntime` and `libcpmt` are hard-wired at
    L551–558.
  - `enable_language(ASM_MASM)` and `mc` are mandatory (L1141–1164), and
    `genEtwProvider.py` runs `mc.exe -um`.
  - No mingw or clang-cl branch exists.
- **SEH.** `PAL_TRY` maps to native `__try/__except` on Windows; 74 files
  use it, plus 16 files with a direct `__try`.
  - The runtime relies on MSVC `/EHa` semantics (catch(...) catches access
    violations, C++ exception code `0xE06D7363`).
- **Assembly.** CoreCLR on win-x64 needs about 24 MASM `.asm` files (vm/amd64
  19, runtime/amd64 3, debug 2). The Unix `.S` versions use the SysV ABI and
  cannot be reused as they are.
  - Two existing MASM-compatible assemblers could be tried: UASM (MSYS2
    package `mingw-w64-uasm`) or `llvm-ml`.
- **TLS.** `vm/threadstatics.cpp` and `vm/threads.cpp` read `_tls_index`
  and the TEB directly, so real PE TLS is required. Clang provides it; GCC
  on MinGW historically used emutls.
- **Prior art.**
  - dotnet/runtime PR #128264 (2026-05, EgorBo) tried clang-cl *with* the
    MSVC SDK. It got `clrjit.dll` working and then died on clang crashes,
    and was closed.
  - There is nothing for MinGW.
  - MSYS2 issue #20075 ("Add mingw-w64-dotnet") stalled on "massive amount
    of patching".
- **The host is easy.** `src/native/corehost` keeps its MSVC bits behind
  `if (MSVC)`, and apphost was rewritten in C (PR #126367). `clrjit` is
  already cross-built for the Windows ABI from Linux compilers
  (`clrjit_win_x64_*`).
- **Verdict on toolchains.** A real `x86_64-w64-mingw32` target with CLANG64
  clang/lld/libc++ is the realistic route: clang accepts `__try` on MinGW
  targets, has `-fms-extensions` and uses real TLS.
  - clang-cl + mingw-w64 is effectively blocked. It needs an MSVC-ABI C++
    runtime (vcruntime/STL) that is proprietary or nonexistent in MSYS2.
  - UCRT64 GCC would need all 90 SEH files rewritten.
- **Alternative runtime.** dotnet/runtime also contains the **Mono runtime
  flavor** (`src/mono`, C, still with some `__MINGW32__` paths).
  - .NET on s390x and ppc64le ships with it.
  - It is a better first target for "a runtime under the .NET 8+ shared
    framework, built with MinGW" than CoreCLR. Its native code is C with no
    MASM, and Mono itself has a long MinGW history; Wine-mono is built with
    MinGW.

## 4. The stage chain

Statuses:

- **SOLVED-HERE**: built and tested in this session;
- **SOLVED-ELSEWHERE**: someone did it, not on Windows;
- **PROBABLY**: no blocker known, and a precedent exists;
- **UNKNOWN**;
- **BLOCKED**.

| # | Stage | Built by | Status | Evidence |
|---|---|---|---|---|
| S0 | Mono 6.12.0.199 (MSYS2 package; csc 3.9) | — | trusted seed | policy |
| S1 | Roslyn 4.0.1 (C# 10) | seed csc 3.9 | **SOLVED-HERE** | 1 patch (7 files, 29 changed lines) rewrites C# 10 syntax; C# 10 test passes |
| S2 | Roslyn 4.4.0 → 4.6.0 → 4.8.0 (C# 12) | previous stage | **SOLVED-HERE** | no patches. 4.8's primary constructors need 4.6, which has them as preview, so 4.4 → 4.8 does not work directly |
| S3 | netfx package assemblies from source | Roslyn 4.8/4.12 | **SOLVED-HERE** | `netfx-pkgs.sh`. maintenance-packages 2025.03.19: Unsafe, Buffers, Vectors, Tasks.Extensions, Memory, Microsoft.IO.Redist. runtime v8.0.0: SCI, SRM, Bcl.AsyncInterfaces, Encodings.Web, System.Text.Json, Dataflow, MetadataLoadContext, Resources.Extensions, plus the LibraryImportGenerator source generator. All public-signed with the upstream public keys |
| S4 | Roslyn 4.10.0 → 4.11.0 → 4.12.0 (C# 13) | previous + S3 | **SOLVED-HERE** | 4.10 needs SCI 8 / System.Buffers (S3); 4.12 needs 4.11 (ref struct interfaces); C# 13 test passes |
| S4b | Roslyn 4.13.0 → 5.0.0-preview (tag NET-SDK-10.0.100-preview.4) → 5.0.0 (C# 14) | previous | **SOLVED-HERE** | no patches; 5.0 needs an intermediate with C# 14 extension blocks; csc sources from CscCommandLine.projitems; C# 14 test passes |
| S5 | MSBuild 17.8.49, net472, MonoBuild | Roslyn 4.12 + S3 | **SOLVED-HERE** | 1 patch (7 files: rotten MONO code paths plus FrameworkPathOverride). An SDK-less net472 csproj builds with the Roslyn Csc task through `mono` and runs |
| S6 | NuGet.Client 6.8.2.3 (net472: 15 NuGet.* libs, NuGet.Build.Tasks, NuGetSdkResolver) | S4 + S5 | **SOLVED-HERE** | no patches; deps Newtonsoft.Json 13.0.3, Microsoft.Web.Xdt 3.2.12, Microsoft.Extensions.* 8.0 from source; PackageReference restore from a local feed works in MSBuild on Mono |
| S7 | dotnet/sdk 8.0.131 `Microsoft.NET.Build.Tasks` (net472) + `Sdks/Microsoft.NET.Sdk` on MSBuild/Mono | S4–S6 | **SOLVED-HERE** (net472 targets) | no patches; deps DependencyModel, HostModel (runtime 8.0.0), Deployment.DotNet.Releases from source; an SDK-style net472 Exe restores, builds and runs on Mono. Missing pieces stubbed/disabled: workloads, SourceLink, roslyn-analyzers (empty NetAnalyzers.props), BundledVersions.props hand-written |
| S8 | `Microsoft.NETCore.App.Ref` 8.0 (163 ref assemblies + facades, 20 shim stubs, FrameworkList.xml) | S4 | **SOLVED-HERE** | `refpack.sh`; identities match upstream (e.g. System.Runtime 8.0.0.0, b03f5f7f11d50a3a); an SDK-style **net8.0** library compiles against it with the SDK of S7 on Mono (UseAppHost=false, no runtime needed) |
| S9 | System.Private.CoreLib + `src/libraries/*/src` for net8.0-windows (Mono flavor) | S4 + S8 | **SOLVED-HERE** | `corelib.sh`, `libraries.sh`, `vbc.sh`, `framework.sh`, §6.10: CoreLib + 166 C# + Microsoft.VisualBasic.Core (VB compiler from source), nothing skipped, no source patches; `shared/Microsoft.NETCore.App/8.0.0` runs a net8.0 hello world and a smoke test (exceptions, threads, ICU, sockets, HttpClient/TLS, JSON, Regex, XML, Process, Reflection.Emit) on the from-source Mono runtime |
| S10a | Host (dotnet.exe, hostfxr, hostpolicy, apphost, nethost, comhost) with CLANG64 | MinGW toolchain | **SOLVED-HERE** | `corehost.sh`, §6.8: 3 patches (GNU/MinGW branch in `eng/native`, skip ijwhost/tests, 7 small clang/libc++ source fixes); self-contained binaries (static libc++); `dotnet --info` runs, the SDK patches apphost.exe for a net8.0 `UseAppHost=true` app, apphost → hostfxr → hostpolicy load and report the missing runtime. singlefilehost needs `coreclr_static` (S10c) |
| S10b | Runtime: Mono flavor of dotnet/runtime 8.0 (`-rf Mono`) with MinGW | MinGW | **SOLVED-HERE** (native part) | `mono-runtime.sh`, §6.9: 4 more patches (MSVC-only CMake, `__thread`, no MASM, POSIX configure results cleared, ICU C headers, zlib-intel/windres fixes). `coreclr.dll` (Mono VM) + `System.IO.Compression.Native.dll` build with CLANG64 and import only system DLLs; dotnet.exe → hostfxr → hostpolicy → `coreclr.dll` initialises Mono and stops at the missing System.Private.CoreLib (S9) |
| S10c | Runtime: CoreCLR with CLANG64 clang (GNU driver) | MinGW | **BLOCKED for now** (large port) | MSVC-only CMake, ~24 MASM files, /EHa semantics, mc.exe ETW, PE TLS (§3) |
| S11 | Assemble SDK 8.0 (host + shared framework + SDK managed bits built for net8.0) | all above | **SOLVED-HERE** | §6.11: Roslyn, MSBuild, NuGet, SDK tasks/resolvers, `dotnet.dll`, TemplateEngine, SourceLink, ILLink as net8.0 assemblies in the upstream `sdk/8.0.131` layout; `dotnet new/build/run/pack/publish` (incl. self-contained, trimmed, PackageReference from a local feed, `-m` with node reuse, the compiler server) run on the from-source Mono-flavour runtime, no Mono 6.12. 1 patch (Roslyn compiler server mutex); open: roslyn-analyzers, vstest, F#, XPlat |
| S12 | Rebuild the SDK with itself (VMR, Windows) | S11 | UNKNOWN | VMR source-only mode is Linux-only; Windows would need Arcade from source and a local package feed |

Earlier stages S0–S5 run only on Mono. Everything up to S8 is managed code
and is platform-neutral.

## 5. Method

The method is Guix's: no MSBuild, NuGet or Arcade. Each assembly is
compiled by one `csc` command line. The difference from Guix is that the
source lists are not hand-maintained:

- **`tools/csproj_items.py`** evaluates the *subset* of MSBuild these
  repositories use: properties, Compile/EmbeddedResource items with
  conditions, wildcards, `.projitems` and `.props/.targets` imports, and the
  `[MSBuild]::IsTargetFrameworkCompatible/GetTargetFrameworkIdentifier/VersionGreaterThan…`
  functions.
  - It also implements the SDK default `**/*.cs` glob.
  - That lets us read the real upstream `.csproj` files, such as
    `System.Text.Json.csproj` for `TargetFramework=net462`, instead of
    copying file lists.
- **`tools/ResxGen.cs`** stands in for Arcade's `GenerateResxSource`. It
  generates the strongly typed resource class, including the dotnet/runtime
  `SR` + `FxResources.<Asm>.SR` shape. `.resources` files come from the
  seed's `resgen`.
- **Compile references.**
  - Roslyn, MSBuild and the packages are compiled against **Mono's
    .NET Framework 4.7.2 reference assemblies** (`lib/mono/4.7.2-api`).
  - Mono's *runtime* `mscorlib` has its own public `Span<T>`. Compiling
    against it clashes with `System.Memory`'s `Span<T>` (CS0433/CS0012).
  - With the reference assemblies the result is a normal net472 build that
    runs on Mono.
- **Strong names.** Every from-source assembly is *public-signed* with the
  upstream *public* key: `keys/*.pubkey.hex`, read from seed assemblies with
  `monodis`, with the tokens verified. Assembly identities therefore match
  the upstream packages, which keeps InternalsVisibleTo, binding redirects
  and analyzer loading working.
- **Source generators.** Libraries that need source generators get them
  built from source as Roslyn analyzers and passed with `-analyzer:`.
  - The LibraryImportGenerator is built against our own Roslyn.
  - Its `build_property.TargetFramework*` inputs come from a generated
    `.globalconfig`.
- **Checked-in generated sources.** Roslyn's checked-in generated syntax,
  bound-tree and error-facts sources are deleted and regenerated with the
  generators compiled from the same tree. The regenerated files differ from
  the checked-in ones only in blank lines.
- **Provenance.** Every tarball is scanned and all PE, archive and object
  files are deleted before building (`delete_binaries`):
  - roslyn: 161–167 test-resource PE files per version;
  - runtime-8.0.0: 26;
  - msbuild: 0;
  - maintenance-packages: 0.

## 6. What was built and tested (exact commands)

All commands run on win11 from `~/dotnetboot` after
`rsync bootstrap/ patches/ → ~/dotnetboot/repo/`, with
`env MSYSTEM=UCRT64 bash -lc …`.

### 6.1 Roslyn chain (`bootstrap/m612/roslyn.sh <ver> <bootstrap-csc.exe>`)

```
bash repo/bootstrap/m612/roslyn.sh 4.0.1  /ucrt64/lib/mono/4.5/csc.exe      # seed csc 3.9
bash repo/bootstrap/m612/roslyn.sh 4.4.0  m612/roslyn-4.0.1/csc.exe
bash repo/bootstrap/m612/roslyn.sh 4.6.0  m612/roslyn-4.4.0/csc.exe
bash repo/bootstrap/m612/roslyn.sh 4.8.0  m612/roslyn-4.6.0/csc.exe
PKGDIR=m612/pkgs bash repo/bootstrap/m612/roslyn.sh 4.10.0 m612/roslyn-4.8.0/csc.exe
PKGDIR=m612/pkgs bash repo/bootstrap/m612/roslyn.sh 4.11.0 m612/roslyn-4.10.0/csc.exe
PKGDIR=m612/pkgs bash repo/bootstrap/m612/roslyn.sh 4.12.0 m612/roslyn-4.11.0/csc.exe
```

(The csc paths are passed through `cygpath -m`.) Each run does the following:

1. fetch the tag tarball and delete its binaries;
2. apply `patches/m612/roslyn-<ver>/`;
3. `resgen` and ResxGen;
4. compile and run the CSharpSyntaxGenerator, BoundTreeGenerator and
   ErrorFactsGenerator;
5. compile Microsoft.CodeAnalysis, Microsoft.CodeAnalysis.CSharp and
   `csc.exe` (net472, public-signed);
6. install to `m612/roslyn-<ver>/`;
7. compile and run `tests/cs10.cs … cs13.cs` up to the compiler's language
   version.

A full version takes about 1–2 minutes. Output of the last step for 4.12.0:

```
$ mono m612/roslyn-4.12.0/csc.exe -langversion:latest -out:csNN.exe tests/csNN.cs tests/polyfill.cs && mono csNN.exe
C# 10 on Mono: Person { Name = Ada, Home = Point { X = 10, Y = 4 } } east 40 30
C# 11 on Mono: 1..2..9 empty file-local boot11 Int32 39
C# 12 on Mono: c=42 1,2,3,4,5 5/3 15
C# 13 on Mono: 6 42 7 15 27
$ mono m612/roslyn-4.0.1/csc.exe -version
4.0.1 (m612 bootstrap) ()
```

The tests cover these features:

- **C# 10:** records, record structs, `with`, global usings, file-scoped
  namespaces, constant interpolated strings, lambda natural types, extended
  property patterns.
- **C# 11:** raw strings, list patterns, required members, generic
  attributes, file-local types.
- **C# 12:** primary constructors, collection expressions, alias any type,
  default lambda parameters.
- **C# 13:** params collections, partial properties, `^` in object
  initialisers, `\e`, ref struct implementing an interface.

`allows ref struct` generics are correctly rejected on Mono with CS9240
("Target runtime doesn't support by-ref-like generics").

Steps that did not work directly:

- **4.0.1 from csc 3.9.** It needs patch 0001: a parameterless struct
  constructor, extended property patterns, `with` on a tuple, and mixed
  declaration/assignment deconstruction.
- **4.4 → 4.8 directly.** Fails on primary constructors, so 4.6 is used as
  an intermediate.
- **4.8 → 4.10.** Fails until SCI 8, SRM 8 and System.Buffers are built
  from source. The errors were `ArrayPool`, `ArrayBuilder.RemoveRange`,
  "This version of ImmutableArray<T> cannot be used with collection
  expressions" and `Span.ToImmutableArray`.
- **4.10 → 4.12 directly.** Fails on "ref structs cannot implement
  interfaces", so 4.11 is used as an intermediate.

### 6.2 Package assemblies (`bootstrap/m612/netfx-pkgs.sh <csc.exe> m612/pkgs [lib…]`)

```
bash repo/bootstrap/m612/netfx-pkgs.sh m612/roslyn-4.12.0/csc.exe m612/pkgs
bash repo/bootstrap/m612/netfx-pkgs.sh m612/roslyn-4.12.0/csc.exe m612/pkgs Microsoft.Bcl.AsyncInterfaces \
     System.Text.Encodings.Web System.Text.Json System.Threading.Tasks.Dataflow \
     System.Reflection.MetadataLoadContext System.Resources.Extensions Microsoft.IO.Redist
```

Every library compiles with 0 errors. `System.Runtime.CompilerServices.Unsafe`
is IL: its `.il` goes through `cpp -P` (for upstream's `#include/#define`),
is assembled with the seed's `ilasm`, and gets the public key through
`.publickey`.

### 6.3 Roslyn 4.13 / 5.0 (C# 14)

```
PKGDIR=m612/pkgs bash repo/bootstrap/m612/roslyn.sh 4.13.0 m612/roslyn-4.12.0/csc.exe
PKGDIR=m612/pkgs ROSLYN_TAG=NET-SDK-10.0.100-preview.4.25258.110 bash repo/bootstrap/m612/roslyn.sh 5.0.0-p4 m612/roslyn-4.13.0/csc.exe
PKGDIR=m612/pkgs ROSLYN_TAG=Visual-Studio-2026-Version-18.0 bash repo/bootstrap/m612/roslyn.sh 5.0.0 m612/roslyn-5.0.0-p4/csc.exe
$ mono m612/roslyn-5.0.0/csc.exe -version
5.0.0 (m612 bootstrap) ()
C# 14 on Mono: 5 abab 42 0 7 12 List
```

The C# 14 test uses extension blocks (`extension(string s) {...}`, static
extension properties on `int`), the `field` keyword, null-conditional
assignment, `nameof(List<>)` and modifiers on simple lambda parameters.
4.13 → 5.0 directly fails on `extension(...)` blocks in 5.0's own source;
the 10.0-preview.4 Roslyn (already 5.0.0-prerelease, extension blocks in
preview) bridges it. 4.14+ moved `Core/CommandLine` into `Shared/`, so the
csc file list now comes from `CscCommandLine.projitems`, and
`src/Dependencies/Contracts` (polyfills) joined Microsoft.CodeAnalysis.

### 6.5 NuGet.Client 6.8.2.3 (`bootstrap/m612/nuget.sh 6.8.2.3 <csc> 17.8.49`)

Builds Microsoft.Web.XmlTransform 3.2.12 (dotnet/xdt), NuGet.Frameworks …
NuGet.Commands, NuGet.Resolver, NuGet.PackageManagement, NuGet.Build.Tasks
and Microsoft.Build.NuGetSdkResolver (net472, public-signed with the
MSShared key, version 6.8.2.3) with `IS_DESKTOP;IS_SIGNING_SUPPORTED` read
from NuGet's own `build/common.targets`; NuGet.Build.Tasks is compiled
against our MSBuild instead of the GAC's Microsoft.Build.Utilities.v4.0.
Test: a package made from source (our csc + a nuspec, zipped) in a local
folder feed, and an SDK-less PackageReference project:

```
$ mono m612/msbuild-17.8.49/tools/MSBuild.exe -nologo -m:1 -nr:false -t:Restore restoreme.csproj
  Determining projects to restore...
  Restored ...\nuget-6.8.2.3-restore\restoreme.csproj (in 278 ms).
obj/project.assets.json: "Bootstrap.Greeting/1.0.0"; packages/bootstrap.greeting/1.0.0/lib/net472/Bootstrap.Greeting.dll
```

### 6.6 .NET SDK tasks (`bootstrap/m612/sdk.sh 8.0.131 <csc> 17.8.49`)

dotnet/sdk v8.0.131 is the SDK that pins exactly MSBuild 17.8.49 and NuGet
6.8.2. `Microsoft.NET.Build.Tasks` (net472) is compiled from
`src/Tasks/Microsoft.NET.Build.Tasks` + `Tasks/Common` + the workload
manifest reader, with the SDK's implicit global usings plus the repo's
`<Using>` items. Layout `m612/dotnet-sdk-8.0.131/sdk/8.0.131/Sdks/Microsoft.NET.Sdk/{Sdk,targets,tools/net472}`,
a hand-written subset of `Microsoft.NETCoreSdk.BundledVersions.props`
(upstream generates it while assembling the whole product), and the RID
graph from runtime's `Microsoft.NETCore.Platforms/src/runtime.json`.
MSBuild finds it through `MSBuildSDKsPath` (environment; the wrapper
`m612/dotnet-sdk-8.0.131/msbuild-mono` sets it). Toolset defaults added:
`MSBuildEnableWorkloadResolver=false` (no workloads; the upstream
sentinel needs the dotnet SDK resolver, which needs native hostfxr),
`SuppressImplicitGitSourceLink=true` (dotnet/sourcelink not built), and an
empty `analyzers/build/Microsoft.CodeAnalysis.NetAnalyzers.props`
(dotnet/roslyn-analyzers not built).

The MSBuild layout was changed to standalone (`m612/msbuild-17.8.49/tools/`):
a `MSBuild/Current/Bin` path switches BuildEnvironmentHelper into
"Visual Studio" mode, which then wants NuGet under `Common7/IDE`.

```
$ MSBuildSDKsPath=...\sdk\8.0.131\Sdks mono m612/msbuild-17.8.49/tools/MSBuild.exe -nologo -m:1 -nr:false -restore sdkstyle.csproj
  Restored ...\sdk-8.0.131-sdkstyle\sdkstyle.csproj (in 136 ms).
  sdkstyle -> ...\bin\Debug\net472\sdkstyle.exe
$ mono bin/Debug/net472/sdkstyle.exe
Hello from an SDK-style project: SDK STYLE ON MONO
running on Mono
```

The `-v:diag` log shows the tasks loaded from our
`tools/net472/Microsoft.NET.Build.Tasks.dll`, csc started through
`csc-mono.cmd`, and no `C:\Windows\Microsoft.NET` path in use.

### 6.7 Targeting pack (`bootstrap/m612/refpack.sh <csc>`)

`plan` reads `NetCoreAppLibrary.props` (168 names, minus 4
`System.Private.*`), finds each library's `ref/` project or its shim
(`shims/*/ref` or `shims/*/src`, checked-in type-forward sources), follows
`ProjectReference`s (transitively) to get a build order, and adds the 20
`shims/stubs` assemblies (empty public types for forwards into OOB
packages, generated from their `ForwardedType` items like the upstream
`Directory.Build.targets` does). `refs` compiles 183 assemblies (~10 min on
Mono); `pack` writes `packs/Microsoft.NETCore.App.Ref/8.0.31/{ref/net8.0,data/FrameworkList.xml}`
and the `KnownFrameworkReference`/`KnownAppHostPack` items.

```
$ MSBuildSDKsPath=... mono .../MSBuild.exe -restore -p:NetCoreRoot=...\dotnet-sdk-8.0.131\ -p:NetCoreTargetingPackRoot=...\packs net8lib.csproj
  net8lib -> ...\refpack-8.0.0-net8lib\bin\Debug\net8.0\net8lib.dll
$ monodis --assemblyref net8lib.dll
1: Version=8.0.0.0 Name=System.Runtime  Public Key token: B0 3F 5F 7F 11 D5 0A 3A
2: Version=8.0.0.0 Name=System.Collections.Immutable ...
```

The library uses .NET 8 APIs (generic math `INumber<T>`,
`FrozenDictionary`, `TimeProvider`), so it really binds to the new
reference surface. It cannot run yet: there is no .NET runtime.

### 6.8 The host (`bootstrap/m612/corehost.sh`, MSYSTEM=CLANG64)

`src/native/corehost` of runtime v8.0.0 (commit 5535e31a71, read from the
tarball with `git get-tar-commit-id`), built with CLANG64 clang 22.1.8 / lld /
libc++ for x86_64-w64-mingw32, Ninja, -j4. The script extracts a separate
full tree `m612/src/runtime-8.0.0-native` (the refpack tree stays pristine),
applies `patches/m612/runtime-8.0.0/`, and writes the version headers that
Arcade normally generates (`artifacts/obj/{_version.h,runtime_version.h}`,
`hostResourceFiles/<name>/version_info.h`, version 8.0.0).

```
cmake -G Ninja -S src/native/corehost -B build/corehost-8.0.0/cmake -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCLR_CMAKE_TARGET_ARCH=x64 -DCLR_CMAKE_HOST_ARCH=x64 \
  -DCLI_CMAKE_PKG_RID=win-x64 -DCLI_CMAKE_FALLBACK_OS=win -DCLI_CMAKE_COMMIT_HASH=5535e31a71... \
  -DCLI_CMAKE_RESOURCE_DIR=.../hostResourceFiles -DCLR_CMAKE_KEEP_NATIVE_SYMBOLS=ON
ninja    # 112 steps, no errors
```

Patches (git format, `patches/m612/runtime-8.0.0/`):

1. `eng/native`: a GNU-driver branch for Windows hosts keyed on `MINGW`.
   No `/Ox` etc. (the Unix `-O` flags instead), no MASM/`mc` requirement,
   `-static` (libc++, libunwind, winpthreads linked in, like the MSVC
   build's static CRT), no `lib` prefix on DLLs, `-municode` for the
   `wmain` executables. With it, neither `-DMC=` nor an MASM assembler is
   needed for the host.
2. `corehost/CMakeLists.txt`: skip `ijwhost` (C++/CLI only, MASM) and the
   host tests (C++/CLI, midl) with MinGW; `/MANIFEST:NO` only with MSVC.
3. Seven MSVC-only constructs: `_SH_DENYNO` needs `<share.h>`;
   `L ## __FUNCTION__`; function pointer → `void*`; `char_t` keyed on
   MSVC's `_WCHAR_T_DEFINED`; `std::ifstream(std::wstring)`; a narrowing
   HRESULT in braces.

Plus one managed patch, `patches/m612/runtime-8.0.0-managed/0001`, applied
by `netfx-pkgs.sh` to the shared tree: Microsoft.NET.HostModel's
`ResourceUpdater` maps the apphost with a capacity larger than the file;
.NET grows the file, Mono's `MemoryMappedFile.CreateFromFile` throws
`ArgumentException: capacity`, so the SDK's `CreateAppHost` task failed.
The fix sets the length first. (`netfx-pkgs.sh` now also extracts
`src/installer/managed` itself; it had been extracted by hand.)

Output (stripped, each depends only on KERNEL32/ADVAPI32/USER32/SHELL32/ole32/
WINTRUST and the OS UCRT `api-ms-win-crt-*`): `dotnet.exe` 411 KB,
`hostfxr.dll` 1.0 MB, `hostpolicy.dll` 1.0 MB, `apphost.exe` 834 KB,
`nethost.dll` + `libnethost.a`, `comhost.dll`, headers. Installed into the
SDK as `dotnet.exe`, `host/fxr/8.0.0/hostfxr.dll` and
`packs/Microsoft.NETCore.App.Host.win-x64/8.0.31/runtimes/win-x64/native/`
(the version `KnownAppHostPack` names). `hostpolicy.dll` belongs to the
runtime (`shared/Microsoft.NETCore.App/<ver>/`) and stays in
`m612/corehost-8.0.0/corehost` until there is one. Not built:
`singlefilehost` (links `coreclr_static`, so it needs CoreCLR) and
`ijwhost`.

Tests (`corehost.sh test`):

```
$ dotnet-sdk-8.0.131/dotnet.exe --info
Host:
  Version:      8.0.0
  Architecture: x64
  Commit:       5535e31a71
  RID:          win-x64
.NET SDKs installed:
  No SDKs were found.
.NET runtimes installed:
  No runtimes were found.
...
$ MSBuild.exe -restore ... tests/apphost/apphost.csproj      # net8.0 Exe, UseAppHost=true
  apphost -> ...\bin\Debug\net8.0\apphost-test.dll           # + apphost-test.exe, placeholder replaced
$ DOTNET_ROOT=...\dotnet-sdk-8.0.131 bin/Debug/net8.0/apphost-test.exe
You must install or update .NET to run this application.
App: ...\apphost-test.exe
Framework: 'Microsoft.NETCore.App', version '8.0.31' (x64)
.NET location: ...\dotnet-sdk-8.0.131
No frameworks were found.                                    # exit 0x80008096
$ fakeroot/dotnet.exe apphost-test.dll     # shared/Microsoft.NETCore.App/8.0.31 = hostpolicy.dll + deps.json only
Could not resolve CoreCLR path.                              # exit 0x80008087
  trace: Loaded library from ...\fakeroot\host\fxr\8.0.0\hostfxr.dll
         Loaded library from ...\fakeroot\shared\Microsoft.NETCore.App\8.0.31\hostpolicy.dll
```

The `.dll` is a framework-dependent net8.0 app; it needs a runtime (S9/S10b).
The SDK still runs on MSBuild/Mono: it has no `sdk/<ver>/dotnet.dll`, so
`dotnet build` does not exist yet, and the SDK resolver
(Microsoft.DotNet.MSBuildSdkResolver, which would call `hostfxr`) is not built.

The Mono flavor of the runtime, built from the same patched tree, is in §6.9.

### 6.9 The Mono runtime (`bootstrap/m612/mono-runtime.sh`, MSYSTEM=CLANG64)

```
env MSYSTEM=CLANG64 bash repo/bootstrap/m612/mono-runtime.sh   # steps: prep configure build install nativelibs test
```

Runs after `corehost.sh` (chain step 19): same tree
(`m612/src/runtime-8.0.0-native`, patches 0001–0007), same version headers
in `artifacts/obj`. The script calls the CMake project of `src/mono` with
the options `src/mono/mono.proj` uses for `TargetsWindows`:
`-DMONO_SHARED_LIB_NAME=coreclr` (the host loads the Mono VM as
`coreclr.dll`), `-DGC_SUSPEND=hybrid`, `-DMONO_COMPONENTS_RID=windows-x64`,
the mono.proj CPPFLAGS (`-DWIN32 -DWIN64 -DWIN32_THREADS -D_WIN32_WINNT=0x0601
-DUNICODE -DFD_SETSIZE=1024 -DNVALGRIND …`), plus `-DSTATIC_COMPONENTS=1`
(debugger, hot_reload, diagnostics_tracing and marshal-ilgen are linked into
`coreclr.dll` instead of `mono-component-*.dll`). Step `nativelibs` builds
`src/native/libs`, which on Windows is only `System.IO.Compression.Native`
(zlib-intel + brotli); System.Globalization.Native is compiled into the
runtime (it loads Windows' own `icu.dll`), and System.Native and the
crypto/net shims are Unix-only.

Patches (`patches/m612/runtime-8.0.0/`):

| Patch | What |
|---|---|
| 0004 mono: build with a MinGW toolchain | MSVC options (`/W4 /WX /EHsc /FC /Oi /GF /Zi /GL`, `/LTCG /DEBUG /OPT:* /STACK`) only with MSVC; MinGW gets `-Wl,--stack,0x800000`, `-static`, no `lib` prefix. `MONO_KEYWORD_THREAD` = `__thread` (clang/MinGW uses native PE TLS). `win64.asm` only with MSVC: with a GNU compiler `mono-context.h`/`mono-threads-coop.c` already use inline asm and `__builtin_unwind_init`, so no MASM (and no llvm-ml) is needed. eglib `G_GNUC_NORETURN` on Windows (clang says `__GNUC_MINOR__` 2, so `eg_unreachable()` is empty). `OS_LIBS` without `.lib`, `--icf=none` for `/OPT:NOICF` |
| 0005 mono: clear the POSIX configure results on MinGW | configure finds MinGW's POSIX layer; with the patch `config.h` has the same `HAVE_*` set an MSVC build gets (no pthread/unistd/sys/time/dirent/getopt/semaphore, no clock_gettime, mkstemp, vasprintf, struct timeval, tls_model …), so every `HOST_WIN32` path is the one MSVC builds |
| 0006 System.Globalization.Native: ICU C headers with MinGW | MinGW has no Windows SDK `icu.h`; use ICU's own C headers (MSYS2 `mingw-w64-clang-x86_64-icu`, headers only) with `U_DISABLE_RENAMING`, `U_SHOW_CPLUSPLUS_API 0`, `U_HIDE_DEPRECATED_API` (as `icu.h`). Functions are still resolved from `icu.dll` at run time |
| 0007 System.IO.Compression.Native, zlib: build with MinGW | `FAST_FAIL_HEAP_METADATA_CORRUPTION` (50) missing from MinGW's `winnt.h`; zlib-intel `crc_folding.c` / `deflate_quick.c` need `-mpclmul -msse4.2` (used after the CPUID check, as in zlib-intel's own Makefile); PDB install only with MSVC; windres needs `VER_FILEDESCRIPTION_STR` quoted once more |

No component needed MSVC-only constructs: the `_MSC_VER` code in `src/mono`
(MSVC inline asm, `__assume`, `#pragma` exports for x86) all has a GNU
branch, which is the one legacy Mono used on MinGW.

Output (`m612/mono-8.0.0`):

- `lib/coreclr.dll` (4.2 MB with `--strip-debug`; 25 MB with `-g` in
  `build/mono-8.0.0/mono/mono/mini/`). Imports: KERNEL32, USER32, SHELL32,
  ole32, OLEAUT32, WS2_32, WINMM, bcrypt and the `api-ms-win-crt-*` UCRT;
  exports `coreclr_initialize`/`_execute_assembly`/`_shutdown(_2)`/
  `_create_delegate`/`_set_error_writer` and the `mono_*` embedding API;
  has a `.tls` section.
- `lib/libmonosgen-2.0.a`, `lib/libmono-component-*-static.a`,
  `lib/coreclr.import.lib` (import library), `include/mono-2.0/`.
- `bin/mono-sgen.exe` (the Mono driver, statically linked).
- `libs/System.IO.Compression.Native.dll` (+ `.dll.a`, static `.a`): the
  18 exports of its `.def`, imports only KERNEL32 and the UCRT, version
  resource "System.IO.Compression.Native".

Tests (`mono-runtime.sh test`):

```
$ ziptest.exe          # tests/mono/ziptest.c against System.IO.Compression.Native.dll
deflate level 6: 200000 -> 5461 -> 200000 bytes
deflate level 1: 200000 -> 5461 -> 200000 bytes
deflate level 9: 200000 -> 5007 -> 200000 bytes
crc32("123456789") = cbf43926 (expect cbf43926)
brotli: 200000 -> 3597 -> 200000 bytes
OK
$ mono-8.0.0/bin/mono-sgen.exe --version
Mono JIT compiler version 8.0.0.0 (8.0.0)
	TLS:           __thread
	SIGSEGV:       normal
	Architecture:  amd64
	Misc:          softdebug
	Interpreter:   yes
	Suspend:       hybrid
	GC:            sgen (concurrent by default)
$ MONO_LOG_LEVEL=debug MONO_LOG_MASK=asm fakeroot/dotnet.exe apphost-test.dll
#   fakeroot/shared/Microsoft.NETCore.App/8.0.31 = hostpolicy.dll + coreclr.dll (Mono) + deps.json
Mono: netcore preload hook: did not find 'System.Private.CoreLib'.
Mono: Assembly Loader probing location: 'System.Private.CoreLib.dll'.
* Assertion at .../src/mono/mono/metadata/assembly.c:2718, condition `corlib' not met
  trace: CoreCLR path = '...\shared\Microsoft.NETCore.App\8.0.31\coreclr.dll'
         Loaded library from ...\shared\Microsoft.NETCore.App\8.0.31\coreclr.dll
```

Before this step the host stopped at "Could not resolve CoreCLR path". Now
hostpolicy loads `coreclr.dll`, calls `coreclr_initialize`, Mono starts up
(GC, threads, TLS, JIT init) and asks for System.Private.CoreLib.

Not verified yet, because it needs CoreLib: JIT-compiled managed code,
exception handling through JIT frames (unwind info registered with
`RtlAddGrowableFunctionTable`), hybrid suspend, `__thread` across
threads, and ICU through `icu.dll`. The build log has 214 `-Wformat`
warnings (`%u` for `DWORD`, the same size on LLP64) and a few minor ones;
none were made errors upstream.

### 6.10 System.Private.CoreLib, the libraries and the shared framework (S9)

```
bash repo/bootstrap/m612/vbc.sh 4.8.0                                  # chain step 20 (VB compiler, for Microsoft.VisualBasic.Core)
bash repo/bootstrap/m612/corelib.sh m612/roslyn-4.8.0/csc.exe          # chain step 21; steps: gen events compile
bash repo/bootstrap/m612/libraries.sh m612/roslyn-4.8.0/csc.exe        # chain step 22; steps: plan gens build vb report
TESTS="hello smoke" bash repo/bootstrap/m612/framework.sh              # chain step 23; steps: layout install test
```

Everything is compiled from the patched tree `m612/src/runtime-8.0.0-native`
(the one `coreclr.dll` was built from, so CoreLib's icalls and the runtime's
struct layouts match) with **Roslyn 4.8.0**, the compiler runtime 8.0.0
builds with (`MicrosoftNetCompilersToolsetVersion` 4.8.0-3, `LangVersion`
preview). No patch to the managed sources was needed.

**CoreLib** (`corelib.sh`, ~1 min). `src/mono/System.Private.CoreLib/System.Private.CoreLib.csproj`
plus the shared `System.Private.CoreLib.Shared.projitems`, evaluated by
`csproj_items.py` with `TargetOS=windows Platform=x64 Configuration=Release`:
1772 files, defines `MONO NETCOREAPP SYSTEM_PRIVATE_CORELIB TARGET_AMD64
MONO_FEATURE_SRE FEATURE_MANAGED_ETW FEATURE_MANAGED_ETW_CHANNELS
FEATURE_PERFTRACING TARGET_64BIT TARGET_WINDOWS` (+ the SDK's `NET*`
symbols; no `DEBUG`/`TRACE`, the project sets
`DisableImplicitConfigurationDefines`). Generated inputs, all from source:

- `NativeRuntimeEventSource.Mono.cs`: `src/coreclr/scripts/genRuntimeEventSources.py --runtimeflavor mono` over `ClrEtwAll.man`;
- `SR`: `Strings.resx` → `ResxGen --corelib` (manifest `System.Private.CoreLib.Strings.resources`, the projitems' `StringResourcesName`);
- source generators as analyzers: the **EventSourceGenerator** (`System.Private.CoreLib.Generators`, new `netfx-pkgs.sh` recipe) and the LibraryImportGenerator (`LibraryImportGenerator_UseMarshalType=true`, `.NETCoreApp v8.0` in a `.globalconfig`);
- assembly attributes (version 8.0.0.0, informational `8.0.0+<commit>`, `TargetFramework`).

It is public-signed with the **SilverlightPlatform** key (`7cec85d7bea7798e`).
The only warnings are 30× `AD0001` from `IntrinsicsInSystemPrivateCoreLibAnalyzer`,
the known upstream issue (dotnet/runtime#90356, suppressed in the csproj).
Result: `m612/netcoreapp-8.0.0/lib/System.Private.CoreLib.dll`, 4.7 MB of IL.

**Libraries** (`libraries.sh`, ~35 min for all on Mono).

- `plan` (`tools/libplan.py`, 1 s): reads `NetCoreAppLibrary.props` (168
  names), evaluates each `src/<Name>.csproj` once for `net8.0-windows` if the
  project has that target framework (35 do), else `net8.0` (72 do); the 59
  names without a src project are the checked-in shims
  (`src/libraries/shims/*/src`). Per project it records Compile items,
  defines, `Reference` items (→ the reference pack of §6.7), `ProjectReference`s
  (→ other implementation assemblies or CoreLib), analyzer references,
  embedded resources, `AssemblyAttribute` items and properties, and computes
  the build order.
- `gens`: the generators the projects use, built from source by `netfx-pkgs.sh`
  recipes: LibraryImportGenerator (every src project that references
  System.Runtime.InteropServices or CoreLib, as in `eng/generators.targets`),
  **ComInterfaceGenerator**, **System.Text.RegularExpressions.Generator**
  (`[GeneratedRegex]` in System.Private.Xml, System.Data.Common,
  System.ComponentModel.TypeConverter, System.Private.DataContractSerialization).
- `build`: one csc command line per assembly, with the same identity (name,
  version, key) as its reference assembly. What Arcade/eng would add is added
  by the script: `FxResources.<Asm>.SR` + `Common/src/System/SR.cs`,
  `SkipLocalsInit.cs`, `DefaultDllImportSearchPaths(AssemblyDirectory|System32)`,
  `CLSCompliant`, `NeutralResourcesLanguage`, the SDK defines (`TRACE RELEASE NET8_0…`,
  `WINDOWS` for `-windows`). **Partial facades** (`IsPartialFacadeAssembly`,
  34 projects, e.g. System.Runtime with 856 forwards, System.Runtime.InteropServices
  130, System.Xml.ReaderWriter 187) are compiled twice: `tools/genfacades.py`
  (GenFacades without Arcade) lists the public types of the contract that the
  first build does not define and that a compile reference does (`monodis --typedef`),
  and writes `[assembly: TypeForwardedTo(typeof(...))]` for the second build.
  56 shims are the ones `refpack.sh` compiled; `System`, `System.Data` and
  `mscorlib`, whose ref-pack versions come from `shims/*/ref`, are compiled
  from `shims/*/src` plus the contract's `shims/*/ref/<name>.cs` (as
  `shims/Directory.Build.props` does; fixed in S11, §6.11) against the
  implementation assemblies (they also forward implementation-only
  serialization types such as `TreeSet<T>`).
  One PlatformNotSupported assembly: System.Runtime.InteropServices.JavaScript
  (browser only) is its reference source with `throw null` → `throw new PlatformNotSupportedException()`
  (Arcade's GenAPI-based PNSE generator does the same).
- `vb`: **Microsoft.VisualBasic.Core** (13.0.0.0, the only VB project) with the
  VB compiler of `vbc.sh`; `tools/vbresx.py` writes its VB `SR`.

Result: **168 managed assemblies** (CoreLib + 166 C# + 1 VB), 24 MB of IL in
`m612/netcoreapp-8.0.0/lib/`. Nothing is skipped. Not done (not needed to run):
ReadyToRun, XML doc files. (The embedded `ILLink.*.xml` descriptors were
added in S11, §6.11.)

Problems found and fixed on the way (tools, not sources):
`csproj_items.py` ignored `Exclude=` (System.Net.Quic got both its real and
its `*.Unsupported.cs` files); the Regex generator needs System.Buffers
4.0.5.0, which csc 4.8's directory does not have (passed as an extra
`-analyzer` path so Roslyn's analyzer loader resolves it); csc on Mono does
not create nested `-generatedfilesout` directories; and the reference pack
had **Microsoft.VisualBasic.Core 8.0.0.0** instead of 13.0.0.0 (its
`Directory.Build.props` adds 5 to `MajorVersion`): `refpack.sh` now
evaluates the library's `Directory.Build.props` (`csproj_items --pre-local --init`,
`[MSBuild]::Add`), and the VB ref and the `Microsoft.VisualBasic` shim were rebuilt.

**VB compiler** (`vbc.sh 4.8.0`). The VB compiler is written in VB, so the
seed's `vbc.exe` (Roslyn 3.9, in the MSYS2 mono package next to the seed csc)
compiles, once, the VB syntax and error-facts generators and
`Microsoft.CodeAnalysis.VisualBasic` 4.8.0 from the roslyn-4.8.0 tree
(regenerated `Syntax.xml.*.Generated.vb`, `BoundNodes.xml.Generated.vb`
(with the C# BoundTreeGenerator), `ErrorFacts.Generated.vb`: identical to the
checked-in files, which are not used). `vbc.exe` is C# (our csc 4.8).
Settings as Roslyn's: `-langversion:latest -removeintchecks+`, the SDK's
default VB imports, `-vbruntime*`. Test: a VB program with LINQ/lambdas
compiles and runs on Mono.

**Framework** (`framework.sh`). `shared/Microsoft.NETCore.App/8.0.0/` =
the 168 assemblies + `coreclr.dll` (Mono, §6.9) + `System.IO.Compression.Native.dll`
+ `hostpolicy.dll` (§6.8) + a `Microsoft.NETCore.App.deps.json` in the
upstream shape (`runtimeTarget .NETCoreApp,Version=v8.0/win-x64`, one
`Microsoft.NETCore.App.Runtime.win-x64/8.0.0` package with `runtime`
(assembly/file versions from the metadata) and `native` sections).
Upstream ships no `.runtimeconfig.json` for Microsoft.NETCore.App itself
(only frameworks layered on it have one), so neither do we. It is installed
into the SDK root next to `dotnet.exe`/`host/fxr`, and the SDK's
`KnownFrameworkReference` now says `DefaultRuntimeFrameworkVersion` /
`LatestRuntimeFrameworkVersion` = 8.0.0, so apps' `runtimeconfig.json` ask
for 8.0.0 (the reference/apphost packs keep their 8.0.31 version numbers).

Tests (`framework.sh test`; the apps are built by our SDK on MSBuild/Mono 6.12,
then run by `dotnet.exe` → hostfxr → hostpolicy → coreclr.dll):

```
$ dotnet-sdk-8.0.131/dotnet.exe --list-runtimes
Microsoft.NETCore.App 8.0.0 [E:\msys64\home\topkek\dotnetboot\m612\dotnet-sdk-8.0.131\shared\Microsoft.NETCore.App]
$ dotnet-sdk-8.0.131/dotnet.exe test/fx-8.0.0/hello/bin/Release/net8.0/hello.dll     # tests/hello/Program.cs
Hello, World! .NET 8.0.0 on Microsoft Windows 10.0.26200 (X64)
CoreLib: E:\msys64\home\topkek\dotnetboot\m612\dotnet-sdk-8.0.131\shared\Microsoft.NETCore.App\8.0.0\System.Private.CoreLib.dll
caught InvalidOperationException: deep; frames: 52
finally ran
rethrown DivideByZeroException
NullReferenceException caught (hardware fault -> managed exception)
4 threads x 1e6 increments = 4000000
tasks: 204 (thread pool, async/await)
thread pool work item: True
invariant mode: False
ToUpper: STRAßE ISTANBUL / tr: İSTANBUL / invariant: ISTANBUL
de-DE: 1.234.567,89 Freitag, 9. Oktober 2026
ja-JP: 2026年10月9日金曜日
compare ä/z sv-SE: 1, de-DE: -1
IndexOf ignore case: -1, normalization: 1
current culture: en-GB, time zone: W. Europe Standard Time
rc=0
$ dotnet-sdk-8.0.131/dotnet.exe test/fx-8.0.0/smoke/bin/Release/net8.0/smoke.dll --net   # tests/smoke/Program.cs (two runs merged: --net adds the https line; code pages/VB were added later)
[ok]   file IO: 1001 lines, 16898 bytes, 1 file(s), deleted=True
[ok]   LINQ: 0:392,1:343
[ok]   LINQ expression compile: 50
[ok]   Regex (interpreted): 10
[ok]   Regex (compiled, Reflection.Emit): HELLO WORLD / 4
[ok]   System.Text.Json: {"Name":"m612","Values":[1,2,3],"When":"2026-10-09T00:00:00"} -> 3 values, Name=m612
[ok]   XML (System.Private.Xml + Linq): 3
[ok]   Process: 'from-child' exit=0, self=dotnet
[ok]   sockets (loopback TCP): echo 'ping!' via port 50362
[ok]   HttpClient (loopback HttpListener): hi from HttpListener
[ok]   DNS: InterNetworkV6,InterNetwork
[ok]   HttpClient https (internet): 200 577
[ok]   Reflection.Emit: 1234
[ok]   compression (System.IO.Compression.Native): 12000 -> gzip 108, brotli 23, roundtrip True
[ok]   crypto (SHA256, AES via CNG): BA7816BF8F01CFEA... aes 48 bytes
[ok]   GC + finalizers + weak refs: weak target collected=False, gen2=2, heap=5209 KB
[ok]   code pages (System.Text.Encoding.CodePages, codepages.nlp): € 4
[ok]   Microsoft.VisualBasic.Core (VB, late binding): basic 42 -1073.64 True
SMOKE OK
```

This exercises what §6.9 could not: JIT-compiled managed code, exception
handling through JIT frames (filters, finally, rethrow, 50-deep unwinding,
an access violation turned into `NullReferenceException`), threads, the
thread pool and async/await (hybrid suspend, `__thread` TLS), globalization
through Windows' `icu.dll` (Turkish casing, Swedish collation, Japanese
dates), P/Invoke into the OS (kernel32, ws2_32, bcrypt/CNG, SChannel for
TLS) and into `System.IO.Compression.Native.dll`, Reflection.Emit (compiled
Regex, LINQ expression trees), and child processes. No runtime crash was
seen; no runtime patch beyond 0001–0007 was needed. Two notes: the weak
reference target survives `GC.Collect()` because SGen scans stacks
conservatively (Mono behaviour, not a bug); and `"Straße".IndexOf("SS",
CurrentCultureIgnoreCase)` is -1 under en-GB (not compared against CoreCLR+ICU yet).

### 6.11 S11: the SDK's own tools as net8.0 assemblies (`dotnet build` without Mono 6.12)

```
OOB="…" bash repo/bootstrap/m612/libraries.sh m612/roslyn-4.8.0/csc.exe        # chain 24: out-of-band libraries
bash repo/bootstrap/m612/netfx-pkgs.sh m612/roslyn-4.8.0/csc.exe m612/pkgs \
     System.Text.Json.SourceGeneration Microsoft.Interop.JavaScript.JSImportGenerator   # chain 25
bash repo/bootstrap/m612/refpack.sh m612/roslyn-4.8.0/csc.exe pack           # chain 26: + analyzers/dotnet/cs
bash repo/bootstrap/m612/roslyn-net8.sh 4.8.0                               # chain 27: csc.dll vbc.dll VBCSCompiler.dll
bash repo/bootstrap/m612/msbuild-net8.sh 17.8.49                            # chain 28: MSBuild.dll
bash repo/bootstrap/m612/roslyn-net8.sh 4.8.0 tasks                         # chain 29: Microsoft.Build.Tasks.CodeAnalysis
bash repo/bootstrap/m612/clideps-net8.sh                                    # chain 30
bash repo/bootstrap/m612/nuget-net8.sh 6.8.2.3                              # chain 31
bash repo/bootstrap/m612/clideps-net8.sh templating                         # chain 32
bash repo/bootstrap/m612/sourcelink-net8.sh                                 # chain 33
bash repo/bootstrap/m612/sdk-net8.sh                                        # chain 34: dotnet.dll, resolvers, SDK tasks
bash repo/bootstrap/m612/illink-net8.sh                                     # chain 35: Cecil, illink, ILLink.Tasks
bash repo/bootstrap/m612/sdk-layout-net8.sh                                 # chain 36: m612/dotnet-sdk-8.0.131-net8
bash repo/bootstrap/m612/sdk-net8-test.sh                                   # chain 37: the end-to-end proof
```

Everything is still compiled by the net472 compilers on Mono 6.12 (Roslyn 4.8 for
the SDK, as SDK 8.0.1xx pins `4.8.0-7`; MSBuild with 4.12 as in §6.4), but now
against the from-source **reference pack** (§6.7) instead of Mono's 4.7.2-api, and
the result runs on the from-source runtime (§6.9/§6.10). The new SDK lives in
`m612/dotnet-sdk-8.0.131-net8/`; the mono-based `m612/dotnet-sdk-8.0.131/` is kept
as the fallback and stage-0 tool (it supplies `dotnet.exe`, `host/`, `shared/` and
`packs/`, which the layout copies).

**Method.** One new driver for "an SDK-style project → one csc command line":
`sdkproj.sh` + `tools/sdkproj.py`. It evaluates the real `.csproj` (with the repo's
`Directory.Build.props` when needed) for one target framework and implements the
SDK conventions these repos rely on: default `**/*.cs`/`**/*.resx` items with
`Compile Remove`/`Update` applied to them, manifest resource names
(`CreateCSharpManifestResourceName`: `LogicalName`, the `DependentUpon` .cs
convention, `RootNamespace` + directory names made identifiers), Arcade's
`GenerateResxSource` (`GenerateSource`/`Namespace`/`ClassName`, repo default
`GenerateResxSource=true`, a checked-in designer class of the same name wins),
`ImplicitUsings` + `<Using>` items, `InternalsVisibleTo` items, `GenerateAssemblyInfo`,
`AllowUnsafeBlocks`/`Nullable`/`LangVersion`/`NoWarn`, and the source generators the
reference pack hands to every net8.0 compile. References are the reference pack
+ the out-of-band libraries + the given package assemblies; for dotnet/sdk, where
projects compile other projects' sources in (the resolvers, the template locator),
`SP_EXPLICIT=1` restricts project references to the transitive `ProjectReference`
closure (a registry of what sdkproj built), as MSBuild would.

**What was built** (all net8.0 unless noted, public-signed with the upstream keys):

| Component | Source | Assemblies | Script |
|---|---|---|---|
| out-of-band libraries | runtime v8.0.0 `src/libraries/*/src` | 24: ConfigurationManager, EventLog, ProtectedData, MetadataLoadContext, Resources.Extensions, CodeDom, Cryptography.Pkcs/Xml, ServiceController, Microsoft.Extensions.{Primitives, FileProviders.Abstractions, FileSystemGlobbing, DependencyModel, Logging(+Abstractions, Console, Configuration), Options(+ConfigurationExtensions), DependencyInjection(+Abstractions), Configuration(+Abstractions, Binder)} | `libraries.sh` with `OOB=` (`m612/oob-8.0.0/lib`) |
| Roslyn 4.8.0 | roslyn tree of roslyn.sh/vbc.sh | Microsoft.CodeAnalysis, .CSharp, .VisualBasic (VB, by the from-source vbc), .ExternalAccess.RazorCompiler, csc.dll, vbc.dll, VBCSCompiler.dll, Microsoft.Build.Tasks.CodeAnalysis | `roslyn-net8.sh` (`m612/roslyn-4.8.0-net8/{bincore,tasks}`) |
| MSBuild 17.8.49 | msbuild tree of msbuild.sh | Microsoft.NET.StringTools, Microsoft.Build.Framework, .Utilities.Core, Microsoft.Build, .Tasks.Core, MSBuild.dll | `msbuild-net8.sh` (`DotNetBuildFromSource=true`, `RUNTIME_TYPE_NETCORE`, `FEATURE_NODE_REUSE`, `FEATURE_PIPEOPTIONS_CURRENTUSERONLY` …) |
| NuGet.Client 6.8.2.3 | nuget tree of nuget.sh | NuGet.{Frameworks, Common, Configuration, Versioning, Packaging, LibraryModel, Protocol, DependencyResolver.Core, ProjectModel, Credentials, Commands, Resolver}, NuGet.Build.Tasks, NuGet.Build.Tasks.Console, Microsoft.Build.NuGetSdkResolver, NuGet.Build.Tasks.Pack | `nuget-net8.sh` |
| third-party / satellite repos | Newtonsoft.Json 13.0.3 (its net6.0 flavour), runtime HostModel, deployment-tools Releases, command-line-api `02fe27cd` (System.CommandLine 2.0.0-beta4.23307.1), ApplicationInsights 2.21.0, templating v8.0.131 (TemplateEngine.{Abstractions, Utils, Core.Contracts, Core, Edge, Orchestrator.RunnableProjects, IDE}, TemplateSearch.Common) | 14 | `clideps-net8.sh` (`m612/net8-pkgs`) |
| dotnet/sdk v8.0.131 | sdk tree of sdk.sh | Microsoft.DotNet.{InternalAbstractions, Cli.Utils, NativeWrapper, SdkResolver, MSBuildSdkResolver, Configurer, Cli.Sln.Internal, TemplateLocator}, Microsoft.NET.Sdk.{WorkloadManifestReader, WorkloadMSBuildSdkResolver}, Microsoft.Win32.Msi, Microsoft.TemplateEngine.Cli (+ Wcwidth 1.0.0 sources), **dotnet.dll**, Microsoft.NET.Build.Tasks | `sdk-net8.sh` (`IncludeAspNetCoreRuntime=false`) |
| dotnet/sourcelink 8.0.0 | public tag | Microsoft.Build.Tasks.Git, Microsoft.SourceLink.{Common, GitHub, GitLab, AzureRepos.Git, Bitbucket.Git} (their `tools/core`) | `sourcelink-net8.sh` |
| ILLink | runtime v8.0.0 `src/tools/illink`, dotnet/cecil `45dd3a7` | Mono.Cecil{,.Pdb,.Mdb,.Rocks} 0.11.4, illink.dll, ILLink.Tasks, ILLink.RoslynAnalyzer → `Microsoft.NET.ILLink.Tasks.8.0.0.nupkg` | `illink-net8.sh` |

Build times (Mono 6.12 csc, 4 cores, mostly one compile at a time): the OOB
libraries ~8 min, Roslyn ~3 min, MSBuild 1.5 min, NuGet 4.6 min, the sdk repo
3 min, the rest well under a minute each. Nothing needed a source patch except the
compiler server (below).

**Layout** (`sdk-layout-net8.sh`, the upstream shape): `dotnet.exe`, `host/fxr`,
`shared/Microsoft.NETCore.App/8.0.0`, `packs/` (Ref + Host + the new runtime pack);
`sdk/8.0.131/` = `dotnet.dll` + `dotnet.runtimeconfig.json`, the CLI and NuGet
libraries, `MSBuild.dll` + its targets + `Current/`, `Roslyn/` (Csc task, targets)
and `Roslyn/bincore/` (`csc.dll`, `vbc.dll`, `VBCSCompiler.dll` + runtimeconfigs),
`Sdks/` (Microsoft.NET.Sdk with `tools/net8.0/Microsoft.NET.Build.Tasks.dll`,
NuGet.Build.Tasks.Pack/CoreCLR, the SourceLink SDKs), `SdkResolvers/`
(Microsoft.DotNet.MSBuildSdkResolver, which calls the from-source hostfxr; the NuGet
SDK resolver by manifest), `Microsoft.NETCoreSdk.BundledVersions.props` (+
`NetCoreRoot`/`NetCoreTargetingPackRoot`, which dotnet/installer's generator adds,
and `KnownILLinkPack`), the RID graphs, `trustedroots/*.pem`, `.version`;
`templates/8.0.131/` (the common project/item templates zipped from
`template_feed/`); `library-packs/` (the ILLink nupkg; the SDK adds this folder as a
restore source itself). 828 files, 113 MB. `dotnet.exe` → hostfxr finds
`sdk/8.0.131/dotnet.dll` by the usual muxer route; `dotnet build` runs MSBuild
in-proc in `dotnet.dll`, out-of-proc nodes as `dotnet MSBuild.dll /nodemode:1`, and
the compiler as `dotnet exec VBCSCompiler.dll` (shared compilation).

**End-to-end proof** (`sdk-net8-test.sh`; DOTNET_CLI_TELEMETRY_OPTOUT=1, a fresh
`DOTNET_CLI_HOME` and NuGet cache; Mono 6.12 is not involved: no `mono` process runs):

```
$ dotnet-sdk-8.0.131-net8/dotnet.exe --info                          # 0.8 s
.NET SDK:
 Version:           8.0.131
 Workload version:  8.0.100-manifests.e3b0c442
Runtime Environment:
 OS Name:     Windows   OS Version:  10.0.26200   RID: win-x64
 Base Path:   E:\msys64\home\topkek\dotnetboot\m612\dotnet-sdk-8.0.131-net8\sdk\8.0.131\
Host:
  Version:      8.0.0   Architecture: x64   Commit: 5535e31a71
.NET SDKs installed:
  8.0.131 [E:\msys64\home\topkek\dotnetboot\m612\dotnet-sdk-8.0.131-net8\sdk]
.NET runtimes installed:
  Microsoft.NETCore.App 8.0.0 [E:\msys64\home\topkek\dotnetboot\m612\dotnet-sdk-8.0.131-net8\shared\Microsoft.NETCore.App]

$ dotnet new console -o hello                                         # 3.2-4.0 s (incl. restore)
The template "Console App" was created successfully.
  Restored ...\new\hello\hello.csproj (in 275 ms).
$ dotnet build                                                        # 2.9-3.3 s; incremental 2.6-3.2 s
MSBuild version 17.8.49+m612.boot for .NET
  hello -> ...\new\hello\bin\Debug\net8.0\hello.dll
Build succeeded.
$ dotnet run                                                          # 2.8-3.2 s
Hello, World!
$ DOTNET_ROOT=... bin/Debug/net8.0/hello.exe                          # the apphost
Hello, World!

$ dotnet pack -c Release -o ../feed          # greeting/ (classlib)   # 3.3 s, NuGet.Build.Tasks.Pack
  Successfully created package '...\feed\Bootstrap.Greeting.2.0.0.nupkg'.
$ dotnet build                               # consumer/: PackageReference Bootstrap.Greeting 2.0.0,
  Restored ...\consumer\consumer.csproj      #   NuGet.Config <clear/> + the local feed only;  3.2 s
$ dotnet run --no-build
hello from a source-built package (2.0.0.0)
runtime: .NET 8.0.0 (Mono VM)

$ dotnet new sln; dotnet sln add LibA LibB App   # B -> A, App -> B, A
$ dotnet build multi.sln -m:4                     # 6.5 s cold (nodes start), 4.0 s; no-op 2.9 s
$ dotnet App/bin/Debug/net8.0/App.dll
sum(1..100) = 5050
#   while building: dotnet.exe MSBuild.dll /nologo /nodemode:1 /nodeReuse:true /low:false  (x2)
#                   dotnet.exe exec ...\Roslyn\bincore\VBCSCompiler.dll "-pipename:juhq..."

$ dotnet build -c Release roslyncore/Microsoft.CodeAnalysis.csproj   # Roslyn's Microsoft.CodeAnalysis 4.8,
  Microsoft.CodeAnalysis -> ...\bin\Release\net8.0\Microsoft.CodeAnalysis.dll   # ~1500 files, 2.9 MB
$ dotnet exec cc/csc.dll ... h.cs     # a copy of bincore with that Microsoft.CodeAnalysis.dll
compiled by a Microsoft.CodeAnalysis that the source-built SDK built: 8.0.0.0

$ dotnet build   # generators/: [GeneratedRegex], [LibraryImport], [JsonSerializable] via the ref pack's analyzers
[GeneratedRegex] 8.0.131 (generated: <RegexGenerator_g>F74B…__Version_0)
[LibraryImport] GetCurrentProcessId = 28412 (Environment.ProcessId = 28412)
[JsonSerializable] {"Name":"m612","Values":[1,2,3]} -> m612 3

$ dotnet publish -c Release -r win-x64 --self-contained          # 3.5 s; 177 files, 30 MB
$ (no DOTNET_ROOT, PATH=/usr/bin) bin/Release/net8.0/win-x64/publish/sc.exe
Hello, World!
$ dotnet publish -c Release -r win-x64 --self-contained -p:PublishTrimmed=true   # 9 s; 11 files, 9.7 MB
$ (no DOTNET_ROOT) bin/Release/net8.0/win-x64/publish/tr.exe            # illink.dll ran on the source-built runtime
Hello, World!
```

Timings against the mono 6.12 path (same projects, `mono MSBuild.exe` of §6.4–6.6,
`-m:1 -nr:false`, csc 4.12 through `csc-mono.cmd`, no compiler server):

| Build | mono 6.12 SDK | net8.0 SDK on the source-built runtime |
|---|---|---|
| hello console, first build (restore + build) | 3.2–4.5 s | 2.9–3.3 s |
| hello console, incremental | 2.6–3.0 s | 2.6–3.2 s |
| Microsoft.CodeAnalysis (~1500 files), Release | 19.2 s | 11.7 s (cold compiler server), 9.3 s (warm), 13.7 s (`UseSharedCompilation=false`) |
| csc of a one-file program | — | 1.6 s (`dotnet csc.dll`) |

(The numbers are from quiet runs; a later full run of the test script on a busier host
measured 15 s / 7 s for the hello builds and 27.5 s for Microsoft.CodeAnalysis, so treat
them as ±2×.) Small builds are dominated by start-up (JIT; no ReadyToRun, no tiered
compilation in the Mono flavour) and come out even; the large compile is ~1.6-2×
faster on .NET 8 Mono than on Mono 6.12.

**Bugs found and fixed on the way** (none in the MinGW Mono runtime itself: no
crash or hang under the compiler server, two MSBuild worker nodes, GC under the
Roslyn workload, many threads, named pipes):

- **Roslyn's compiler server takes "Mono" for "Unix"** (`patches/m612/roslyn-4.8.0/0001`):
  under the Mono runtime `BuildServerConnection` uses `ServerFileMutex`, a lock file
  that it deletes while it is still open; Windows refuses, `CouldLock()` reports a
  server that is not running, and every compile fell back to csc.dll after a 1 s
  connect timeout ("CompilerServer: server failed - cannot connect"). On Windows the
  named `Mutex` is used now, as on CoreCLR (the .NET 8 Mono CoreLib implements it with
  Win32). The server and its clients also need the same `[CommitHash]`
  (`GenerateMicrosoftCodeAnalysisCommitHashAttribute`), taken from the tarball's commit.
- **S9: three facades were incomplete** (found by the generators below): `mscorlib`,
  `System` and `System.Data` were compiled from `shims/*/src` only; upstream
  `shims/Directory.Build.props` also compiles the contract's `ref/<name>.cs`, which
  holds the bulk of the forwards. `mscorlib.dll` had 122 of 1425 forwards, so no
  assembly compiled against .NET Framework could resolve `System.Object` ("Could not
  resolve type with token 01000009 from typeref"). They are 4.0.0.0 again (the
  reference pack had 8.0.0.0: those three refs predated `refpack.sh`'s
  `Directory.Build.props` evaluation).
- **S9: partial facades lacked the types their contract forwards**: `genfacades.py`
  took the contract's type definitions only; `System.Memory`'s contract forwards
  `Span<T>`, `Memory<T>`… and netstandard-era code asks `System.Memory` for them. Now
  it also takes the contract's exported types (10 facades rebuilt, e.g. System.Memory
  18 forwards, System.Runtime.InteropServices 139).
- **The reference pack had an empty metadata version string**: compiled without
  `-runtimemetadataversion`, the ref `System.Runtime.dll` (the corlib of the pack) has
  `""` where upstream has `v4.0.30319`, and Roslyn copies the corlib's string into
  every assembly compiled against it. Mono and .NET do not care, but Cecil sizes the
  portable-PDB header with it and illink wrote broken PDBs (Mono:
  `debug-mono-ppdb.c:241 pdb_stream` assertion). Fixed in `refpack.sh`; the tool
  assemblies built before the fix keep the empty string (harmless; they are not
  trimmed).
- **CoreLib without its ILLink descriptors**: §6.10 left the embedded
  `ILLink.Descriptors/Substitutions/LinkAttributes.xml` out ("not needed to run"); a
  trimmed app then lost runtime-critical types ("Runtime critical type
  System.TypedReference not found"). `corelib.sh` now combines and embeds them as
  `eng/illink.targets` does (`tools/illinkxml.py` = ILLink.Tasks' CombineLinkerXmlFiles),
  and so do the libraries (`libplan.py` records each project's `ILLink*Xmls` and
  `ILLink/ILLink.Descriptors.xml`; every library with string resources also gets the
  `UseSystemResourceKeys` substitution from `eng/ILLink.Substitutions.Resources.template`);
  all 167 libraries were recompiled for it (~35 min).

**Open workarounds** (each is a deliberate gap, not a hidden hack):

- `EnableNETAnalyzers` defaults to false (in BundledVersions.props): dotnet/roslyn-analyzers
  is not built, `analyzers/build/Microsoft.CodeAnalysis.NetAnalyzers.{props,targets}`
  are empty placeholders; a project that sets `EnableNETAnalyzers=true` fails with
  CS0006 (missing analyzer).
- No `dotnet test` (vstest not built), no F# (needs an F# compiler), no
  `dotnet add package`/`dotnet nuget` (NuGet.CommandLine.XPlat not built: it needs
  the `Microsoft.Extensions.CommandLineUtils.Sources` source package and
  Microsoft.Build.Locator), no ASP.NET Core dev-certs (`EXCLUDE_ASPNETCORE`), no
  workloads (the resolver works, no manifests), no satellite resource assemblies,
  `dotnet-watch`/`dotnet-format`/Razor/Blazor/WebSdk/Containers not laid out.
- `Microsoft.Extensions.Configuration.Binder.SourceGeneration` is not passed to
  Logging.Configuration/Console (they bind through reflection instead).
- A framework-dependent apphost (`hello.exe`) needs `DOTNET_ROOT` (no global
  install, as upstream).
- Mono-path only: GenerateResource in .NETCoreApp projects needs
  `-p:GenerateResourceMSBuildRuntime=CLR4` (MSB4214) on MSBuild/Mono.

### 6.4 MSBuild (`bootstrap/m612/msbuild.sh 17.8.49 <csc.exe> 4.12.0`)

```
bash repo/bootstrap/m612/msbuild.sh 17.8.49 m612/roslyn-4.12.0/csc.exe 4.12.0
# steps: prep stringtools framework utilities build tasks exe roslyntask install test
```

What the script does:

- **Compile.** It builds Microsoft.NET.StringTools, Microsoft.Build.Framework
  (with the Microsoft.CodeAnalysis.Collections *source* package taken from
  the Roslyn 4.12 tree), Microsoft.Build.Utilities.Core, Microsoft.Build,
  Microsoft.Build.Tasks.Core, `MSBuild.exe` and Roslyn's
  Microsoft.Build.Tasks.CodeAnalysis (the Csc task).
- **Settings.** All with `MonoBuild=true`, `TargetFramework=net472`, and
  defines read from the repo's own `Directory.BeforeCommon.targets`.
  - `FEATURE_REPORTFILEACCESSES` is dropped, because it needs the
    prebuilt-only Microsoft.BuildXL.Processes.
  - `FEATURE_MSIOREDIST` is added, because Microsoft.IO.Redist is built
    from source.
- **Layout.** VS-style: `m612/msbuild-17.8.49/MSBuild/Current/Bin/`, with
  `Roslyn/` underneath.
- **`MSBuild.exe.config`** is generated from `src/MSBuild/app.config` by
  `tools/fix_config.py`:
  - binding redirects point at the versions actually built;
  - `TargetFrameworkRootPath` points at Mono's `xbuild-frameworks`;
  - `MSBuildFrameworkToolsPath*` point at Mono's `lib/mono/4.5`, never at
    `C:\Windows\Microsoft.NET`;
  - `MSBuildToolsPath` points at `Bin` (Mono is 64-bit, and the default
    would be `Bin\amd64`);
  - `CscToolExe=csc-mono.cmd`, a one-line wrapper that runs `csc.exe` under
    the seed `mono.exe`. Started directly, Windows would run it on
    Microsoft's .NET Framework;
  - `UseSharedCompilation=false`.

Test (`tests/oldstyle/hello.csproj`): an SDK-less `.NETFramework v4.7.2`
project with C# 12 features, `System.Xml.Linq` and an `EmbeddedResource`
resx handled by the GenerateResource task.

```
$ mono m612/msbuild-17.8.49/MSBuild/Current/Bin/MSBuild.exe -nologo -m:1 -nr:false -v:m hello.csproj
  hello -> E:\msys64\home\topkek\dotnetboot\m612\test\msbuild-17.8.49-oldstyle\bin\Release\hello.exe
$ mono bin/Release/hello.exe
Hello from MSBuild on Mono <sorted><i>1</i><i>2</i><i>3</i></sorted>
(resource string via GenerateResource)
```

The diagnostic log (`-v:diag`) of that build was checked for two things:

- the compiler was `…\Bin\Roslyn\csc-mono.cmd` with
  `/reference:E:\msys64\ucrt64\lib\mono\4.7.2-api\mscorlib.dll …`;
- no path under `C:\WINDOWS\Microsoft.NET` was used. The only hit was the
  unused `MSBuildFrameworkToolsPathArm64`, which is now redirected as well.

`-m:1 -nr:false` keeps the build in-proc. Out-of-proc MSBuild nodes are
started as `MSBuild.exe` processes and would run on the Windows .NET
Framework, which is the same issue as for csc. A node launcher for Mono is
future work.

Patch `patches/m612/msbuild-17.8.49/0001-monobuild-fixes.patch` fixes the
MONO code paths, which have been untested since the Mono CI was turned off
in 2021 (dotnet/msbuild#6058):

- a `"Full");` syntax error in `Toolset.cs`;
- `SecurityCritical` without its `using`;
- `Stream.ReadAsync(Memory<byte>)`, which only exists on .NET Core;
- an mcs-only lambda workaround in `FileMatcher.cs` that Roslyn rejects
  (CS9108);
- `ManifestUtilities`/`Microsoft.IO` uses in two Tasks files that MonoBuild
  excludes;
- `FrameworkPathOverride`, which must not pick Mono's `xbuild-frameworks`
  directory unless it contains `mscorlib.dll`.

## 7. Binary inventory (phase 6)

| Input | Kind | Used? | Justification |
|---|---|---|---|
| MSYS2 `mingw-w64-ucrt-x86_64-mono 6.12.0.199-3` | binary package (runtime, mcs-built class libs, Roslyn 3.9 blob as `csc.exe`, ilasm, resgen, monodis, `4.7.2-api` ref assemblies) | **yes, the seed** | trusted by the 2026-10-08 policy. Its `csc.exe` compiles exactly one thing, Roslyn 4.0.1. Its `ilasm` assembles one IL file. Its 4.7.2-api reference assemblies are the compile references for everything |
| Seed `lib/mono/4.5/{System.Collections.Immutable,System.Reflection.Metadata,System.Memory,…}.dll` | IL (from Mono's corefx copy) | yes, for Roslyn 4.0.1–4.8.0 only (compile reference + runtime) | part of the trusted seed. Replaced by from-source builds from 4.10 on (`pkgs/`) |
| Public keys `b03f5f7f11d50a3a`, `cc7b13ffcd2ddd51`, `31bf3856ad364e35` (`bootstrap/m612/keys/*.pubkey.hex`) | 160-byte RSA public-key blobs (data) | yes | for public signing only; read from seed assemblies; contain no code |
| Roslyn tarballs: 161–167 test PE files each (`src/Compilers/Test/Resources/**`) | test binaries | **no**, deleted before build | — |
| runtime-8.0.0 tarball: 26 PE files (tests) | test binaries | **no**, deleted | — |
| msbuild `src/MSFT.snk`, `src/Test.snk` | key files | no (we use `keys/`) | — |
| `Microsoft.BuildXL.Processes` (MSBuild `FEATURE_REPORTFILEACCESSES`) | prebuilt-only NuGet | **no**, feature disabled | it would be needed for file-access reporting only |
| `Microsoft.VisualStudio.Setup.Configuration.Interop` | prebuilt COM-interop NuGet | **no**, disabled by `MonoBuild` (no `FEATURE_VISUALSTUDIOSETUP`) | — |
| `Microsoft.CodeAnalysis.Collections` (MSBuild dep) | source-only NuGet | **no**, compiled from the Roslyn 4.12 tree instead | — |
| `LargeAddressAware`, `System.Private.Uri`, `System.Runtime` (MSBuild exe PackageReferences) | build-time / reference-only NuGets | no | not needed for a net472 build on Mono |
| NuGet.Client tarball: 81 test packages/PEs (`test/EndToEnd/Packages`, test `compiler/resources`) | test binaries | **no**, deleted | — |
| dotnet/sdk tarball: 4 test PEs (`dummyshim.exe`, `testwpf.dll`, …) | test binaries | **no**, deleted | — |
| Newtonsoft.Json `Src/Newtonsoft.Json/Dynamic.snk` | key pair (data) | **no**, deleted; public-signed with the public key text from its `AssemblyInfo.cs` | — |
| Public keys `adb9793829ddae60` (Microsoft.Extensions), `30ad4fe6b2a6aeed` (Newtonsoft), ECMA `b77a5c561934e089` | public-key blobs (data) | yes | taken from `PublicKey=` *text* in the runtime / Newtonsoft sources, and the 16-byte ECMA-335 pseudo key |
| `ILMerge` (NuGet.Build.Tasks.Pack), `Microsoft.DotNet.ApiCompat.Task` (SDK project ref), `Microsoft.CodeAnalysis.NetAnalyzers`, SourceLink, workload packs | prebuilt tools/packages | **no**, not built / disabled | pack, API compat, analyzers and SourceLink are not needed to build projects |
| Seed `lib/mono/4.5/vbc.exe` (Roslyn 3.9 VB compiler, MSYS2 mono package) | binary, part of the trusted seed | yes, once (`vbc.sh`) | compiles the VB generator tools and `Microsoft.CodeAnalysis.VisualBasic` 4.8.0 from source; everything VB after that (Microsoft.VisualBasic.Core) uses the from-source vbc |
| Public key `7cec85d7bea7798e` (SilverlightPlatform, `keys/SilverlightPlatform-*.pubkey.hex`) | public-key blob (data) | yes | public signing of System.Private.CoreLib; taken from the `PublicKey` text in runtime-8.0.0 `System.Private.DataContractSerialization/.../Globals.cs`, token verified |
| Windows' own `icu.dll`, `kernel32`/`ws2_32`/`bcrypt`/SChannel … | OS components | yes, at run time only | what any .NET 8 app on Windows uses (globalization, sockets, CNG, TLS); nothing is linked at build time |
| MSYS2 `mingw-w64-clang-x86_64-icu` 78.3 headers (`unicode/*.h`) | C headers of an upstream MSYS2 package | yes, compile-time only (patch 0006) | trusted by the 2026-10-08 policy; stand in for the Windows SDK `icu.h`. No ICU code is linked; the runtime loads Windows' own `icu.dll` |
| dotnet/templating v8.0.131, dotnet/command-line-api `02fe27cd`, microsoft/ApplicationInsights-dotnet 2.21.0, dotnet/sourcelink 8.0.0, dotnet/cecil `45dd3a7`, spectreconsole/wcwidth 1.0.0 (S11) | source tarballs | yes | test binaries deleted before building: templating 2 test nupkgs, ApplicationInsights 2 test DLLs, cecil 121 test assemblies, the others none. `cecil.snk` (key pair) deleted. SourceLink: the SDK pins `8.0.0-beta.26351.4` from the internal mirror; the public `8.0.0` tag is used |
| Public key `50cebf1cceb9d05e` (Mono.Cecil, `keys/Cecil-*.pubkey.hex`) | public-key blob (data) | yes | from the `Consts.PublicKey` text in dotnet/cecil `Mono.Cecil/Consts.cs`, token verified |
| `Wcwidth.Sources` 1.0.0 (source-only package used by Microsoft.TemplateEngine.Cli) | source | replaced by the same `.cs` files from the wcwidth 1.0.0 tag | its `Tables/*.Generated.cs` are Unicode width tables generated upstream from Unicode data (checked-in data-derived source, like Roslyn's generated syntax was before regeneration; not regenerated here: the generator needs Scriban/Spectre.Console.Cli) |
| dotnet/sdk `template_feed/` (console/classlib/... templates), `src/Layout/redist/trustedroots/*.pem` | text | yes | the template nupkgs in `templates/8.0.131` are zipped from this text with a hand-written nuspec; the PEM files are the code-signing/timestamping root lists NuGet verifies against (certificate data) |
| `Microsoft.Windows.CsWin32`, `Microsoft.VisualStudio.Setup.Configuration.Interop`, `Microsoft.AspNetCore.DeveloperCertificates.XPlat`, `StaticCs`, `Microsoft.CodeAnalysis.BannedApiAnalyzers`, `Microsoft.Diagnostics.Tracing.EventRegister`, `ILMerge` | prebuilt packages | **no** | not referenced with `DotNetBuildFromSource=true` / `IncludeAspNetCoreRuntime=false`; EventRegister only registers ETW manifests at build time |
| `Microsoft.Extensions.CommandLineUtils.Sources`, `Microsoft.Build.Locator` (NuGet.CommandLine.XPlat), vstest, F# compiler, dotnet/roslyn-analyzers | sources/packages | **no**, not built | `dotnet add package`/`dotnet nuget`, `dotnet test`, F# and the NetAnalyzers are not available (§6.11) |
| Windows' own .NET Framework 4.8 (`C:\Windows\Microsoft.NET`) | OS component | **no**, all paths redirected to Mono; verified by a `-v:diag` grep | out-of-proc MSBuild nodes and directly started `csc.exe` would use it, so we avoid both |

Nothing was stopped for: every binary that came up was either part of the
trusted seed, deleted, or avoided.

## 8. Recommended next steps

0. *(Done since this list was written: S6 NuGet, S7 SDK tasks, S8 targeting pack — §6.5–6.7.)*
1. **NuGet.Client 6.8 and dotnet/sdk 8.0 tasks on MSBuild/Mono (S6/S7).**
   This gives an MSBuild that understands `<Project Sdk="Microsoft.NET.Sdk">`
   for `net472` targets. It is the same pattern as S5:
   - NuGet's net472 build;
   - `Microsoft.NET.Build.Tasks` net472;
   - Newtonsoft.Json, Microsoft.Extensions.DependencyModel and NuGet
     packaging libraries from source.

   The first targets are a local, empty-restore SDK-style net472 project,
   and then a local source-built package feed.
2. **Reference pack 8.0 (S8).** Compile `src/libraries/*/ref/*.cs` with the
   direct-csc driver into a `Microsoft.NETCore.App.Ref`. It is plain C#
   and does not depend on S10.
3. **Runtime decision (S10).** Try the **Mono flavor** of dotnet/runtime 8.0
   with MinGW (CLANG64 and UCRT64) before attempting CoreCLR. In parallel,
   build `corehost` (dotnet.exe/hostfxr/hostpolicy) with MinGW, which looks
   cheap. *(Done: host §6.8, Mono native runtime §6.9.)*
3b. *(Done: S9 for the Mono flavor, §6.10.)*
3c. *(Done: S11, §6.11: the tools as net8.0 assemblies; `dotnet build` on the source-built runtime.)*
3d. **S11 leftovers**, in order of usefulness:
   - **dotnet/roslyn-analyzers** (NetAnalyzers; source fetched, `m612/src/ra`, not built): the
     analyzers themselves are ordinary C# against Microsoft.CodeAnalysis; the shipped
     `buildTransitive` props/targets and the `AnalysisLevel_*.globalconfig` files are generated by
     its `GenerateDocumentationAndConfigFiles` tool from the built analyzers, so that tool has to
     be built and run too. Then `EnableNETAnalyzers` can default to true again.
   - **NuGet.CommandLine.XPlat** (`dotnet add package`, `dotnet nuget`): needs the
     `Microsoft.Extensions.CommandLineUtils.Sources` text (dotnet/aspnetcore `src/Shared/CommandLineUtils`)
     and dotnet/msbuildlocator.
   - **vstest** (`dotnet test`), **F#** (needs an F# compiler: the same bootstrap problem as C#,
     with no MSYS2 seed), dotnet-watch/format, Razor/Web SDKs, workload manifests.
   - **singlefilehost** (`PublishSingleFile`): corehost's `singlefilehost` links `coreclr_static`
     and the static native libs; for the Mono flavour that is `libmonosgen-2.0.a` + the static
     components (built, §6.9) plus `System.IO.Compression.Native` static, behind the
     `coreclr_*` API that Mono's `main-core.c` provides. Not attempted: it needs the corehost
     CMake to accept a Mono static runtime (upstream only does that for mobile/WASM).
   - rebuild the S11 tool assemblies against the fixed reference pack (their metadata version
     string is empty, §6.11), ideally with the new SDK itself (the start of S12).
3e. **S12 (self-rebuild / dotnet/dotnet source-build)**: the new SDK can now build ordinary
   SDK-style projects, so the next step is to rebuild its own components with `dotnet build` from
   their real project files instead of `sdkproj.sh`. That needs, per repo, what Arcade provides
   (`Microsoft.DotNet.Arcade.Sdk` targets: versioning, GenerateResxSource, signing, ApiCompat,
   GenFacades …) built from dotnet/arcade source, and a local feed of source-built packages for
   every `PackageReference` (the S3/OOB/clideps assemblies packed as nupkgs). The VMR's
   `--source-only` mode is still Linux-only; on Windows it would be driven repo by repo.
4. CoreCLR on CLANG64 remains the long pole:
   - a CMake branch for the GNU-on-Windows toolchain;
   - UASM or `llvm-ml` for the 24 MASM files;
   - `FEATURE_EVENT_TRACE` off, to replace `mc -um`;
   - the `/EHa` semantics audit.

## 9. Where this stops, and why

Everything up to here is managed code compiled by our own Roslyn on Mono,
and the result is an SDK that builds net472 *and compiles* net8.0 projects
on Mono. The next stages produce the .NET runtime itself. The native part of
the Mono flavor is done (§6.9); CoreCLR is the hard one:

- **S9 (System.Private.CoreLib + libraries src for net8.0-windows)** is done
  for the Mono flavor (§6.10); a CoreCLR CoreLib would be a separate build
  of the same shared sources plus `src/coreclr/System.Private.CoreLib`.
- **S10 (runtime)** is native code. dotnet/runtime's shared Windows CMake
  configuration assumes MSVC: the first probe of even the *host* with
  CLANG64 clang fails at `configurecompiler.cmake:969` "MC not found"
  (MASM and the Windows Message Compiler are mandatory for every Windows
  native build); CoreCLR additionally needs ~24 MASM files ported or an
  MASM-compatible assembler (UASM / llvm-ml), `/EHa` semantics, PE TLS and
  the ETW `mc -um` generator replaced or disabled (§3).
- **Assessment:** not blocked in principle, but a multi-week port.
  Recommended order: (1) a GNU-on-Windows branch in
  `eng/native/configurecompiler.cmake` (no `mc`, no MASM for the host),
  then corehost (dotnet.exe/hostfxr/hostpolicy/apphost) with CLANG64 — this
  also unblocks the SDK's apphost; (2) the **Mono flavor** of dotnet/runtime
  8.0 for win-x64 with MinGW (C only, Mono has a MinGW history, and .NET on
  s390x/ppc64le ships this flavor) plus its CoreLib from S9; (3) CoreCLR on
  CLANG64 later.
