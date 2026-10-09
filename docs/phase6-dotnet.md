# Phase 6: from Mono 6.12 to a source-built .NET SDK on MSYS2

Status as of 2026-10-08. Host: `ssh topkek@win11`, MSYS2 UCRT64, 4 cores.
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
| NuGet client (NuGet.Build.Tasks, NuGet.* libs, restore) | NuGet/NuGet.Client | C# | restore and package resolution in SDK builds | not started |
| .NET SDK tasks and targets (Microsoft.NET.Build.Tasks, Sdks/Microsoft.NET.Sdk) | dotnet/sdk | C# + targets | SDK-style projects | not started |
| Reference packs (Microsoft.NETCore.App.Ref) | dotnet/runtime `src/libraries/*/ref` | C# | compiling anything for netX | not started |
| System.Private.CoreLib + framework libraries | dotnet/runtime | C# | the shared framework | not started |
| Runtime: CoreCLR (`coreclr.dll`, `clrjit.dll`) **or** the Mono flavor of dotnet/runtime | dotnet/runtime | C/C++/asm | the shared framework | Mono flavor native runtime built (§6.9); CoreCLR not started (§3) |
| Host: `dotnet.exe`, `hostfxr.dll`, `hostpolicy.dll`, `apphost.exe` | dotnet/runtime `src/native/corehost` | C/C++ | running anything | **done** with CLANG64 (§6.8) |
| Native shims (System.IO.Compression.Native, Globalization) | dotnet/runtime `src/native` | C | framework | not started |
| Arcade SDK, source generators, ILLink, GenAPI … | dotnet/arcade, runtime | C# | only to run the upstream build | avoided so far (direct csc) |

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
| S9 | System.Private.CoreLib + `src/libraries/*/src` for net8.0-windows | S4 + S8 | UNKNOWN | compile-feasible with the direct-csc method (source generators build from source, §6.2), but CoreLib's build has many generated inputs and runtime-flavor specifics; no one has done this outside Arcade |
| S10a | Host (dotnet.exe, hostfxr, hostpolicy, apphost, nethost, comhost) with CLANG64 | MinGW toolchain | **SOLVED-HERE** | `corehost.sh`, §6.8: 3 patches (GNU/MinGW branch in `eng/native`, skip ijwhost/tests, 7 small clang/libc++ source fixes); self-contained binaries (static libc++); `dotnet --info` runs, the SDK patches apphost.exe for a net8.0 `UseAppHost=true` app, apphost → hostfxr → hostpolicy load and report the missing runtime. singlefilehost needs `coreclr_static` (S10c) |
| S10b | Runtime: Mono flavor of dotnet/runtime 8.0 (`-rf Mono`) with MinGW | MinGW | **SOLVED-HERE** (native part) | `mono-runtime.sh`, §6.9: 4 more patches (MSVC-only CMake, `__thread`, no MASM, POSIX configure results cleared, ICU C headers, zlib-intel/windres fixes). `coreclr.dll` (Mono VM) + `System.IO.Compression.Native.dll` build with CLANG64 and import only system DLLs; dotnet.exe → hostfxr → hostpolicy → `coreclr.dll` initialises Mono and stops at the missing System.Private.CoreLib (S9) |
| S10c | Runtime: CoreCLR with CLANG64 clang (GNU driver) | MinGW | **BLOCKED for now** (large port) | MSVC-only CMake, ~24 MASM files, /EHa semantics, mc.exe ETW, PE TLS (§3) |
| S11 | Assemble SDK 8.0 (host + shared framework + SDK managed bits built for net8.0) | all above | UNKNOWN | depends on S9/S10 |
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
| MSYS2 `mingw-w64-clang-x86_64-icu` 78.3 headers (`unicode/*.h`) | C headers of an upstream MSYS2 package | yes, compile-time only (patch 0006) | trusted by the 2026-10-08 policy; stand in for the Windows SDK `icu.h`. No ICU code is linked; the runtime loads Windows' own `icu.dll` |
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
3b. **S9 for the Mono flavor.** `src/mono/System.Private.CoreLib` (the
   shared `src/libraries/System.Private.CoreLib/src/*.projitems` plus the
   Mono part) with the direct-csc driver: defines `TARGET_WINDOWS`,
   `TARGET_AMD64`, `TARGET_64BIT`, `MONO`, the `FEATURE_*` of the Mono
   csproj; `Strings.resx` → SR via ResxGen; the CoreLib source generators
   (LibraryImportGenerator is built already, EventSourceGenerator is not);
   CoreLib's icall signatures must match this exact runtime tree. Then the
   `src/libraries/*/src` implementation assemblies for `net8.0-windows`,
   and a `shared/Microsoft.NETCore.App/<ver>` layout (coreclr.dll,
   hostpolicy.dll, System.IO.Compression.Native.dll, CoreLib, libraries,
   `Microsoft.NETCore.App.deps.json`, `.runtimeconfig.json`).
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

- **S9 (System.Private.CoreLib + libraries src for net8.0-windows)** is
  compile-feasible with the same driver (the libraries are C# with source
  generators we can build), but CoreLib is runtime-flavor-specific (CoreCLR
  vs Mono) and is only useful together with S10.
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
