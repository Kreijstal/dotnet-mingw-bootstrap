# Phase 6: from Mono 6.12 to a source-built .NET SDK on MSYS2

Status as of 2026-10-10. Host: `ssh topkek@win11`, MSYS2 UCRT64, 4 cores.
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
| C# compiler (`csc`, Microsoft.CodeAnalysis*) | dotnet/roslyn | C# | net472 build on Mono now; netX build in the SDK later | **built (4.0 → 4.12)**; 4.8 net8.0 **by its own project files** (S12, §6.13) |
| MSBuild engine + tasks + Common targets | dotnet/msbuild | C# | net472 on Mono now; netX in the SDK later | **built (17.8.49, net472)**; net8.0 **by its own project files** (S12) |
| Roslyn MSBuild task (Csc) + Microsoft.CSharp.Core.targets | dotnet/roslyn | C# | with MSBuild | **built** |
| netfx "package" assemblies (SCI, SRM, System.Memory, STJ, …) | dotnet/runtime, dotnet/maintenance-packages | C#/IL | MSBuild/Roslyn deps on Mono | **built** |
| NuGet client (NuGet.Build.Tasks, NuGet.* libs, restore) | NuGet/NuGet.Client | C# | restore and package resolution in SDK builds | **built** (net472 §6.5, net8.0 §6.11; by its own project files incl. NuGet.CommandLine.XPlat, S12) |
| .NET SDK tasks and targets (Microsoft.NET.Build.Tasks, Sdks/Microsoft.NET.Sdk) | dotnet/sdk | C# + targets | SDK-style projects | **built** (net472 §6.6, net8.0 §6.11; by its own project files, S12) |
| dotnet CLI (`dotnet.dll`, Cli.Utils, TemplateEngine, System.CommandLine, resolvers) | dotnet/sdk, templating, command-line-api | C# | `dotnet build/new/run/pack/publish` | **built** (net8.0, §6.11; by their own project files, S12) |
| Reference packs (Microsoft.NETCore.App.Ref) | dotnet/runtime `src/libraries/*/ref` | C# | compiling anything for netX | **built** (§6.7; generators in `analyzers/dotnet/cs`, runtime pack, §6.11) |
| System.Private.CoreLib + framework libraries | dotnet/runtime | C# | the shared framework | **built** (168 assemblies, §6.10; CoreLib for both flavors, §6.12) |
| Runtime: CoreCLR (`coreclr.dll`, `clrjit.dll`) **or** the Mono flavor of dotnet/runtime | dotnet/runtime | C/C++/asm | the shared framework | **both built** with CLANG64: Mono flavor (§6.9), CoreCLR + its CoreLib + crossgen2/ReadyToRun (§6.12) |
| Host: `dotnet.exe`, `hostfxr.dll`, `hostpolicy.dll`, `apphost.exe` | dotnet/runtime `src/native/corehost` | C/C++ | running anything | **done** with CLANG64 (§6.8) |
| Native shims (System.IO.Compression.Native, Globalization) | dotnet/runtime `src/native` | C | framework | **built** (§6.9; Globalization is in the runtime) |
| Arcade SDK, source generators, ILLink, GenAPI … | dotnet/arcade, runtime | C# | only to run the upstream build | S8–S11: avoided (direct csc); generators built from source; GenFacades/PNSE replaced by small scripts (§6.10); ILLink built for PublishTrimmed (§6.11). S12: **Arcade SDK built from source** (§6.13) and used by every repo build |

## 3. Native parts on MinGW

The source is dotnet/runtime `main` and `release/9.0`, read from shallow
clones. Nothing was built.

*(Written before the port. Outcome, §6.12: the CLANG64 route below worked; UASM, not
`llvm-ml`, assembles the MASM files; `/EHa` is emulated in a personality wrapper; the ETW header
is generated without `mc.exe` (ETW off); PE TLS needed nothing.)*

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
| S10c | Runtime: CoreCLR with CLANG64 clang (GNU driver) | MinGW | **SOLVED-HERE** | `coreclr.sh`, `corelib-coreclr.sh`, `framework-coreclr.sh`, `crossgen2.sh`, §6.12: patches 0008–0017 (GNU-driver CMake, inc/mingw, ETW header without mc.exe, /EHa emulation for clang's Itanium EH, a clang `__finally` miscompile worked around, MASM via UASM). hello/smoke/ehstress and the whole `sdk-net8-test.sh` pass on CoreCLR; crossgen2 R2R-compiles the framework and the SDK tools (SDK commands 2–2.5× faster than on Mono). Open: ETW, vxsort, DAC/DBI/createdump, singlefilehost |
| S11 | Assemble SDK 8.0 (host + shared framework + SDK managed bits built for net8.0) | all above | **SOLVED-HERE** | §6.11: Roslyn, MSBuild, NuGet, SDK tasks/resolvers, `dotnet.dll`, TemplateEngine, SourceLink, ILLink as net8.0 assemblies in the upstream `sdk/8.0.131` layout; `dotnet new/build/run/pack/publish` (incl. self-contained, trimmed, PackageReference from a local feed, `-m` with node reuse, the compiler server) run on the from-source Mono-flavour runtime, no Mono 6.12. 1 patch (Roslyn compiler server mutex); open: roslyn-analyzers, vstest, F#, XPlat |
| S12 | Rebuild the SDK with itself (repo by repo, Windows) | S11 + S10c | **SOLVED-HERE** (SDK tools; runtime managed parts still hand-built) | §6.13: Arcade from source, source-build-reference-packages (ilasm from the CoreCLR tree), then Arcade, xliff-tasks, Roslyn, MSBuild, Newtonsoft.Json, xdt, NuGet.Client, command-line-api, templating, deployment-tools, sourcelink, dotnet/sdk with `dotnet build` of their own project files against a local feed only; `dotnet-sdk-8.0.131-stage2` passes `sdk-net8-test.sh` (+ `dotnet add package`); stage 3 (built by stage 2) and stage 4 (built by stage 3) are **bit-identical** (2725 assemblies); stage 2 vs 3 differ only in the compiler-version string in the PDBs. Open: runtime libraries/CoreLib/crossgen2 from their project files, roslyn-analyzers, vstest, F# |

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

### 6.12 CoreCLR with CLANG64 (S10c), its CoreLib, crossgen2 and ReadyToRun

The runtime the upstream SDK ships, from the same runtime 8.0.0 tree as §6.8–6.10, built with
MSYS2 CLANG64 (clang 22.1.8 / lld / libc++, `x86_64-w64-mingw32`) instead of MSVC, and run
under the S11 SDK in a **separate root** (`m612/dotnet-sdk-8.0.131-net8-coreclr`), so the
Mono-flavour root stays as it is.

**Patches** (`patches/m612/runtime-8.0.0/`, on top of 0001–0007):

| Patch | What |
|---|---|
| 0008 | `eng/native`: MinGW `-O` flags for C/C++ only (not for the MASM language), no MSVC CRT libs, natvis only with MSVC |
| 0009 | `src/coreclr` CMake for MinGW: MASM via UASM (`CMAKE_ASM_MASM_FLAGS -win64 -nologo`, `__MINGW32__` for asm), clang options, `USE_STL`, `-include inc/mingw/msvc_compat.h`; no DAC/DBI/createdump/single-file diagnostics, no vxsort; checked-in MIDL `_i.cpp`; resource DLLs linked as C++; `mscorrc.rc` string IDs masked to 16 bits for llvm-rc; `.def` `DATA` upper case; coreclr links the mingw system libs + `clrmingweha` |
| 0010 | `src/coreclr/inc/mingw`: `msvc_compat.h` (force-included: `__annotation`, min/max, crtdbg.h, `_rdsspq` with MSVC's signature …), `sal_legacy.h` (415 SAL 1 macros, empty), a `winnt.h` wrapper (`KNONVOLATILE_CONTEXT_POINTERS` with the SDK's named members, `C_ASSERT` as `static_assert`), `verrsrc.h` |
| 0011 | `genEtwProvider.py --nomc`: the ETW header without `mc.exe` (provider GUIDs/handles/contexts, event descriptors, task GUIDs, keyword/opcode defines); providers are never registered, so ETW is off; EventPipe works |
| 0012 | `/EHa` for clang's Itanium C++ EH: `minipal/Windows/mingw_eha.cpp` + `mingw_guids.cpp`, the `clrmingweha` library linked with `--wrap=__gxx_personality_seh0,_Unwind_Resume,__cxa_rethrow,__C_specific_handler`; `RtlUnwind` → `CLR_MinGW_RtlUnwind` in `exceptionhandling.cpp` (below) |
| 0013 | `SEH_FINALLY_FUNCTION` (`optnone,noinline` with clang+MinGW) on the 8 functions whose `__finally` reads a member of a parent local: clang 22 miscompiles them (below) |
| 0014 | `vm/amd64/*.asm` for UASM: `.errnz` messages in `<>`, `NOP_3_BYTE`/`rdsspq`/`incsspq`/`rexw` as `db`, `ALTENTRY` as a public label, Itanium C++ names instead of MSVC-mangled ones (`_Z7JIT_NewP21CORINFO_CLASS_STRUCT_`, `_ZTV16InlinedCallFrame`, `RedirectTarget` names built with `@CatStr`/`@SizeStr`) |
| 0015 | C++ fixes for clang with the GNU driver: function-pointer ↔ `void*`, gotos over initialisations, `AllocMemHolder`, `AlignUp`/`BitScan` overloads, `JITINTERFACE_HRESULT`, daccess VPTR ctors, enums with fixed types, `static g_SystemInfo` (Itanium does not mangle variable types, so the GC's and the VM's collided), GS cookie, `__report_gsfailure` … |
| 0016 | `src/native`: cpufeatures `xsave` target attribute, EventPipe goto locals, ICU shim `U_FALLTHROUGH` in C |
| 0017 | `COMIMAGE_FLAGS_NATIVE_ENTRYPOINT` moves from `msvc_compat.h` to the `winnt.h` wrapper (crossgen2's jitinterface includes `corhdr.h` without `windows.h`) |

**Commands** (chain steps 38–44):

```
env MSYSTEM=CLANG64 bash bootstrap/m612/coreclr.sh            # prep configure build install test
#   cmake -G Ninja -S src/coreclr ... -DCMAKE_ASM_MASM_COMPILER=uasm -DCLR_CMAKE_TARGET_OS=windows ...
#   ninja coreclr clrjit mscorrc clretwrc   -> m612/coreclr-8.0.0 (stripped)
bash bootstrap/m612/corelib-coreclr.sh "$(cygpath -m m612/dotnet-sdk-8.0.131-net8/sdk/8.0.131/Roslyn/bincore/csc.dll)"
#   -> m612/netcoreapp-8.0.0-coreclr/lib/System.Private.CoreLib.dll
env TESTS="hello smoke ehstress" bash bootstrap/m612/framework-coreclr.sh   # newroot layout install runtimepack check test
env ROOT=m612/dotnet-sdk-8.0.131-net8-coreclr TESTDIR=m612/test/sdk-net8-coreclr bash bootstrap/m612/sdk-net8-test.sh
bash bootstrap/m612/crossgen2.sh                               # native managed layout test
bash bootstrap/m612/crossgen2.sh r2rroot r2rfx r2rsdk          # -> m612/dotnet-sdk-8.0.131-net8-coreclr-r2r
env ROOT=m612/dotnet-sdk-8.0.131-net8-coreclr-r2r TESTDIR=m612/test/sdk-net8-coreclr-r2r bash bootstrap/m612/sdk-net8-test.sh
```

**Outputs**:

```
m612/coreclr-8.0.0/        coreclr.dll 7662080, clrjit.dll 3113472, mscorrc.dll 139264, clretwrc.dll 12288
                           (imports: system DLLs and the UCRT api sets only; libc++ is static)
m612/netcoreapp-8.0.0-coreclr/lib/System.Private.CoreLib.dll   4963840
m612/dotnet-sdk-8.0.131-net8-coreclr/      the S11 SDK + the CoreCLR shared framework + runtime pack
m612/crossgen2-8.0.0/      crossgen2.dll + ILCompiler.{DependencyAnalysisFramework,TypeSystem,Diagnostics,ReadyToRun}.dll,
                           System.CommandLine.dll, clrjit_win_x64_x64.dll, jitinterface_x64.dll, crossgen2.runtimeconfig.json
m612/dotnet-sdk-8.0.131-net8-coreclr-r2r/  the same root, framework (168 assemblies, 35 MB -> 70 MB) and SDK tools
                           (81 assemblies, sdk/ 46 MB -> 91 MB) as ReadyToRun images
```

**Tests**:

- `coreclr.sh test`: `tests/coreclr/eha.cpp`, the `/EHa` shim self-test: AV / `RaiseException` /
  C++ throw through C++ frames into `__except` and `catch(...)` with destructors in order,
  `catch(int)` skipped for an AV, `throw;` of an SEH exception, nesting; 10 cases `ok` and
  200000 round trips.
- `framework-coreclr.sh test` (apps built by the CoreCLR SDK itself, run by its `dotnet.exe`):
  `hello` (exceptions, a 52-frame unwind, `finally`, rethrow, NullReferenceException from a
  hardware fault, threads, thread pool, async, ICU cultures) passes; `smoke --net` prints
  `SMOKE OK`; `ehstress` (exceptions across VM/native frames, reflection invoke, cctor failures,
  reverse P/Invoke through `qsort`, filters, Monitor waits) prints `EHSTRESS OK`, and with
  `CLR_MINGW_EHA_TRACE=1` 24803 cleanups ran and no unwind skipped one.
  `check`: `tools/rvaalign.cs` finds 0 misaligned `CreateSpan` blobs.
- `sdk-net8-test.sh` on the CoreCLR root: every step passes; the consumer app prints
  `runtime: .NET 8.0.0 (CoreCLR)`.
- `crossgen2.sh test`: `hello.dll` compiles in 1.6 s; the COR header goes from
  `flags=0x1 ManagedNativeHeader=0x0/0 IL only` to `flags=0x4 ManagedNativeHeader=0x1560/160
  READYTORUN (RTR) v9.1`; it runs, and `DOTNET_JitDisasmSummary=1` lists no `Program:` method
  as jitted (3 with the IL image).
- `r2rfx`: 168 of 168 framework assemblies compiled (5 native/R2R skipped), 2 min with 4 jobs;
  `r2rsdk`: 68 (`sdk/8.0.131`) + 7 (`Roslyn/bincore`) + 4 (`Microsoft.NET.Sdk/tools/net8.0`)
  + 1 (NuGet pack tasks) + 1 (SDK resolver), none failed.

**Timings** (wall clock, win11, 4 cores, same day; `sdk-net8-test.sh` TIME lines):

| Step | Mono flavour | CoreCLR (JIT) | CoreCLR + R2R |
|---|---|---|---|
| dotnet new console | 3.1 s | 3.2 s | 1.2 s |
| dotnet build (first) | 6.3 s | 5.0 s | 2.4 s |
| dotnet build (incremental) | 2.8 s | 2.3 s | 1.0 s |
| dotnet run | 3.0 s | 2.5 s | 1.1 s |
| dotnet pack | 3.5 s | 3.0 s | 1.4 s |
| dotnet build (pkgref) | 3.5 s | 2.9 s | 1.4 s |
| dotnet run (pkgref) | 1.7 s | 1.9 s | 0.9 s |
| dotnet build -m:4 (3 projects) | 6.5 s | 5.0 s | 2.2 s |
| dotnet build -m:4 (no-op) | 2.9 s | 2.4 s | 1.1 s |
| dotnet build roslyncore (~1500 files) | 16.2 s | 18.4 s (repeats 7.6 / 7.1 s) | 9.5 s |
| csc.dll (rebuilt MCA) hello | 1.6 s | 1.5 s | 0.8 s |
| dotnet build (generators) | 3.5 s | 3.2 s | 1.7 s |
| dotnet publish --self-contained | 3.6 s | 3.2 s | 2.1 s |
| dotnet publish -p:PublishTrimmed=true | 7.2 s | 13.8 s (6.8 s in a rerun) | 4.8 s |
| `dotnet hello.dll` | ~690 ms | ~560 ms | ~290 ms |
| `dotnet smoke.dll --net` | ~1400 ms | ~1010 ms | ~430 ms |
| `dotnet ehstress.dll` | — (cctor case fails on Mono) | ~4.1 s | ~4.0 s |

CoreCLR with the JIT alone is already faster than Mono at steady state (the compiler server,
roslyncore repeats: 7.1–7.6 s vs 12.3–14.6 s); the first build of a big project pays for
tier-0 JIT of the compiler. With R2R the SDK commands are 2–2.5× faster than Mono.

**How the hard parts were done**:

- **MASM.** `llvm-ml64` lacks PROC-scoped labels and `mod`; UASM 2.58 (MSYS2
  `mingw-w64-clang-x86_64-uasm`, `-win64`) assembles the 20 `vm/amd64` files (7 of them changed) with the byte
  encodings and names of patch 0014.
- **`/EHa` semantics.** CoreCLR relies on MSVC `/EHa`: `catch(...)` and C++ destructors also see
  SEH exceptions (AVs, `RaiseException` of the managed exception code), and SEH unwinds run C++
  cleanups. clang's Itanium EH over SEH (`_GCC_specific_handler`) ignores foreign exceptions.
  `mingw_eha.cpp` wraps the personality: it parses the LSDA itself; in phase 1 a `catch(...)`
  takes a foreign exception (wrapped in a C++ `EhaSehException`); in phase 2 cleanup landing
  pads are entered by a collided `RtlUnwindEx` and resumed through a tagged copy of the
  `EXCEPTION_RECORD` (`ExceptionInformation[13]` = entry, `[14]` = magic). `throw;` of a
  foreign exception re-raises it. `__C_specific_handler` and `RtlUnwind` are wrapped so the
  VM's own unwinds run C++ cleanups too.
- **A clang bug.** A `__finally` that reads a member at a nonzero offset of a parent-frame local
  is miscompiled by LLVM 22 (`llvm.localrecover` + constant offset loses the frame-escape symbol;
  the same on the MSVC target). On the finalizer thread it clobbered RDI and killed the
  process at shutdown ("Fatal error."), found by attaching gdb late (under a debugger started
  with the process it did not reproduce). Workaround: `optnone` on the 8 functions (patch 0013).
- **CoreLib** needs the net8.0 `csc.dll` (Roslyn 4.8 on System.Reflection.Metadata 8): the seed's
  SRM 5 aligns RVA field data to 4 only and CoreCLR's `RuntimeHelpers.CreateSpan` rejects it
  (ArgumentException at startup). Roslyn emits bodies for `InternalCall` auto-property accessors
  that ILLink removes upstream; `tools/fcallrva.cs` does the same (else the VM took the getter
  for an NDirect stub). The runtime-root ILLink descriptor
  (`CreateRuntimeRootILLinkDescriptorFile` of the source-built ILLink.Tasks) is embedded, else a
  trimmed app lost `System.__Canon`.

**Open issues**:

- ETW is off (no `mc.exe`; stub header, providers never registered). EventPipe/`dotnet-trace`
  work.
- No vxsort in the GC (AVX2/AVX-512 sort of the mark list; its `numeric_limits` clashes with
  libc++'s), no DAC
  (`mscordaccore`), DBI (`mscordbi`), `createdump` or single-file diagnostics: managed debugging
  and `dotnet-dump` analysis are not available. `coreclr_static`/singlefilehost not built.
- Asynchronous faults at instructions that are not calls are not covered by the C++ LSDA
  (clang has no `-fasync-exceptions` for Itanium C++ EH; it crashes clang): a destructor whose
  scope contains only the faulting instruction may not run. The VM itself converts hardware
  faults in managed code, so this only matters for native VM code that faults inside a C++
  scope with a destructor.
- Unwinds started by initiators the shim does not know skip C++ cleanups;
  `CLR_MINGW_EHA_TRACE=1` logs every such unwind (none in the tests).
- The LLVM `__finally` bug should be reported upstream with the `fin7`/`fin10` reproducers;
  until then patch 0013's `optnone` list has to be kept complete by inspection.
- The Mono-flavour CoreLib keeps 18 misaligned `CreateSpan` blobs (Mono does not check).
- The R2R images are compiled per assembly (no composite image, no PGO/MIBC data, no partial
  R2R); crossgen2 itself runs as IL (it could be R2R-compiled by itself); no `--pdb`
  (needs the native DiaSymReader).

### 6.13 S12: the SDK rebuilds itself from the upstream project files

The SDK of §6.12 (`dotnet-sdk-8.0.131-net8-coreclr-r2r`, CoreCLR + ReadyToRun) builds the SDK's own
repositories with **`dotnet build`/`dotnet pack` of their own project files** (no `csc` command lines,
no `csproj_items.py`/`sdkproj.sh`, no Mono), in the upstream source-build configuration
(`DotNetBuildFromSource=true`, `DotNetBuildFromSourceFlavor=Product`), restoring only from local feeds
of packages built here. The result is laid out as a new SDK root, `m612/dotnet-sdk-8.0.131-stage2`;
the same scripts run again with that SDK give stage 3, and once more stage 4.

The VMR's `./build.sh --source-only` is Linux-only, so this goes **repo by repo**, in the VMR's order,
with the VMR's own mechanisms where they exist:

- **Version lifting.** The VMR passes every repo a `PackageVersions.props` with the versions the earlier
  repos produced (`DotNetPackageVersionPropsPath`; Arcade imports it right after `eng/Versions.props`,
  NuGet's `build/config.props` imports it itself). `tools/s12versions.py` writes the same file from
  the stage's feed: `<IdWithoutDots>PackageVersion` and `<IdWithoutDots>Version` for every package.
- **source-build-reference-packages** (SBRP, `release/8.0`) supplies compile-time-only packages, as in
  the VMR: targeting packs (IL text), text-only packages, and C# reference packages.
- **Target frameworks**: `DotNetTargetFrameworkFilter=netstandard2.0;netstandard2.1;net8.0` (no
  .NET Framework, no older .NETCoreApp; SBRP itself also keeps netstandard1.x).

**Scripts** (all under `bootstrap/m612/`; `s12-stage.sh` runs them in order, chain steps 45–50):

| Step | Script | What |
|---|---|---|
| 1 | `s12-sbrp.sh prep targetpacks textonly` | SBRP: targeting packs NETStandard.Library 1.6.1/2.0.3, NETStandard.Library.Ref 2.1.0, Microsoft.NETCore.App.Ref 3.0.0–7.0.0 assembled from SBRP's IL with the **from-source CoreCLR `ilasm`** (`ninja ilasm` in the coreclr.sh tree), as SBRP's `build-il.sh` does; the 11 text-only packages. `s12-sbrp.sh refs` builds C# reference packages with `dotnet pack` of their SBRP project files, on demand: `s12_restore` reads NU1101/NU1102/NU1603 from a failed restore (`tools/s12missing.py`), works out the SBRP closure (`tools/s12sbrpdeps.py`) and retries |
| 2 | `s12-packs.sh` | the targeting packs the builds use (`NetCoreTargetingPackRoot`): Microsoft.NETCore.App.Ref 8.0.31 rebuilt by `refpack.sh` (with the build SDK's `csc.dll`, no Mono) with two fixes found here, see below |
| 3 | `s12-feed.sh` | the **seed feed**: 35 packages made from assemblies of the hand-made S9–S11 drivers (see below) |
| 4 | `s12-arcade.sh` | dotnet/arcade 7945b1b6 (Microsoft.DotNet.Arcade.Sdk **8.0.0-beta.26411.4**, the version SDK 8.0.131 pins): a bootstrap SDK `8.0.0-boot` (Arcade's own `sdk/` + `tools/` text, its task assembly compiled by a 20-line project of ours from the same sources), then the real `Microsoft.DotNet.Arcade.Sdk.csproj` under it; every later repo's `global.json` points at this Arcade |
| 5 | `s12-xliff.sh` | dotnet/xliff-tasks 73f08509 (Microsoft.DotNet.XliffTasks 1.0.0-beta.23475.1): the repos' `.xlf` → satellite assemblies |
| 6 | `s12-roslyn.sh` | Roslyn 4.8.0 (tag), `s12.slnf`: Microsoft.CodeAnalysis{,.CSharp,.VisualBasic,.Scripting,.CSharp.Scripting,.ExternalAccess.RazorCompiler}, csc/vbc/VBCSCompiler/csi, Microsoft.Build.Tasks.CodeAnalysis, the **Microsoft.Net.Compilers.Toolset** package; 4.8.0-7.25569.25 (the SDK's pin) |
| 7 | `s12-msbuild.sh` | MSBuild 17.8.49, `MSBuild.SourceBuild.slnf` (net8.0 + the netstandard2.0 reference builds), packages 17.8.49 |
| 8–9 | `s12-newtonsoft.sh`, `s12-xdt.sh` | Newtonsoft.Json 13.0.3 (`LibraryFrameworks=net6.0;netstandard2.0`, `SIGNED`, public-signed); dotnet/xdt 9a1c3e1b (Microsoft.Web.Xdt 7.0.0-preview.22423.2) |
| 10 | `s12-nuget.sh` | NuGet.Client 6.8.2.3: the 19 `XPLATProject` projects of its source build (NuGet.*, NuGet.Build.Tasks(.Console,.Pack), Microsoft.Build.NuGetSdkResolver, **NuGet.CommandLine.XPlat**), 6.8.2-rc.3 |
| 11–14 | `s12-cmdline.sh`, `s12-templating.sh`, `s12-deployment-tools.sh`, `s12-sourcelink.sh` | System.CommandLine 2.0.0-beta4.23307.1 (`sourcebuild.slnf`), Microsoft.TemplateEngine.* 8.0.131, Microsoft.Deployment.DotNet.Releases 2.0.0-rtm.1.25064.1, Microsoft.Build.Tasks.Git + Microsoft.SourceLink.* (public tag 8.0.0) |
| 15 | `s12-sdk.sh` | dotnet/sdk 8.0.131, `s12.slnf` = the S11 set: dotnet.dll, Cli.Utils, Configurer, Sln.Internal, InternalAbstractions, TemplateLocator, Microsoft.Win32.Msi, Microsoft.TemplateEngine.Cli, the resolvers, Microsoft.NET.Build.Tasks (+ ApiCompat tasks, which it references), `IncludeAspNetCoreRuntime=false` |
| 16 | `s12-layout.sh` | `dotnet-sdk-8.0.131-stageN-il` and the R2R root `dotnet-sdk-8.0.131-stageN` (below) |
| — | `s12-verify.sh` | the stage comparison (below) |

Common settings (`s12-common.sh`): `ContinuousIntegrationBuild=true`, `Deterministic=true`, source-control
queries and SourceLink off (tarballs, not checkouts; Arcade then maps `RepoRoot` to `/_/`; the two
non-Arcade repos get `PathMap`), `RepositoryCommit`/`SourceRevisionId` = the tarball's commit,
`RepositoryUrl` = the GitHub URL, `OfficialBuildId` chosen to reproduce each pinned version number
(e.g. 20251119.25 → Roslyn 4.8.0-7.25569.25). Every `dotnet` runs with a private CLI home and
`NUGET_PACKAGES` per stage, and `HTTP(S)_PROXY=http://127.0.0.1:9`, so any network access fails; each
repo's `NuGet.config` is replaced by one that `<clear/>`s and lists only `s<N>/feed`, `feed-sbrp`,
`feed-seed` (and the bootstrap feed). The SDK itself adds `<root>/library-packs` (the ILLink nupkg).

**Turned off** (tools, not product code; Arcade's own source-build mode turns off the same):
Pdb2Pdb (Windows-PDB conversion, a prebuilt native tool; also `PublishWindowsPdb=false`), VS SDK,
IBC optimization data/training, symbol uploader; `FullAssemblySigningSupported=false` (public signing
only, as Arcade offers for distros; the Open/AspNetCore/cecil key *pairs* in Arcade's `tools/snk` are
replaced by the public keys of `keys/`); `EnableNETAnalyzers=false` and `EnforceCodeStyleInBuild=false`
(not built; analyzers only report); package validation (`EnablePackageValidation=false`, MSBuild and
Newtonsoft: needs the previous release from nuget.org); `UsingToolMicrosoftNetCompilers=false` (Roslyn,
MSBuild, sdk pin a toolset package: they are compiled by the building SDK's own Roslyn, which is the
point of the exercise).

**Patches** (`patches/m612/`):

| Patch | What |
|---|---|
| `sbrp/0001-configurationmanager-7.0.0-ConfigurationPropertyAttribute.patch` | SBRP's generated System.Configuration.ConfigurationManager 7.0.0 reference source lacks `ConfigurationPropertyAttribute` (a GenAPI omission; the real 7.0.0 has it); MSBuild's `ToolsetElement.cs` needs it. The VMR never notices: it lifts ConfigurationManager to the runtime's 8.0 build |
| `s12-newtonsoft.json-13.0.3/0001-no-prebuilt-analyzer-and-sourcelink-packages.patch` | drops Newtonsoft's Microsoft.CodeAnalysis.NetAnalyzers 6.0.0 and Microsoft.SourceLink.GitHub 1.0.0 tool references (prebuilt; the VMR does the same) |
| `roslyn-4.8.0/0001` (S11) | applied as before (compiler-server mutex) |

The S5 MSBuild patch (Mono code paths) is not applied: nothing here runs on Mono.

**Fixes to the stage-1 drivers found by S12** (`refpack.sh`): the reference assemblies lacked
`[assembly: CLSCompliant(true)]` (upstream's `eng/versioning.targets` adds it to every library unless a
project opts out), so every CLS-checked compile against the pack failed (MSBuild: CS3009 "base type
'object' is not CLS-compliant"); and the pack had no `data/PlatformManifest.txt`, so the SDK's conflict
resolution did not know what the framework carries and a net8.0 publish copied lower package versions
(SBRP's System.Collections.Immutable 7.0.0, a *reference* assembly) next to the app — found in the first
Roslyn toolset package. The stage-1 roots are left as they were; S12 uses the rebuilt pack.

**Bootstrap stubs and version overrides** (each one deliberate):

- `8.0.0-boot` Arcade SDK (bootstrap only, see step 4; replaced by the real build).
- `Microsoft.CodeAnalysis.Analyzers 3.3.4` **stub**: a package with no analyzers. Roslyn's
  CSharpSyntaxGenerator references it (`PrivateAssets=all`); it is built by dotnet/roslyn-analyzers, which
  needs Roslyn first (the VMR takes it from the previously-source-built tarball). Analyzers only report
  diagnostics, so the stub changes no output.
- `s12/KnownFrameworks.targets` (via `CustomAfterMicrosoftCommonTargets`): `KnownFrameworkReference`s for
  netcoreapp3.0–net7.0 and netstandard2.1 when the SDK lacks them (the stage-1 `BundledVersions.props`
  is hand-written and knows net8.0 only); SBRP projects target those frameworks.
- every repo's `global.json` (`tools/s12globaljson.py`): our SDK version and Arcade 8.0.0-beta.26411.4
  (each repo pins a different 8.0 Arcade build).
- Arcade: Microsoft.Extensions.Logging.Console 8.0.1 → 6.0.0 (SBRP; used by Microsoft.Arcade.Common only).
- NuGet: System.Security.Cryptography.Pkcs 6.0.4 → 7.0.2, .Xml 6.0.1 → 7.0.1 (what Microsoft.Build.Tasks.Core
  17.8.49 needs, else NU1605), System.ComponentModel.Composition 4.5.0 → 7.0.0 (SBRP has no 4.5.0);
  `IsXPlat=true` (what its Linux source build sees; on Windows it would add Microsoft.SourceLink.GitHub
  1.0.0); `DebugType=portable` (pdbonly means a native Windows PDB on Windows); warnings not as errors
  (SYSLIB0051 obsoletions of .NET 8 in NuGet 6.8).
- templating: Microsoft.Extensions.Logging(.Abstractions/.Console)/DependencyInjection.Abstractions 8.0.x
  → 6.0.0 and System.Formats.Asn1 8.0.1 → 7.0.0 (SBRP; netstandard2.0 targets need a netstandard2.0
  asset, which our runtime libraries do not have); NU5104 not an error.
- sdk: the runtime version properties 8.0.31 → our 8.0.0; the seed packages are lifted.

**The seed feed** (`s12-feed.sh`, `feed-seed/`): the runtime is *not* rebuilt from its project files
(below), so the packages dotnet/runtime produces in the VMR are packed from the S11 assemblies: the 24
out-of-band libraries of `libraries.sh` (Microsoft.Extensions.*, System.Configuration.ConfigurationManager,
EventLog, MetadataLoadContext, Pkcs/Xml, …), 8 framework assemblies the SDK pins as packages
(System.Text.Json, …), Microsoft.NET.HostModel 8.0.0, Microsoft.ApplicationInsights 2.21.0 (`clideps-net8.sh`)
and Microsoft.NETCore.DotNetHostResolver 8.0.0 (our `hostfxr.dll`). They are net8.0-only and therefore
lifted only where a repo asks for them (`S12_LIFT_SEEDS`: dotnet/sdk); everything else compiles against
SBRP.

**Feeds after stage 2**: `s2/feed` 72 packages, all built by `dotnet build/pack` here (Arcade, XliffTasks,
11 Roslyn, 7 MSBuild, Newtonsoft.Json, Xdt, 19 NuGet, 4 System.CommandLine, 8 TemplateEngine,
Deployment.DotNet.Releases, 6 SourceLink, 11 dotnet/sdk, 4 symbol packages); `feed-sbrp` 219 packages
(~190 reference packages, 8 targeting packs, 21 text-only; all from SBRP text, one patched); `feed-seed` 35 (above). No package with
compiled code comes from outside; nuget.org is never reachable.

**Layout** (`s12-layout.sh`): `sdk/8.0.131` is assembled the way dotnet/sdk's `src/Layout/redist` does it,
from the outputs: MSBuild's `bin/MSBuild` (only MSBuild's own assemblies, targets, `Current/`, `ref/`,
satellites), the CLI's `bin/dotnet` (dotnet.dll + the libraries it references), NuGet's restore/pack/
XPlat/resolver assemblies and targets, `Roslyn/` = the Toolset package's `tasks/netcore`, `Sdks/` =
dotnet/sdk's `bin/Release/Sdks` + NuGet.Build.Tasks.Pack + SourceLink from their packages, the MSBuild
SDK resolver. `redist.csproj` itself is not used: it also wants dotnet-format, vstest, F#, Razor,
NetAnalyzers/CodeStyle, Containers, dotnet-watch. Not rebuilt and taken from stage 1: `dotnet.exe`,
`host/`, `shared/` (the framework of S9/S10c), `templates/`, `library-packs/` (ILLink),
`BundledVersions.props`, the RID graphs, and the out-of-band runtime libraries MSBuild/NuGet run with.
`check` fails on any `[ReferenceAssembly]` DLL outside `sdk/8.0.131/ref` (MSBuild's and NuGet's output
folders do contain SBRP reference assemblies; they are never copied). `r2r` compiles the SDK directory
with crossgen2 (stage 1's) and reuses stage 1's R2R framework.

**Timings** (win11, 4 cores; one stage = steps 4–16):

| | stage 2 (built by stage 1) | stage 3 (by stage 2) | stage 4 (by stage 3) |
|---|---|---|---|
| Arcade (bootstrap + real) | 12 s | 12 s | 14 s |
| xliff-tasks | 5 s | 5 s | 6 s |
| Roslyn | 62 s | 72 s | 84 s |
| MSBuild | 21 s | 23 s | 22 s |
| Newtonsoft.Json + xdt | 14 s | 13 s | 14 s |
| NuGet.Client | 32 s | 34 s | 34 s |
| command-line-api, templating, deployment-tools, sourcelink | 38 s | 38 s | 37 s |
| dotnet/sdk | 32 s | 33 s | 32 s |
| layout + check + R2R of the SDK directory | 119 s | 121 s | 121 s |
| **total** | **5.6 min** | **5.9 min** | **6.1 min** |

One-time (shared by all stages): SBRP targeting packs + text-only 1.5 min, ~190 SBRP reference packages
≈ 8 min in total (built on demand during the first restores), the fixed targeting pack 7 min, seeds
30 s. (For comparison: S11's hand-made drivers on Mono 6.12 took ~3 min for Roslyn alone.)

**End-to-end proof**: `sdk-net8-test.sh` with `ROOT=m612/dotnet-sdk-8.0.131-stage2`: every step passes
(`--info` reports the stage-2 SDK; new/build/run, apphost, pack + PackageReference from a local feed,
3-project sln with `-m:4`, Roslyn's Microsoft.CodeAnalysis rebuilt and used, the reference pack's
generators, self-contained and trimmed publish). New step `addpackage`: **`dotnet add package` and
`dotnet list package` work** (NuGet.CommandLine.XPlat, an S11 gap):

```
TIME dotnet new console                    1.5 s        TIME dotnet build -m:4 (3 projects)        2.2 s
TIME dotnet build (first)                  2.5 s        TIME dotnet build -m:4 (no-op)             1.1 s
TIME dotnet build (incremental)            1.0 s        TIME dotnet build roslyncore               9.9 s
TIME dotnet run                            1.1 s        TIME csc.dll (rebuilt MCA) hello           0.8 s
TIME dotnet pack                           1.4 s        TIME dotnet build (generators)             1.6 s
TIME dotnet build (pkgref)                 1.4 s        TIME dotnet publish --self-contained       1.6 s
TIME dotnet run (pkgref)                   0.5 s        TIME dotnet publish -p:PublishTrimmed=true 4.6 s
$ dotnet add package Bootstrap.Greeting --version 2.0.0     # 1.3 s
info : PackageReference for package 'Bootstrap.Greeting' version '2.0.0' added to file '...\app2.csproj'.
```

(the same speed as the stage-1 R2R root, §6.12, as expected: same runtime, same compiler sources.)

**The bootstrap proof** (`s12-verify.sh`: every `.dll`/`.exe` in the repos' output folders, byte by byte;
then `tools/pecompare.cs` on each difference: it blanks the fields a deterministic build derives from
content hashes — MVID, COFF time stamp, PE checksum, CodeView GUID/age, PDB checksum — and compares the
rest, and lists the portable-PDB compilation options and references that differ):

| repo | stage 2 vs 3 identical / different | stage 3 vs 4 identical / different |
|---|---|---|
| Arcade | 22 / 8 | 30 / 0 |
| xliff-tasks | 0 / 1 | 1 / 0 |
| Roslyn | 442 / 51 | 493 / 0 |
| MSBuild | 550 / 60 | 610 / 0 |
| xdt | 0 / 2 | 2 / 0 |
| command-line-api | 78 / 12 | 90 / 0 |
| templating | 169 / 26 | 195 / 0 |
| deployment-tools | 13 / 1 | 14 / 0 |
| sourcelink | 78 / 6 | 84 / 0 |
| dotnet/sdk | 580 / 95 | 675 / 0 |
| NuGet.Client | 288 / 241 | 529 / 0 |
| Newtonsoft.Json | 0 / 2 | 2 / 0 |
| **SDK root `sdk/8.0.131`** (all files) | 630 / 110 | 735 / 5 |

- **Stage 3 = stage 4, bit for bit**: all 2725 assemblies (csc/Microsoft.CodeAnalysis*, MSBuild.dll and
  Microsoft.Build*, dotnet.dll, NuGet, the tasks, every satellite). The only differing files in the SDK
  roots are `.m612-stage` (a build note with the date) and four `deps.json` of the Roslyn toolset: they
  record the SHA-512 of the Microsoft.DotNet.XliffTasks *package*, and `.nupkg` files are zip archives
  with the packing time in them (NuGet's pack is not deterministic); no assembly depends on that.
- **Stage 2 vs 3**: all satellite assemblies identical; 478 of the 483 differing main assemblies are
  identical after normalization, and the PDBs show why: `compiler-version` — stage 2 was compiled by
  stage 1's csc, which reports itself as `4.8.0 (m612 bootstrap, net8.0)` (S11's hand-written
  AssemblyInformationalVersion), stage 3 by stage 2's, `4.8.0-7.25569.25+e091728607ca…`. Roslyn writes
  that string into every portable PDB, the PDB's hash goes into the PE's debug directory and MVID, and
  the MVIDs of references go into the PDBs of their dependents. The other 5 are Arcade's assemblies,
  which embed their PDB (`DebugType=embedded`), so the same string changes the PE layout itself.
  Nothing else differs: the IL, metadata and resources stage 1's compiler produced are the ones stage 2's
  produces.
- Found and fixed on the way: Arcade writes `RepositoryUrl=file://<RepoRoot>` into every assembly when
  source-control queries are off (the stage directories differ) → `RepositoryUrl` is passed; NuGet and
  Newtonsoft are not Arcade-based, so nothing mapped their source root → `PathMap`.

**What is built from its own project files now, and what still is not** (stage 2 SDK):

- from project files: everything in `sdk/8.0.131` that comes from Roslyn, MSBuild, NuGet.Client,
  dotnet/sdk, templating, command-line-api, deployment-tools, sourcelink, Newtonsoft.Json, xdt, and the
  Arcade SDK/XliffTasks that built them;
- still from the hand-made drivers of S8–S11: the shared framework (CoreLib `corelib.sh`, the 168
  libraries `libraries.sh`, Microsoft.VisualBasic.Core `vbc.sh`), the out-of-band runtime libraries in the
  SDK directory (Microsoft.Extensions.*, System.Configuration.ConfigurationManager, …: `libraries.sh` OOB),
  Microsoft.NET.HostModel and Microsoft.ApplicationInsights (`clideps-net8.sh`), the reference pack
  (`refpack.sh`, now with CLSCompliant/PlatformManifest) and its source generators (`netfx-pkgs.sh`, compiled
  in stage 1 by Roslyn 4.8 on Mono), ILLink (`illink-net8.sh`), crossgen2 (`crossgen2.sh`), the templates
  and `BundledVersions.props`; natively: host, CoreCLR, clrjit (CMake, §6.8/§6.12). Mono runs nowhere in
  stages 2–4.
- not built at all: roslyn-analyzers (NetAnalyzers/CodeStyle), vstest (`dotnet test`), F#, Razor/Web SDKs,
  dotnet-format, dotnet-watch, Containers, workloads.

**Open issues**:

- **dotnet/runtime's managed parts from their project files** (CoreLib, the libraries, the out-of-band
  packages, ILLink, crossgen2). A first attempt (`s12-runtime.sh`, Microsoft.Extensions.Logging.Abstractions)
  stops at restore on the runtime repo's own build tooling: Microsoft.DotNet.Build.Tasks.TargetFramework and
  Microsoft.DotNet.GenFacades (Arcade repo projects, buildable), Microsoft.DotNet.ApiCompat.Task (dotnet/sdk,
  built here), Microsoft.NET.ILLink.Tasks (bootstrap ILLink), and the generators' Microsoft.CodeAnalysis
  4.4/4.5 + **Workspaces** packages; Roslyn's Workspaces in turn need Humanizer.Core 2.14.1 (the VMR builds
  Humanizer from source) and System.Composition 7.0 (runtime). Ordering that loop is the next piece; with
  real runtime packages (netstandard2.0 + net8.0) the seed feed and most version overrides above go away.
- roslyn-analyzers needs Roslyn's Workspaces too (its docs/config generator runs Microsoft.CodeAnalysis);
  then the analyzer stub goes and `EnableNETAnalyzers` can default to true.
- NuGet's `.nupkg` timestamps (the only stage-3/4 difference in the SDK root, via `deps.json` hashes).
- The stage-N SDKs still use stage 1's hand-written `BundledVersions.props` (dotnet/installer's
  `GenerateBundledVersions` is not run).

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
| MSYS2 `mingw-w64-clang-x86_64-uasm` 2.58-1 (UASM, MASM-compatible assembler) | binary package | yes, build-time only (§6.12) | trusted by the 2026-10-08 policy; assembles CoreCLR's `vm/amd64/*.asm` (the Windows SDK's `ml64.exe` is not available) |
| MSYS2 CLANG64 toolchain (clang/lld/libc++ 22.1.8, llvm-rc/llvm-windres, cmake, ninja, python3) | binary packages | yes, build-time only | trusted; the native host, Mono and CoreCLR are built with it; libc++ is linked statically |
| runtime-8.0.0 `src/coreclr/pal/prebuilt/idl/*_i.cpp` and `pal/prebuilt/inc/*.h` | MIDL-generated C/C++ checked into the runtime tarball | yes (§6.12, patch 0009) | text sources generated upstream by `midl.exe` (interface IDs and COM headers); the Unix build of CoreCLR uses the same files; no MIDL here |
| `ILLink.Tasks.dll` (`CreateRuntimeRootILLinkDescriptorFile`), run by the net8.0 SDK's MSBuild | IL built from source (§6.11, `illink-net8.sh`) | yes, build-time only | generates CoreLib's runtime-root descriptor from `corelib.h`/`rexcep.h`/… as the upstream build does |
| net8.0 `csc.dll` of the S11 SDK (Roslyn 4.8 on SRM 8) | IL built from source (§6.11) | yes, compiles the CoreCLR CoreLib | the seed-era SRM 5 aligns RVA data to 4 only; CoreCLR rejects such `CreateSpan` data |
| System.CommandLine 2.0.0-beta4.23307.1 (`m612/net8-pkgs`, §6.11) | IL built from source | yes, crossgen2's command line | the version runtime 8.0.0 pins (`SystemCommandLineVersion`) |
| `Microsoft.DiaSymReader.Native` (crossgen2 layout), `StaticCs`, `Microsoft.DiaSymReader` (ILCompiler PackageReferences) | prebuilt packages (the first is a native Microsoft DLL) | **no** | DiaSymReader.Native is only copied next to crossgen2 for `--pdb` (native PDBs of R2R code), which is therefore unavailable; the other two are not referenced with `DotNetBuildFromSource=true` (`ClosedAttribute` comes from ILLink.Shared) |
| Windows' own .NET Framework 4.8 (`C:\Windows\Microsoft.NET`) | OS component | **no**, all paths redirected to Mono; verified by a `-v:diag` grep | out-of-proc MSBuild nodes and directly started `csc.exe` would use it, so we avoid both |
| dotnet/source-build-reference-packages `release/8.0` 5aeee273 (S12) | text: C# reference sources (GenAPI output), IL text of targeting packs, nuspecs, text-only packages (MSBuild SDKs, Microsoft.NETCore.Platforms runtime.json, source packages) | yes, compile time only | the VMR's own answer to reference packages; IL assembled by our CoreCLR `ilasm`, C# compiled by our Roslyn via `dotnet pack` of the SBRP projects; nothing from SBRP is shipped in the SDK (`s12-layout.sh check`). One reference source patched (sbrp/0001). `Icon.png` files are images |
| dotnet/arcade 7945b1b6, dotnet/xliff-tasks 73f08509, dotnet/xdt 9a1c3e1b, deployment-tools 5255d40e, command-line-api 02fe27cd, templating/sourcelink/NuGet/Roslyn/MSBuild/sdk tags (S12) | source tarballs | yes | test binaries deleted (arcade 44 test nupkgs/PEs, roslyn 162, NuGet 81, sdk 4, templating 2, runtime 30). Arcade's `tools/snk` Open/AspNetCore/cecil key *pairs* replaced by public keys (`keys/`) |
| Arcade's toolset packages: MicroBuild, Microsoft.DotNet.SignTool, Microsoft.DiaSymReader.Pdb2Pdb, Microsoft.DotNet.Build.Tasks.Feed/VisualStudio, NuGetRepack, SymbolUploader, IBC data, Microsoft.VSSDK.BuildTools | prebuilt tools | **no** | Arcade's `Tools.proj` restore is never run; source-build mode / `UsingTool*=false` (§6.13) |
| `Microsoft.Net.Compilers.Toolset` (pinned by Roslyn, MSBuild, sdk, roslyn-analyzers) | prebuilt compiler | **no** | `UsingToolMicrosoftNetCompilers=false`: the building SDK's own (from-source) Roslyn compiles |
| `Microsoft.CodeAnalysis.Analyzers` 3.3.4 (Roslyn's CSharpSyntaxGenerator) | prebuilt analyzer | **no**, an empty stub package (`s12-roslyn.sh boot`) | analyzers only report diagnostics; roslyn-analyzers needs Roslyn first |
| `Microsoft.CodeAnalysis.NetAnalyzers` 6.0.0, `Microsoft.SourceLink.GitHub` 1.0.0 (Newtonsoft.Json; NuGet on Windows) | prebuilt tool packages | **no** | Newtonsoft patch 0001; NuGet built with `IsXPlat=true` as its Linux source build |
| Microsoft.Build* 17.7.0, Newtonsoft.Json 13.0.1 (package-validation baselines) | prebuilt packages | **no** | `EnablePackageValidation=false` |
| `Microsoft.DotNet.Arcade.Sdk` of the previously-source-built tarball / Microsoft's feeds | prebuilt SDK package | **no** | `s12-arcade.sh` bootstraps Arcade from its own sources |
| Seed feed (`feed-seed`, 35 packages: runtime OOB libraries, HostModel, ApplicationInsights, DotNetHostResolver) | IL/native built from source in S8–S11 by the hand-made drivers | yes, S12 | stands in for the VMR's runtime build until dotnet/runtime's managed parts are built from their project files |
| nuget.org (user NuGet.Config) | package source | **no** | every repo's `NuGet.config` `<clear/>`s; `HTTP(S)_PROXY` points at a closed port during all S12 builds |

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
3e. *(Done: S12, §6.13: Arcade from source, SBRP, and Roslyn/MSBuild/NuGet/sdk/templating/command-line-api/
   deployment-tools/sourcelink/xdt/Newtonsoft rebuilt with `dotnet build` of their project files;
   stage 3 = stage 4 bit for bit.)*  Leftovers, in order:
   - **dotnet/runtime's managed parts from their project files**: first the out-of-band packages
     (Microsoft.Extensions.*, System.Configuration.ConfigurationManager, … with netstandard2.0 + net8.0),
     which needs Arcade's Microsoft.DotNet.Build.Tasks.TargetFramework/GenFacades packages (arcade repo),
     dotnet/sdk's ApiCompat task (built), a bootstrap ILLink, and Roslyn's Workspaces (Humanizer.Core from
     source, System.Composition); then the reference pack, CoreLib and the libraries (`build.cmd libs`
     subsets), ILLink and crossgen2. That retires the seed feed, most version overrides and `refpack.sh`.
   - roslyn-analyzers (same Workspaces prerequisite) → real Microsoft.CodeAnalysis.Analyzers instead of
     the stub, NetAnalyzers in the SDK, `EnableNETAnalyzers` back to its default.
   - vstest (`dotnet test`), dotnet-format, dotnet-watch; then `src/Layout/redist/redist.csproj`
     itself instead of `s12-layout.sh`, and dotnet/installer's `GenerateBundledVersions` for
     `BundledVersions.props`.
   - deterministic `.nupkg`s (the last stage-3/4 difference, through `deps.json` package hashes).
4. *(Done: CoreCLR on CLANG64, its CoreLib, crossgen2 and an R2R-compiled SDK root, §6.12.)*
   Leftovers, in order of usefulness:
   - report the clang/LLVM 22 `__finally` miscompile upstream (reproducers `fin7`/`fin10`)
     and drop patch 0013 once fixed; until then audit new `PAL_FINALLY`/`__finally` users;
   - make the R2R root the default S11 root (`sdk-layout-net8.sh` + `crossgen2.sh r2rfx r2rsdk`),
     R2R-compile crossgen2 itself, try composite/partial R2R with the from-source MIBC data;
   - ETW: generate the real manifest resources with an `mc.exe` replacement (the manifest is
     plain XML: `genEtwProvider.py` + a small `mc -um` emitter);
   - vxsort (its own `numeric_limits` clashes with libc++'s under `USE_STL`; MSVC `i64` literals);
   - DAC (`mscordaccore`) and DBI (`mscordbi`) for managed debugging, `createdump`;
   - `coreclr_static` + singlefilehost for `PublishSingleFile`;
   - the async-fault gap of the `/EHa` emulation (needs `-fasync-exceptions` for Itanium EH in
     clang, or an audit of VM code that relies on destructors during native AVs).

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
- **Outcome (§6.12):** CoreCLR was ported after all (patches 0008–0017); the paragraph
  below is the assessment from before the port.
- **Assessment:** not blocked in principle, but a multi-week port.
  Recommended order: (1) a GNU-on-Windows branch in
  `eng/native/configurecompiler.cmake` (no `mc`, no MASM for the host),
  then corehost (dotnet.exe/hostfxr/hostpolicy/apphost) with CLANG64 — this
  also unblocks the SDK's apphost; (2) the **Mono flavor** of dotnet/runtime
  8.0 for win-x64 with MinGW (C only, Mono has a MinGW history, and .NET on
  s390x/ppc64le ships this flavor) plus its CoreLib from S9; (3) CoreCLR on
  CLANG64 later.


## 10. C++/CLI frontend for the MinGW bootstrap

`bootstrap/m612/cppcli.sh` now builds a working C++/CLI subset using Clang
22.1.8. It produces executable or library CLR assemblies that run on the
source-built .NET 8 CoreCLR. The full compiler roadmap remains partially
implemented; `bootstrap/m612/cppcli/roadmap.json` records each stage, and
`capabilities.json` records the supported subset and its limitations.

The pipeline uses Clang preprocessing, a CLI syntax adapter, metadata
surrogate declarations, Clang's parser/Sema, an AST-to-IL emitter, and the
source-built `ilasm.exe`. The metadata helper uses `System.Reflection.Metadata`
to read assemblies without loading their code. Its default references come
from the source-built SDK pack. Additional imported types are selected with
`--type`; managed references use `#using` or `--reference`. The driver supports
`--target exe|library`, `--emit il|assembly`, `-I`, `-D`, and distinct assembly
identities. Intermediate metadata, adapted source, IL and AST evidence remain
in the output's `.work` directory.

The initial adapter failed the scope-lookup gate: a local variable named
`String` made lexical caret rewriting mistake native XOR for a managed type.
The implementation therefore builds a private parser from 18 SHA-256-pinned
LLVM 22.1.8 source files and links it into the Clang plugin. The patches keep
caret declarators and array indexing in Clang's normal scope/type lookup.
`adapter-gate.json` records this decision and its passing regression fixture.
Only source files are downloaded; the plugin links the trusted MSYS2
`clang-cpp` library. Rebuilding a small parser patch preserves timestamps of
unchanged parser sources.

The current tests exercise managed classes, constructors and field
initializers, class/interface inheritance, overloads and casts, ordinary C++
function templates, properties, rank-one managed arrays, and genuine CLR
catch/finally regions. C# consumers verify generated APIs and metadata,
virtual dispatch, property visibility, array bounds/null checks and storage
across a forced collection, and exception propagation. Invalid or unsupported
input is checked for a diagnostic and absence of a newly generated assembly.
The earlier GCC-hosted hello-world baseline remains in
`bootstrap/m612/cli-poc.sh`.

`--native-library cppcli-native.dll` binds used, undefined `extern "C"`
functions to a separate native DLL through CLR P/Invoke metadata. The native
fixture is built by UCRT64 MinGW g++ and called from both generated C++/CLI
and C#. The bridge currently permits integer/floating scalar signatures;
marshalling, pointers, callbacks and automatic native partitioning remain
open. It does not produce a mixed-mode PE or use `ijwhost`.

On the build host, the regression command is:

```sh
MSYSTEM=CLANG64 bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh \
  build test-core test-objects test-adapter-gate test-properties \
  test-virtuals test-arrays test-exceptions test-interop
```

The runner defaults to `m612/dotnet-sdk-8.0.131-stage2` and the source-built
CoreCLR `ilasm.exe`. Builds and C# consumer checks use an empty NuGet source
list; no prebuilt Microsoft binary or package was introduced. Native tools
and their libraries come from the trusted MSYS2 toolchain.

The frontend now also emits managed enums and value classes, including CLR
literal/field metadata, value copies, constructors, methods, arrays and imported
public struct fields. Explicit `safe_cast<Object^>(value)` boxes, and
`safe_cast<ValueType>(object)` unboxes with genuine CLR cast/null exceptions.
The private parser recognizes `%` tracking-reference declarators while keeping
ordinary remainder expressions intact. Parameters and local aliases use CLR
byref storage, including aliases into value fields and managed array elements.
C# consumers and re-imported generated assemblies exercise each feature;
`test-enums test-values test-tracking` extend the regression command above.

Implicit boxing, CLI generics, delegates/events/indexers, extended tracking
references/pinning, managed destruction/finalization, attributes, static
initialization and multiple translation units still require work.
Some surrogate rules also differ from full CLI semantics, notably interface
lookup and implicit Object conversions for interface-only classes. WPF's
DirectWriteForwarder and System.Printing have not been compiled by this
frontend. The next native milestone is genuine mixed-mode output and the
MinGW `ijwhost` port, followed by those WPF bridges.


## 11. Interim WinForms source package on .NET 8

`bootstrap/m612/winforms.sh` evaluates upstream source items into offline SDK
projects, preserving resources, reference assemblies, public signing keys and
interop source generation. Drawing.Common and Accessibility build from
WinForms 8.0.0 source. WinForms 8's Primitives assembly needs CsWin32-generated
bindings; its Win32Metadata/ClangSharp source bootstrap remains pending. The
planner rejects missing bindings with an explicit diagnostic instead of
restoring a binary generator or WinMD package.

The approved interim uses WinForms 7.0.20 source (which contains its native
bindings), compiled for net8.0 with assembly identities 8.0.0.0. Runtime OOB
libraries and Drawing.Common come from 8.0.0 source. The installed framework
version is explicitly `8.0.0-bootstrap.7.0.20`, with a provenance JSON recording
this compatibility port. It provides WindowsForms only: WPF, design assemblies
and localized satellites are not supplied.

Build/validation on win11:

```sh
MSYSTEM=CLANG64 bash ~/dotnetboot/repo/bootstrap/m612/winforms.sh fetch drawing
# Build Microsoft.Win32.SystemEvents before Drawing.Common, and the remaining
# runtime OOB dependencies before the steps below. The package recipe does this.
WINFORMS_VERSION=7.0.20 MSYSTEM=CLANG64 \
  bash ~/dotnetboot/repo/bootstrap/m612/winforms.sh \
  fetch accessibility generator primitives forms analyzers smoke layout
```

`tools/winforms-layout.py` stages shared-framework, reference and runtime packs,
plus upstream WindowsDesktop SDK XML and the WinForms application configuration
generator. The SDK profile includes the base runtime-pack mapping needed for
self-contained publish. Assembly identities are read from the source-built
assemblies; analyzer manifest entries carry the metadata the SDK expects.

`winforms-tests/check-sdk.sh <isolated-sdk-root> <work>` builds a standard
`net8.0-windows` / `UseWindowsForms=true` application against an empty NuGet feed,
uses `ApplicationConfiguration.Initialize()`, opens a real form with controls,
checks events, embedded dialog/icon resources and GDI+ pixels, and closes through
the message loop. It runs both shared-framework and self-contained outputs.
The sibling msys2-ur recipe `mingw-w64-dotnet-winforms-bootstrap` rebuilds its
Microsoft inputs from pinned source archives and includes these checks.

The frontend driver also accepts `--framework windowsforms`, selecting the
source-built desktop pack, the common Form/Control/Application metadata graph
and the matching runtime configuration. `winforms-tests/check-cppcli.sh` compiles
`hello.cpp` with that flag, shows/closes a real Form and verifies its window
handle. The packaged command is `cli-clang --framework windowsforms hello.cpp
-o hello.dll`, followed by `dotnet hello.dll`.

## 12. WPF managed foundations and native dependency inventory

On 2026-10-12 the current Greenshot source was checked for packaging. It targets
.NET Framework 4.8 and uses WPF in its application, editor and capture code;
the Light edition still requires WPF. The user selected current Greenshot and
WPF support rather than an interim port of the older WinForms-only release.
No Greenshot package or complete WPF shared framework has been produced.

`bootstrap/m612/wpf.sh` now builds these v8.0.0 WPF source projects with the
installed, source-built SDK and an empty NuGet feed:

- `System.Windows.Input.Manipulations` (26 source files);
- `System.Xaml` (181 source files);
- `WindowsBase` (318 source files, including the upstream Perl-generated XML table);
- `UIAutomationTypes` (64 source files);
- `UIAutomationProvider` (31 source files).

The driver also builds `System.IO.Packaging` and `System.Security.Permissions`
from runtime v8.0.0 source. Existing source-built OOB libraries and the interim
WinForms framework supply the remaining managed dependencies. Public signing
uses the upstream assembly keys, including WindowsBase's MicrosoftShared key.
`tools/wpfplan.py` preserves the projects' default-item settings, compile/remove
lists, embedded resource names and declared project dependencies. References to
native DLLs and reverse dependencies are excluded from managed compilation.

The source-built `PresentationNative_cor3.dll` currently implements 38 Win32
and OS-version exports. Its window wrappers preserve Unicode and clear stale
last-error values when a zero result is a successful Win32 operation. This is
only the Win32 portion; it provides no text or paragraph-layout stubs.

Validation on win11, from the staged driver at `~/wpfboot/driver`:

```sh
M612=/home/topkek/dotnetboot/m612 MSYSTEM=CLANG64 \
  WPF_BUILD=/home/topkek/wpfboot/build \
  bash ~/wpfboot/driver/bootstrap/m612/wpf.sh \
  foundations native-win32 check-foundations inventory
```

All five assembly builds and the check finished with zero warnings and errors.
The check runs on the source-built CoreCLR and exercises Point/Rect geometry,
embedded error strings, XAML parsing and serialization, automation assembly
identity, dependency-property defaults and updates, dispatcher thread affinity
and its Win32 message loop, native Unicode window text and last-error behavior,
the native OS-version bool ABI, and manipulation events/translation.
Outputs and source-item manifests remain under `~/wpfboot/build`; no installed
SDK/shared-framework files are modified by these foundation checks.

The next build gate is `DirectWriteForwarder`: planning PresentationCore stops
with `Build WPF dependency first: DirectWriteForwarder`. The initial
packaged frontend stopped at included-header `#using` and missing `vcclr.h`.
The development frontend now handles these, boxed value handles, static fields
and managed metric layouts (see §13–16). Default indexed properties, named imported-interface overrides and interface
property declarations are now supported (see §18). The full upstream translation
unit now parses `for each` (see §19) and COM interface attributes (see §20).
Native declaration visibility and text-analysis delegates are now supported (see §21).
Managed type tokens/reliability metadata and the associated imports now pass
(see §22). The full gate now passes Factory's private constructor access, concrete Object
upcasts, String constructor literals, imported static properties and Action binding
(see §23). Omitted parameter arrays and Factory's actual dispatcher scheduling
expression now compile and execute (see §24). Primitive static members and WPF
font-version parsing now pass (see §25). Guid literal construction and managed
value-to-Object boxing now pass (see §26). Closed C++ managed templates and their
out-of-line properties now pass (see §27). Fundamental keyword static scopes,
nested indexers, default/assigned pins and direct managed-heap interior returns
now pass (see §28). The complete amalgamation passes Clang parsing and Sema;
Native Guid references and opaque trivial native record copies now pass (see
§29). Source destructor disposal and genuine nested ref/value metadata now pass
(§30), including the original LocalizedStringsEnumerator and its getters.
GC-free managed value pointers now pass (§31), including the original
GlyphMetrics header/native ABI. Windows API imports and native FILETIME
aggregate/field operations now pass (§32); the complete original util2.cpp
is compiled and checked against actual registry timestamps. The TrueType
subsetter now builds from its actual upstream algorithm into a native DLL, with
a checked managed split bridge (§33). Clang-proven side-effect-free SDK constexpr
integer calls also pass, including MinGW's constant enum flag operator. IL
IUnknown dispatch and scalar/POD native allocation/deletion now pass (§34),
including the original NativePointerWrapper.cpp cleanup methods. IL collection
now supports the DWrite factory function-pointer prototype, SDK UUID reference
constants and native sizeof (§35). The full gate includes ported main.cpp startup
with a source-built DWrite loader. It next fails at an unsupported compound
assignment in Font.cpp:193.
General native record operations, open managed
generics, callbacks, finalizers and automatic native
partitioning also remain outside its supported subset.
The same compiler failure can be reproduced with `wpf.sh bridge-gate`; it
retains the original Clang diagnostics in `build/directwrite-gate.log`.
The System.Printing bridge, higher-level managed libraries, WPF build tasks and
desktop framework/SDK packaging also remain unfinished.

There is an additional independent source-only blocker. Upstream's
`redist/PresentationNative/PresentationNative.vcxproj` discovers and copies the
DLL from a binary package rather than compiling its source. The managed WPF
source references 139 unique exports from it. `tools/wpfnative-inventory.py`
records each caller and its category in `build/native-coverage.json`; 38 are
implemented and 101 remain, including Unicode classification, line services,
paragraph layout and natural-language services. `--require-complete` deliberately
fails while any required export is absent. Completing the C++/CLI bridges alone
will therefore not complete source-only WPF. The existing native gfx/PenImc
packages are v10.0.12; their versions and rendering protocol must also be matched
to the eventual managed framework before a rendered-window check or Greenshot
package can be claimed.


## 13. C++/CLI bridge prerequisites and a real DirectWrite probe

On 2026-10-12 bridge work added these source-built frontend capabilities:

- `#using` in transitively included headers. A Clang VFS wrapper raw-lexes only
  the directive spelling, keeping file identities, include resolution, macro
  paths, inactive conditional branches and source locations. Continued directive
  names work; comments and literals are not rewritten. Its raw-lexer source
  manager has independent diagnostics so subsequent include warnings remain valid.
- `as_friend` imports with validated `InternalsVisibleTo` assembly names and full
  public keys. Internal types/members are imported only for a declared friend;
  private members remain inaccessible, and protected members retain Clang access
  checks. `--public-key` emits the assembly's public-key identity, with no private
  signing key. Signed friend calls were executed on source-built CoreCLR.
  This follows the CLR access rules described in
  [Microsoft's friend-assembly documentation](https://learn.microsoft.com/en-us/dotnet/api/system.runtime.compilerservices.internalsvisibletoattribute).
- A source `include/vcclr.h` with intrinsic declarations for primitive
  `pin_ptr`, `interior_ptr` and `PtrToStringChars`. The emitter gives pin locals
  genuine CLR `T& pinned` storage and clears the slots in a lexical `finally`
  region. Returns, exceptions, break and continue run this cleanup. String
  addresses use `String.GetPinnableReference`; array addresses use `ldelema`.
  Interior pointers use tracked CLR byrefs. These are not native pointer aliases.
- Raw unmanaged pointer arguments/returns to split `extern "C"` native DLLs.
  Pin-to-native conversion emits `conv.u` while retaining the actual pin local.
  Automatic marshalling, unmanaged pointer arithmetic, managed callbacks,
  nonprimitive pinning, managed pointer assignments and intrinsic returns are still
  unsupported. Pin locals in for-initializers, pin parameters/returns and managed
  pointer fields are rejected until their lifetimes can be handled.
- Bare `gcnew Type` default allocation, resolved by Clang's constructor checking.
  System-header line markers survive reparsing, so libc++ can specialize its
  own restricted templates while identical user specializations are rejected.
  `--std` selects C++14, C++17 (default) or C++20 preprocessing/parser mode.

`cppcli.sh test-references` builds a public-signed C# friend library with the
source SDK and calls its internal APIs from a generated C++/CLI assembly. It
also rejects wrong names/keys, imports without friend access, private/protected
access and trailing directive tokens. Header references include nested relative
includes, pragma-once deduplication, a macro path, a continued directive name,
inactive references and a diagnostic after header shimming.

`cppcli.sh test-pinning` uses dynamically allocated Unicode strings and mutable
UInt16 arrays. A native MinGW DLL reads their pointers, waits during concurrent
forced compacting GC and mutates the array. The C# consumer checks the data and
actual pinned-local/finally metadata. It also exercises interior-pointer reads
across GC, early returns, exceptions and loop scope exits. Unsafe/unimplemented
constructs produce diagnostics rather than an assembly.

Exact development validation on win11:

```sh
MSYSTEM=CLANG64 M612=/home/topkek/dotnetboot/m612 \
  bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh \
  test-core test-objects test-references test-properties test-virtuals \
  test-arrays test-exceptions test-interop test-enums test-values \
  test-tracking test-adapter-gate test-pinning
```

All suites passed. The focused native rebuild was
`/clang64/bin/cmake.exe --build /home/topkek/dotnetboot/m612/build/cppcli --target cli_ast -j2`;
the metadata reader was rebuilt by `cppcli.sh build` with zero warnings/errors.

`wpf.sh check-directwrite-bridge` builds `wpf-native/DirectWriteBridge.cpp` with
MinGW Clang and `-ldwrite`, then compiles `wpf-tests/DirectWriteBridge.cpp` through
this frontend. A C# consumer calls the resulting managed assembly on the source
CoreCLR. It creates/releases a real DirectWrite factory, enumerates installed
font families and reads a localized family name into a pinned managed Char
array, checking required capacity, NUL termination and empty-array rejection.

```sh
M612=/home/topkek/dotnetboot/m612 MSYSTEM=CLANG64 \
  WPF_BUILD=/home/topkek/wpfboot/build \
  WPF_CPPCLI_BUILD=/home/topkek/dotnetboot/m612/build/cppcli \
  bash ~/wpfboot/driver/bootstrap/m612/wpf.sh check-directwrite-bridge
```

Result: zero warnings/errors; `DirectWrite bridge OK: 81 families; first family Arial`.
The probe's assemblies/native DLL remain in `~/wpfboot/build/directwrite-check`.
They are an incremental integration check, not a replacement with the upstream
DirectWriteForwarder assembly/API identity, and are not installed or packaged.

The same environment with `bridge-gate` compiles the real v8.0.0 amalgamation.
It uses WindowsBase's MSShared public key, C++14 for the upstream legacy code,
project include roots and maps five SAL1 spellings to the existing MinGW SAL2
annotations. Preprocessing, friend validation and adaptation now complete.
Clang parsing/Sema still returns nonzero: the first failure is the implicit
boxed `System.DateTime^` assignment in `TrueTypeSubsetter/util2.cpp:27`; subsequent
failures include managed static initialization and missing Threading imports.
The complete diagnostics are retained in `~/wpfboot/build/directwrite-gate.log`.
Full DirectWriteForwarder and System.Printing, complete WPF/WindowsDesktop and
Greenshot packaging remain unfinished. The separate PresentationNative export
and native gfx/PenImc version issues in §12 still apply.


## 14. Upstream bridge boxing, static state and native integers

The 2026-10-12 continuation moved the full DirectWriteForwarder build past
boxed `DateTime`, managed static lock initialization, internal access labels,
static properties, native integer imports and MinGW DirectWrite UUID namespace
failures. Full WPF is still not packaged or verified: the bridges and the
independent native requirements in §12 remain incomplete.

The frontend now supports implicit boxing of a selected managed value class
into its matching `T^` handle and `gcnew T(...)` boxed allocation. Boxes retain
CLR object identity; field and method updates use `unbox` addresses rather than
copying the payload. `*box` reads a value copy. Value-class `this` remains a
managed address. Signature metadata uses `System.ValueType` with the concrete
value type and `IsBoxed` optional modifiers, and the reader preserves these
when importing another generated assembly. This is consistent with the
[documented CLR modifier purpose](https://learn.microsoft.com/en-us/dotnet/api/system.runtime.compilerservices.isboxed?view=net-10.0).
Boxing into Object/interfaces without `safe_cast`, primitive boxes, general
boxed-handle upcasts and constrained dispatch still need work. Native `*`
pointers to managed value classes remain rejected in emitted CLI declarations.

Managed static field initializers lower into ordered CLR `.cctor` calls, with
real static field loads/stores/addresses. The runtime handles concurrent first
access and failed type initialization. Non-indexed static properties have real
CLR getters/setters, including out-of-line `Class::Property::get/set` source
definitions. Namespace-private types emit nonpublic type identities; internal
members emit assembly access. Indexed/static auto-properties, compound static
property updates, source-defined static constructors and general CLI attributes
remain unimplemented.

Metadata imports now represent `IntPtr`/`UIntPtr` as pointer-sized CLR native
integers with corresponding C++ value declarations. The driver imports these
core types by default. Primitive pointer signatures, such as `IntPtr(void*)`
and `ToPointer()`, are imported without automatic marshalling. Imported static
fields retain readonly checks; readonly value receivers use defensive copies.
CLR operator overloads that differ only by return type keep distinct annotated
metadata declarations instead of colliding as C++ methods. General C++ operator
syntax binding for these imports still requires work. Assembly references with
hyphenated names are quoted in emitted IL, including optional modifiers.

Two WPF v8.0.0 source patches are applied idempotently by
`wpf.sh prepare-bridges`:

- `0001-mingw-checked-integers.patch` uses MinGW's existing checked integer
  operations for the four functions copied from Microsoft's intsafe header.
  The check verifies successful results, overflow HRESULTs and failure sentinels
  for addition, conversion, multiplication and subtraction.
- `0002-mingw-dwrite-namespace.patch` includes MinGW's DirectWrite header at
  global scope so its UUID template specializations are in the correct namespace.
  The original MSVC include path is retained for non-MinGW builds.

`wpf.sh bridge-gate` now supplies the source-built threading/interoperability
reference assemblies and System.IO.Packaging, the latter providing the
FileFormatException forwarded by WindowsBase. Current first Sema failures are
missing Guid/exception/layout metadata imports and the cast from pinned byte
storage to a native GUID pointer in `DWriteWrapper/Common.h`. Native record
layout, attributes, templates, callbacks and automatic native partitioning are
still needed; the full upstream amalgamation deliberately returns nonzero.

Focused checks added to `cppcli.sh`:

```sh
MSYSTEM=CLANG64 M612=/home/topkek/dotnetboot/m612 \
  bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh \
  test-boxed-values test-static-fields test-static-properties test-nativeints
```

All passed on the source-built CoreCLR. These checks cover the real WPF
`DateTime::FromFileTimeUtc(...)->Ticks` pattern, boxing copy/alias semantics,
mutable box methods and fields, null and GC behavior, C# reflection/consumption,
a second C++/CLI assembly importing typed boxes, parallel static initialization,
initialization failure caching, private/internal visibility, pointer-sized API
signatures, unmanaged memory allocation/free and Marshal.Copy into managed bytes.
The rebuilt metadata tool and native plugin passed, and all 17 frontend suites
passed after the native integer changes. Later modifier/assembly-quoting changes
were validated with boxed-library imports and the affected import suites.

The actual upstream `LocalizedErrorMsgs.cpp` and header now compile through the
frontend, without rewriting their class bodies. `wpf.sh check-localized-messages`
wraps them with a small public check facade, then validates Unicode roundtrips,
concurrent Monitor-protected access, nonpublic type identity, assembly-level
accessors and the CLR type initializer. This is a real compiled component of
DirectWriteForwarder; it does not establish completion of the entire bridge.

```sh
M612=/home/topkek/dotnetboot/m612 MSYSTEM=CLANG64 \
  WPF_BUILD=/home/topkek/wpfboot/build \
  WPF_CPPCLI_BUILD=/home/topkek/dotnetboot/m612/build/cppcli \
  bash ~/wpfboot/driver/bootstrap/m612/wpf.sh \
  check-checked-integers check-localized-messages check-directwrite-bridge
```

Results: `WPF checked integers OK`, `upstream WPF LocalizedErrorMsgs OK`, and
`DirectWrite bridge OK: 81 families; first family Arial`. Managed check builds
had zero warnings/errors. Outputs remain in `~/wpfboot/build`; the complete
upstream gate diagnostics remain in `build/directwrite-gate.log`.
No full WPF shared framework, WindowsDesktop SDK or Greenshot package is
claimed. The goal remains building and packaging the complete source-only WPF
stack, then current Greenshot.

## 15. WPF metric layouts and explicit pinned-buffer casts

The next 2026-10-12 continuation added `StructLayout`/`FieldOffset` support to
the development frontend. Attribute arguments remain expressions checked and
evaluated by Clang. The IL backend emits actual CLI explicit/sequential layout,
field offsets, packing, size and charset; managed value types now default to
sequential layout. It rejects missing or misplaced offsets, invalid sizes and
packing, and nonconstant arguments. Layout attributes preserve internal field
visibility. General custom attributes, layout inheritance and bitfields remain
unfinished. These rules follow the documented [StructLayout semantics](https://learn.microsoft.com/en-us/dotnet/api/system.runtime.interopservices.structlayoutattribute)
and [required explicit offsets](https://learn.microsoft.com/en-us/cpp/error-messages/compiler-errors-2/compiler-error-c3272?view=msvc-170).

`wpf.sh check-metrics-layout` compiles the unchanged upstream `FontMetrics.h`
and `GlyphMetrics.h`, using standard fixed-width aliases for the Windows scalar
types. The consumer checks their actual nonpublic class/value-type identities,
all declared offsets, and the upstream Baseline/LineSpacing calculations.
The glyph structure is also marshalled into a native MinGW DLL compiled against
`dwrite.h`: every offset and its 28-byte size match `DWRITE_GLYPH_METRICS`, and
native code reads all seven signed/unsigned values correctly. FontMetrics has
the offsets declared upstream and is converted field by field in the upstream
converter; this check does not claim its raw layout matches DWRITE_FONT_METRICS.
Result: `upstream WPF font/glyph layout OK: native glyph size 28`, with zero
managed build warnings/errors.

The `vcclr.h` pin intrinsic also accepts explicit C-style/static casts to native
pointer types. The emitter preserves the actual pinned CLR local and converts
its address, without creating a native pin object. New tests reinterpret pinned
bytes as a native UInt32, read and modify the word in a native DLL during forced
compacting collections, then observe the updated bytes. Unrelated implicit
pointer conversions and casts discarding element const qualification are
rejected. This implements the [documented pin-pointer cast pattern](https://learn.microsoft.com/en-us/cpp/extensions/pin-ptr-cpp-cli?view=msvc-170);
direct reinterpret_cast on the surrogate and other pointer operations remain
outside the implemented subset.

Validation on win11:

```sh
MSYSTEM=CLANG64 M612=/home/topkek/dotnetboot/m612 \
  bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh test-layout test-pinning

M612=/home/topkek/dotnetboot/m612 MSYSTEM=CLANG64 \
  WPF_BUILD=/home/topkek/wpfboot/build \
  WPF_CPPCLI_BUILD=/home/topkek/dotnetboot/m612/build/cppcli \
  bash ~/wpfboot/driver/bootstrap/m612/wpf.sh \
  check-metrics-layout check-localized-messages check-directwrite-bridge
```

The layout and pinning checks passed. The affected value, boxed-value, instance
property, virtual, static-field and static-property suites also passed after
the layout change. Rebuilds use `cmake --build ... --target cli_ast -j2`;
the pin-header change additionally used `cppcli.sh build` to copy that header.
The full `wpf.sh bridge-gate` still returns nonzero. Its metadata imports now
include Guid, the needed exception types, layout types, Point and
TextFormattingMode from the already source-built WindowsBase. Remaining first
errors concern managed enum bitwise/conversion semantics; complete native
record interop and automatic partitioning are still required after Sema.
The installed compiler package remains the older build. No complete WPF,
WindowsDesktop or Greenshot package has been produced; the separate 101 missing
PresentationNative exports remain unchanged.

## 16. Managed enum operations, switches and static constructors

The following 2026-10-12 continuation advanced the complete DirectWriteForwarder
Sema gate through its enum expressions, static constructor declarations and
virtual property declarations. The current first failures are the generic
collection types and explicit interface implementations in LocalizedStrings
and FontList. Full bridge emission and WPF packaging remain unfinished.

The private Clang parser now asks Sema to apply bitwise OR/AND/XOR and complement
to a CLI enum's underlying integer, then convert the result back to the same
enum. Native scoped enums and ordinary integer/XOR expressions retain their
original behavior. Different enum types cannot be combined, and implicit enum
to integer conversions remain rejected, consistent with [CLI enum rules](https://learn.microsoft.com/en-us/cpp/dotnet/how-to-define-and-consume-enums-in-cpp-cli?view=msvc-170).
Integral switches permit CLI enum case constants as used in WPF's original
converter; enum switches retain their scoped labels. `[Flags]` now emits the
actual FlagsAttribute on the enum type, which is verified by reflection and
Enum.ToString. The imported-assembly check also caught an IL keyword collision
for an assembly named `switch`; single-word assembly scopes are now quoted.

The IL backend implements switch dispatch with one evaluation of the condition,
constant comparisons, default and fallthrough. Nested switch/loop break and
continue targets are distinct, and branches leaving exception regions use
`leave`, preserving finally cleanup. Tests cover sparse/stacked labels, signed
and unsigned 64-bit constants, no-default switches and CLR exceptions. Switch
initializers and GNU ranges are rejected, as are labels entering generated
protected/pinned scopes. Enum additive/increment/compound operations remain
incomplete.

Inline and out-of-line static constructors now become one private CLR `.cctor`,
combining ordered field initializers with the source body. The tests exercise
ref/value classes, concurrent first access, failure caching, and coexistence
with ordinary instance constructors. Missing definitions, parameters and
initializer lists are rejected. Interface static constructors and linking
definitions across translation units remain unsupported. Virtual properties
transfer their virtual flag to the accessors and preserve CLR override slots;
C# overrides dispatch correctly through generated getters/setters.

`wpf.sh check-font-simulations` compiles the two unchanged font-simulation
conversion bodies from upstream DWriteTypeConverter.cpp, its original
FontSimulation.h, and the native enum declaration from the installed MinGW
DWrite header. A reduced declaration of the containing class and a public test
facade supply the check context; the rest of DWriteTypeConverter is not compiled
by this check. Both bold/oblique flags roundtrip, unknown flags take the original
exception path, and the original nonpublic enum formats as `Bold, Oblique`.
Result: `upstream WPF font simulation conversions OK`, zero build warnings/errors.

Focused validation on win11:

```sh
MSYSTEM=CLANG64 M612=/home/topkek/dotnetboot/m612 \
  bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh \
  test-switch test-static-fields test-virtuals test-properties

M612=/home/topkek/dotnetboot/m612 MSYSTEM=CLANG64 \
  WPF_BUILD=/home/topkek/wpfboot/build \
  WPF_CPPCLI_BUILD=/home/topkek/dotnetboot/m612/build/cppcli \
  bash ~/wpfboot/driver/bootstrap/m612/wpf.sh \
  check-font-simulations check-localized-messages
```

These checks passed. The affected core, enum-import, parser-shadowing, exception,
pinning, static-property and boxed-import suites also passed. The rebuilt
metadata tool had zero warnings/errors; frontend/parser rebuilds used
`cmake --build ... --target cli_ast -j2`. The complete `bridge-gate` still fails
and preserves its diagnostics; CriticalHandle, Interlocked, constrained-execution
metadata, CultureInfo and source-built WindowsBase's internal Invariant are now
real imports. Neither generic collection APIs nor native layout/emission,
callbacks, finalizers or automatic native partitioning are complete. The
installed compiler package remains the earlier release. Complete WPF,
WindowsDesktop and current Greenshot remain unbuilt and unpackaged; the native
PresentationNative text/layout export gap is unchanged.

## 17. Imported CLR generic types and WPF collection declarations

The next 2026-10-12 continuation added structured generic signatures to the
metadata reader. Imported generic definitions become annotated C++ template
declarations, with CLR generic VAR slots retained in their member signatures.
Array, byref and nested generic arguments are qualified recursively. The backend
emits constructed CLR types and bound member references, including generic
constructors and instance/static fields. The actual generic declaration's
`!0`/`!1` signatures remain on the member references, rather than being replaced
by the caller's concrete types. CLR type identity is checked from C# reflection.
This covers [consuming generic classes](https://learn.microsoft.com/en-us/cpp/extensions/generic-classes-cpp-cli?view=msvc-170)
needed by WPF; generic declarations/methods, variance/constraint validation and
nested imported types remain unfinished.

`cppcli.sh test-generics` passes on source-built CoreCLR. It calls actual
List<int> and Dictionary<string,int> APIs, tests a dictionary's byref output,
constructs KeyValuePair<string,int> and Nullable<int>, and checks nested
List<List<int>> returns. Source-built fixture types verify generic field mutation,
constructor binding through a generic base class, and independent static fields
for Slot<int> and Slot<long>. Generated classes implement IComparer<int> and a
two-argument generic interface consumed by C#. Missing interface slots, direct
interface allocation and native pointer generic arguments are rejected.
Two C++ warnings about undefined native template-field storage remain on the
CLR field facades; the emitted fields reference the real CLR storage and pass
the runtime checks. All managed check builds have zero warnings/errors.

Generic and nongeneric enumeration interfaces are also imported and exercised
through a real List<int> enumerator, including MoveNext, Current and Dispose in
finally. The C++ model coalesces repeated interface inheritance paths with
virtual surrogate bases. Nongeneric Current/GetEnumerator declarations avoid
native C++ return-type override rules, while preserving their CLR signatures.
Imported interface completeness is validated by the backend; the facades no
longer impose C++ pure-method requirements on real imported CLR classes whose
member set is only partially selected. The interfaces remain actual CLR
interfaces, and attempts to allocate them are rejected.

The full WPF gate now imports IDictionary, ICollection, IEnumerable,
IEnumerator and KeyValuePair from the source-built targeting pack, along with
nongeneric enumeration and IDisposable. A token-adapter bug that inserted
`public` after commas inside generic base arguments was fixed. The gate now
gets through its generic collection declarations and the enumeration return-type
conflict; the current first source failures are default indexed properties and
named overrides in LocalizedStrings and FontList. The complete bridge still
does not compile.

Validation on win11:

```sh
MSYSTEM=CLANG64 M612=/home/topkek/dotnetboot/m612 \
  bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh \
  test-generics test-virtuals test-values test-boxed-values test-nativeints
```

This command passed after the focused frontend rebuild
`cmake --build /home/topkek/dotnetboot/m612/build/cppcli --target cli_ast -j2`.
The earlier affected friend/reference suite also passed. The metadata reader
rebuild had zero warnings/errors. Current gate diagnostics remain in
`~/wpfboot/build/directwrite-gate.log`; the installed frontend package has not
been replaced by this development build. Source-defined generic types, indexed
properties, named overrides, native record emission/partitioning, callbacks and
finalizers still need work. Full WPF, WindowsDesktop and current Greenshot
remain unpackaged, with the independent PresentationNative export gap unchanged.


## 18. WPF indexers, named overrides and interface declarations

The 2026-10-12 continuation adds one-parameter indexed properties through
Clang's existing MSPropertySubscriptExpr/PseudoObjectExpr machinery. A small
parser hook resolves the default property on a managed handle before asking
Sema to bind its indexed accessors. Native pointer subscripts and managed array
subscripts retain their existing paths. Imported default-property metadata now
exposes String.Chars and Dictionary<TKey,TValue>.Item as indexed properties.
Source default properties emit Item property rows and a real CLR
DefaultMemberAttribute. Matched numeric/string overloads emit separate property
rows and share a single C++ facade; original out-of-line default::get/set
methods bind to the corresponding get_Item/set_Item declarations.

The indexer checks verify side-effectful receiver/index evaluation exactly once,
getter/setter counts, generic dictionary mutation, Unicode string indexing,
null receivers, C# reflection/consumption, internal accessor visibility and
importing the generated overloaded indexers into a second CLI assembly.
Invalid getter/value/index types and multiple index parameters are rejected.
Overloads currently require the same value type and accessor forms; static
indexers, multiple indices and consuming a compound update as a value remain
outside this subset.

Named imported-interface overrides are converted to typed annotations, with
Clang resolving the target interface. The emitter checks implementation,
instance/virtual status and matching parameter/return types, then emits CLR
.override mappings. The tests verify enumeration through IEnumerable and
IEnumerator, a renamed private/final GetEnumerator2 and a renamed property
getter, including interface maps from C#. One imported-interface target is
supported; renamed source-interface implementations, lists and class targets
remain pending.

Raw token protection handles interface class/struct before the Windows
interface macro can expand it. Directive definitions and ordinary native
interface declarations remain subject to preprocessing. Interface property
accessors become abstract virtual methods. Parameter attributes inside an
interface method are excluded from method-boundary detection. Modifier
placement before handle return types is preserved.

`wpf.sh check-font-source` compiles the unchanged upstream IFontSource.h in a
minimal context with the same interface macro definition used by Windows
headers. The check uses source-built DateTime, Uri and UnmanagedMemoryStream
references and validates both private interfaces, all five font-source methods,
read-only property types, abstract/virtual flags and the factory signature on
CoreCLR. This is a managed header check, not a full native-header/bridge build;
including all Windows inline helper bodies in the IL emitter still reaches
unsupported native reference returns and requires native partitioning.

Validation on win11:

```sh
MSYSTEM=CLANG64 bash -l -c "/clang64/bin/cmake.exe --build /home/topkek/dotnetboot/m612/build/cppcli --target cli_ast -j2"
MSYSTEM=CLANG64 M612=/home/topkek/dotnetboot/m612 \
  bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh \
  test-indexers test-named-overrides test-static-properties test-virtuals
M612=/home/topkek/dotnetboot/m612 MSYSTEM=CLANG64 \
  WPF_BUILD=/home/topkek/wpfboot/build \
  WPF_CPPCLI_BUILD=/home/topkek/dotnetboot/m612/build/cppcli \
  bash ~/wpfboot/driver/bootstrap/m612/wpf.sh \
  check-font-source check-localized-messages
```

The focused compiler checks pass. Managed check builds have zero warnings and
errors; the existing virtuals.cpp missing-override Clang warning remains.
The font-source check reports `upstream WPF IFontSource interfaces OK` and the
localized-messages check verifies its upstream static state and monitor locking.
Properties and the native-XOR adapter gate also passed in the earlier regression
run. Logs and source probes are retained under
`/home/kreijstal/tmp/wpf8-support/validation/indexers-20261012`.

The complete `wpf.sh bridge-gate` still exits nonzero. It now passes the indexed
property and IFontSource declaration failures and reaches `for each` in
LocalizedStrings.h plus PreserveSig/COM attribute declarations in
DWriteInterfaces.h. These attributes must produce real interop metadata;
removing them would not implement the bridge. Full DirectWriteForwarder,
System.Printing, PresentationCore/PresentationFramework, the missing native
exports, WindowsDesktop packaging and current Greenshot remain unfinished.
The installed frontend package still predates these development changes.


## 19. CLR for each loops and the upstream localized-string copy method

The following 2026-10-12 continuation adds `for each` over rank-one managed
arrays and reference enumerators. Only the CLI loop boundary is lowered;
Clang still parses the collection expression, element declaration and loop
body. Unevaluated helper declarations preserve Sema's selected GetEnumerator,
MoveNext and get_Current methods. The emitter uses these declarations to
construct actual CLR calls, including bound generic member references. Arrays
use typed element loads with CLR bounds checks. Element conversion preserves
value copies, unboxes nongeneric values and casts reference elements; numeric
safe_cast now includes identity conversion, unsigned floating conversion and
boolean tests at the original source width.

Collection evaluation occurs once. Reference enumerators are cleaned up in a
real CLR finally region via IDisposable when implemented. Normal completion,
break, continue, early return and exceptions retain protected-region control
flow. Null and nondisposable enumerators do not receive a Dispose call. Nested
loops keep separate enumerators, and property-accessor bodies use the same
lowering. The behavior follows the enumeration/cleanup rules in
[ECMA-372 §16.2.1](https://www.ecma-international.org/wp-content/uploads/ECMA-372_1st_edition_december_2005.pdf).
This remains a subset: bodies must be braced, variables cannot be tracking
references, MoveNext must return bool in this subset, and enumerators must be
managed reference types. Value/native
enumerators and broader collection discovery remain pending; metadata types
still require explicit selection.

`cppcli.sh test-each` verifies receiver/GetEnumerator/MoveNext/Current/Dispose
counts, generic and nongeneric dispatch, explicit IEnumerable implementations,
nondisposable enumerators, nested-loop cleanup, early returns from property
accessors, UTF-16 character enumeration (including surrogate pairs), primitive
unboxing/reference cast failures, empty/null arrays and numeric conversions.
It injects failures in acquisition, MoveNext, Current, Dispose and the loop
body. Nonboolean MoveNext return types, incompatible element types, unbraced
bodies and tracking-reference variables are rejected.

`tools/wpf-localized-copy-check.py` extracts the unchanged upstream
LocalizedStrings.h CopyTo declaration/body into a collection probe. The
surrounding class supplies an actual IEnumerable<KeyValuePair<CultureInfo^,
String^>>; the copied upstream method retains its for each loop and array
assignment. `wpf.sh check-localized-copy` builds this method using the frontend
and source-built ilasm, then verifies Unicode values, offset slots and disposal
on normal and out-of-range/null-array exits. It also preserves the original
empty-input behavior. This proves the original CopyTo method, not the complete
LocalizedStrings class or DirectWriteForwarder assembly.

Validation on win11:

```sh
MSYSTEM=CLANG64 M612=/home/topkek/dotnetboot/m612 \
  bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh build
MSYSTEM=CLANG64 bash -l -c "/clang64/bin/cmake.exe --build /home/topkek/dotnetboot/m612/build/cppcli --target cli_ast -j2"
MSYSTEM=CLANG64 M612=/home/topkek/dotnetboot/m612 \
  bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh \
  test-each test-exceptions test-generics test-boxed-values test-core
M612=/home/topkek/dotnetboot/m612 MSYSTEM=CLANG64 \
  WPF_BUILD=/home/topkek/wpfboot/build \
  WPF_CPPCLI_BUILD=/home/topkek/dotnetboot/m612/build/cppcli \
  bash ~/wpfboot/driver/bootstrap/m612/wpf.sh check-localized-copy
```

Builds and focused checks pass. The final `test-each` run also covers wide
boolean/unsigned floating conversion and direct String enumeration;
`check-localized-copy` was rerun after the final emitter change. Managed builds
have zero warnings/errors. The existing generic static-field C++ facade
warnings remain in the generic regression. Results include
`CLR for each enumeration and cleanup OK` and
`upstream WPF LocalizedStrings::CopyTo OK`. Logs, the extracted upstream method
probe and its IL are retained under
`/home/kreijstal/tmp/wpf8-support/validation/each-20261012`.

The complete `wpf.sh bridge-gate` still exits nonzero. Its first errors are now
PreserveSig declarations in DWriteInterfaces.h; the original LocalizedStrings
for each error is absent. Correct COM import/GUID/interface/marshalling metadata
is the next source gate. Native partitioning, callbacks, finalizers, full
System.Printing and higher-level WPF assemblies, missing PresentationNative
exports and WindowsDesktop packaging remain unfinished. Current Greenshot
cannot yet be packaged on this WPF stack, and the installed frontend package
still predates these development changes.


## 20. DirectWrite COM declaration metadata and actual MinGW COM calls

The next 2026-10-12 continuation implements the interop attributes used by
WPF's DirectWrite interface mirrors. ComImport produces the CLR type import
flag; Guid and InterfaceType produce actual custom-attribute blobs;
PreserveSig produces the method implementation flag; In/Out and supported
MarshalAs parameter declarations produce Param/FieldMarshal metadata.
Attribute arguments are parsed by Clang and enum constants are evaluated by
Sema. The emitter resolves attribute constructor references from the selected
metadata rather than guessing the owning assembly: GuidAttribute and
InterfaceTypeAttribute belong to System.Runtime.InteropServices in this SDK.

Grouped lists, Attribute suffixes, qualified names and the Runtime::InteropServices
form used by LocalizedStrings are accepted. Duplicate attributes, unsupported
targets, malformed GUIDs, unsupported interface modes and marshal kinds are
rejected. Ordinary array indexing with an element named Out remains ordinary
indexing. The supported forms are intentionally limited: GUIDs must be
canonical ordinary literals, ComImport currently requires an interface and
GUID, and marshal attributes accept one positional argument. General custom
attributes, coclass activation, return/field marshal descriptors, named marshal
options and broader namespace aliases remain pending.

The emitter now treats IntPtr*/UIntPtr* as unmanaged native-integer pointers.
Previously they were mistaken for typed value boxes; IntPtr^/UIntPtr^ are now
explicitly rejected until their boxed representation is supported. Native
record pointers keep the existing opaque native-address representation, with
further pointer indirection retained; this does not implement native layout or
native object operations. Native SDK record methods and inline functions are
excluded from managed IL collection. Calls to unavailable native functions
fail with an explicit bridge requirement, rather than producing missing method
references. Automatic native partitioning remains unfinished.

`cppcli.sh test-com` builds a genuine MinGW native COM object with IUnknown and
two interface vtables. The generated CLI interfaces call it from C# on the
source-built CoreCLR. The checks verify its GUID/interface/import metadata,
64-bit unsigned input, a noncanonical nonzero four-byte BOOL output, IntPtr*
address output, unchanged S_FALSE/failing HRESULTs with PreserveSig, normal
retval translation and the CLR's UnauthorizedAccessException mapping for
E_ACCESSDENIED without PreserveSig. All native references are released and the
native live-object count returns to zero. The generated Out parameter is also
consumed with C# out syntax. This exercises native calls in addition to metadata
reflection; no prebuilt COM test assembly or native DLL is used.

`wpf.sh check-com-interfaces` compiles the complete unchanged DWriteInterfaces.h
and IClassification.h against MinGW's actual dwrite.h/Windows headers. Its
CoreCLR check verifies all four GUIDs, eight preserved abstract COM methods,
U8/U4/BOOL marshal descriptors, In/Out flags, void**/UInt64*/IntPtr* and opaque
native interface pointer signatures, plus the private managed classification
callback's six boolean byrefs. `check-font-source` now includes windows.h
rather than defining its macro in a minimal context; its unchanged upstream
interfaces also pass. These are declaration/metadata checks, not completion of
DirectWrite's native callbacks or the full assembly.

The full bridge driver now selects the source-built interop attribute types,
enums and Debug type. Legacy __out/__deref_out_bcount SAL annotations are mapped
to MinGW's _Out_/_Outptr_result_bytebuffer_ definitions. This preserves the
source analysis annotations; CLI Out metadata comes from the separate managed
attributes.

Validation on win11:

```sh
MSYSTEM=CLANG64 bash -l -c "/clang64/bin/cmake.exe --build /home/topkek/dotnetboot/m612/build/cppcli --target cli_ast -j2"
MSYSTEM=CLANG64 M612=/home/topkek/dotnetboot/m612 \
  bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh \
  test-com test-interop test-core test-layout test-indexers test-nativeints
MSYSTEM=CLANG64 M612=/home/topkek/dotnetboot/m612 \
  bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh test-com test-generics test-each
M612=/home/topkek/dotnetboot/m612 MSYSTEM=CLANG64 \
  WPF_BUILD=/home/topkek/wpfboot/build \
  WPF_CPPCLI_BUILD=/home/topkek/dotnetboot/m612/build/cppcli \
  bash ~/wpfboot/driver/bootstrap/m612/wpf.sh \
  check-com-interfaces check-font-source
```

All listed focused checks pass, and pinning/compacting-GC checks also passed
before the native-integer pointer adjustment. The final test-com and upstream
interface checks were rerun after the last emitter change. Managed builds have
zero warnings/errors; generic static-field facade warnings remain. Results
include `real MinGW COM metadata/marshalling/HRESULT checks OK`,
`upstream WPF COM/classification interface metadata OK` and
`upstream WPF IFontSource interfaces OK`. Logs and emitted interface IL are
retained under `/home/kreijstal/tmp/wpf8-support/validation/com-20261012`.

The complete `wpf.sh bridge-gate` still exits nonzero. It advances past COM
attribute declarations and now reports native `private struct` declarations
in CharAttribute.h/TextItemizer.h, missing List/IList imports and delegates in
TextAnalyzer.h, along with additional legacy SAL and internal-member lowering
work. Complete native partitioning/callbacks/finalizers, System.Printing,
PresentationCore/PresentationFramework, missing PresentationNative exports,
rendered WPF validation and WindowsDesktop packaging remain unfinished.
Current Greenshot is not packaged. The installed frontend remains the older
release; these checks use the development frontend explicitly.


## 21. WPF delegate declarations, invocation and callback checks

The next 2026-10-12 continuation adds source delegates. Their declarations are
lowered into annotated C++ facades; Clang parses the actual return/parameter
types and resolves typed static or instance method binding. The resulting CLR
type is sealed and derives from MulticastDelegate. Its standard constructor
(object, native int) and Invoke method use runtime-managed implementation flags.
Binding emits ldftn for static/global/nonvirtual methods or ldvirtftn for virtual
instance slots. Reference targets and exact supported signatures are checked.

The private Clang parser now recognizes calls on marked delegate handles and
asks Sema to resolve Invoke before parsing/checking the argument list. Native
function-pointer syntax keeps its existing path. Metadata imports mark delegate
types so a second CLI assembly can invoke an imported source delegate. C# can
construct, call, combine and remove the generated delegate types using CLR
multicast behavior. Source combine/remove expressions, nested/generic delegate
declarations, open/value-instance binding and general delegate attributes remain
pending; imported delegates can be invoked but lack source binding facades.

ClassInterface and type/method ComVisible attributes now emit actual custom
metadata, resolving their constructor scopes from the source-built reference
assemblies. The expanded COM test obtains an interface pointer for a generated
managed class and calls it from a native MinGW vtable. Thus it checks a COM
callback in addition to the prior native COM object calls. Automatic native
partitioning and the full DirectWrite callback implementation remain unfinished.

Native public/private declaration prefixes are lowered into retained native
visibility annotations, leaving the C++ record/template body intact. They do
not create managed objects or implement native record layout/operations.
Internal static field initialization now restores public C++ access after its
helper body while preserving assembly visibility in the CLR annotations.
Raw address-taking is supported for primitive/unmanaged-pointer locals with
ldloca/conv.u; managed object/field address exposure and unmanaged pointer
arithmetic remain outside this subset.

`cppcli.sh test-delegates` verifies static/global/bound/virtual method identity,
normal and explicit Invoke syntax, byref/String/void/raw-pointer signatures,
null/throw behavior, GC target retention, C# multicast/remove behavior and
reflection of runtime methods/private delegate visibility. It also invokes an
imported source delegate from a second CLI assembly. A native MinGW DLL invokes
a generated delegate's real CoreCLR function-pointer thunk after compacting GC,
with the delegate rooted until the native call returns. An undefined target
method is rejected.

`wpf.sh check-delegates` retains all four unchanged private declarations from
TextAnalyzer.h in their original namespace. Its probe invokes callbacks created
by C# using the actual generated private delegate types, checks the nine
CreateTextAnalysisSource arguments (UTF-16 text, booleans, integers and native
pointers), and verifies a void** output. The other three callbacks preserve raw
pointer results. It also includes the unchanged CharAttribute.h and verifies
its native private-struct enum constants. This checks delegate declarations and
invocation, not the complete TextAnalyzer implementation or native text pipeline.

The full bridge driver now imports List/IList, Delegate/MulticastDelegate and
COM export attribute dependencies, including System.Collections.dll. Remaining
legacy counted SAL forms are mapped to MinGW's corresponding annotations.

Validation on win11:

```sh
MSYSTEM=CLANG64 M612=/home/topkek/dotnetboot/m612 \
  bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh build
MSYSTEM=CLANG64 bash -l -c "/clang64/bin/cmake.exe --build /home/topkek/dotnetboot/m612/build/cppcli --target cli_ast -j2"
MSYSTEM=CLANG64 M612=/home/topkek/dotnetboot/m612 \
  bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh \
  test-delegates test-com test-each test-static-fields test-static-properties test-adapter-gate
MSYSTEM=CLANG64 M612=/home/topkek/dotnetboot/m612 \
  bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh test-delegates test-pinning test-interop
M612=/home/topkek/dotnetboot/m612 MSYSTEM=CLANG64 \
  WPF_BUILD=/home/topkek/wpfboot/build \
  WPF_CPPCLI_BUILD=/home/topkek/dotnetboot/m612/build/cppcli \
  bash ~/wpfboot/driver/bootstrap/m612/wpf.sh check-delegates
```

All focused checks pass. The final delegate/COM tests and upstream WPF probe
were rerun after the last emitter change. Managed builds have zero warnings and
errors. Results include `CLR delegate construction/invocation/GC/native callback
OK`, `real MinGW COM metadata/marshalling/HRESULT checks OK` and
`upstream WPF text-analysis delegates OK`. Logs, the unchanged declaration probe
and emitted IL are retained under
`/home/kreijstal/tmp/wpf8-support/validation/delegates-20261012`.

The complete bridge gate still exits nonzero. Its next diagnostics occur in
Factory.h/Factory.cpp: missing DispatcherOperation and FILETIME imports,
ReliabilityContract attribute handling, managed ::typeid tokens and Marshal
GetComInterfaceForObject discovery, followed by native lifetime/pointer work.
Native/managed partitioning, finalizers, System.Printing, higher WPF assemblies,
missing PresentationNative exports, rendered-window validation, WindowsDesktop
packaging and current Greenshot remain unfinished. Development checks still
use WPF_CPPCLI_BUILD; the installed frontend package predates these features.


## 22. Managed type tokens and the Factory reliability accessor

The following 2026-10-12 continuation implements managed T::typeid. Its CLI
qualified type spelling is converted into a type-template operand; Clang
resolves the actual type and rejects expression operands. The emitter generates
ldtoken followed by Type.GetTypeFromHandle, retaining constructed generic,
value/reference/enum and array/pointer identities. It does not synthesize
native C++ record/enum RTTI metadata. Undefined source records are rejected;
ordinary native typeid(expr) is not rewritten by this CLI-only path.

`cppcli.sh test-type-tokens` compares the resulting Type objects with C# typeof
for source classes/value structs/enums, String, primitive integer widths,
aliased arrays/pointers and nested List constructions. It rejects expression
operands, incomplete source types and native class operands. The expanded COM
check calls Marshal.GetComInterfaceForObject from generated CLI code using
IManagedCallback::typeid, then invokes the resulting interface pointer from the
native MinGW DLL. This verifies WPF's type-token COM pattern at runtime.

ReliabilityContract declarations now retain both enum arguments in typed
annotations, with Clang checking/evaluating the constants. The emitted custom
attribute resolves the actual source-built constructor and enum assembly
identities. Method/accessor targets and enum ranges are checked; invalid type
targets are rejected rather than ignored. The check inspects both static-method
and CriticalHandle getter contracts on CoreCLR. This preserves metadata and
does not provide a new runtime constrained-execution-region guarantee.

`tools/wpf-factory-token-check.py` retains Factory.h's exact IsInvalid property
declaration, including its ReliabilityContract and pragma lines, and the
unchanged Factory.cpp getter body. A probe supplies the CriticalHandle base and
native factory pointer field, and renames only the enclosing test class. The
runtime check validates the real getter behavior, its inherited virtual slot
and both reliability values. This compiles the original getter component, not
all of Factory.cpp or DirectWriteForwarder.

The full bridge driver now supplies the source-built Type/RuntimeTypeHandle,
FILETIME, Thread, Dispatcher/DispatcherOperation and Action definitions, the
Thread assembly and Dictionary generic definition. This exposes the correct
Marshal COM overload and advances the complete source gate past the previous
missing type namespaces and reliability/typeid declarations.

Validation on win11:

```sh
MSYSTEM=CLANG64 M612=/home/topkek/dotnetboot/m612 \
  bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh build
MSYSTEM=CLANG64 bash -l -c "/clang64/bin/cmake.exe --build /home/topkek/dotnetboot/m612/build/cppcli --target cli_ast -j2"
MSYSTEM=CLANG64 M612=/home/topkek/dotnetboot/m612 \
  bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh \
  test-type-tokens test-com test-properties test-static-properties test-generics
M612=/home/topkek/dotnetboot/m612 MSYSTEM=CLANG64 \
  WPF_BUILD=/home/topkek/wpfboot/build \
  WPF_CPPCLI_BUILD=/home/topkek/dotnetboot/m612/build/cppcli \
  bash ~/wpfboot/driver/bootstrap/m612/wpf.sh check-factory-tokens
```

The focused rebuild and checks pass. Managed builds have zero warnings/errors;
the C# check suppresses the framework's SYSLIB0004 reliability deprecation
warning, and the existing C++ generic static-field facade warnings remain.
Results include `CLR managed type tokens and reliability metadata OK`,
`real MinGW COM metadata/marshalling/HRESULT checks OK` and
`upstream WPF Factory IsInvalid/reliability accessor OK`. Logs, the original
getter probe and emitted IL are retained under
`/home/kreijstal/tmp/wpf8-support/validation/type-tokens-20261012`.

The full `wpf.sh bridge-gate` still fails. It now reports the private Factory
constructor through the allocation facade, Object conversions for imported
bases/interface-only source classes, contextual String constructor arguments
and additional native pointer/lifetime work. Fixing those is still necessary
for the full bridge. Source-defined managed class templates, native partitioning,
System.Printing, higher WPF assemblies, missing PresentationNative exports,
rendered-window validation, WindowsDesktop packaging and current Greenshot
remain unfinished. The installed frontend still predates this development
build, selected explicitly with WPF_CPPCLI_BUILD.


## 23. Factory allocation access, Object conversions and imported members

The 2026-10-12 continuation replaces the allocation facade's
`decltype(T(args...))` constraint with `__cli_alloc(new T(args...))`. Clang now
resolves the actual constructor and checks its access in the original source
member or friend context, so Factory's own static method can call its private
constructor. The emitter reads the resulting CXXConstructExpr and emits CLR
`newobj`, `newarr` or boxing; it does not emit native operator new. Plain native
`new` remains explicitly rejected. Constructor argument conversions and primitive
default arguments are retained from Clang's resolved argument list. Adapter closing
markers also work in nested allocations, property accessors and static initializers.

Contextual String literals are now recognized at known String-parameter positions
of directly named source and imported constructors. This fixes the original
`gcnew ArgumentOutOfRangeException("faceIndex")` call and avoids the bool overload
winning for a source constructor with String and bool alternatives. This remains
selected token adaptation: constructor aliases, general literal conversions and
all imported optional/parameter-array rules are not implemented.

For source classes whose declared bases are all directly recognized interfaces,
the adapter adds their real implicit System.Object class base to the C++ facade.
The emitted CLR type still extends Object and implements the declared interfaces.
The WPF gate also selects CriticalFinalizerObject explicitly, completing the
actual CriticalHandle-to-Object imported facade chain. No synthetic CLR base is
substituted for the real imported base. Unselected base chains, alias lookup and
interface-handle conversions remain limitations.

The metadata reader now preserves nonindexed static properties with supported
public accessor signatures and emits the existing static-property proxy with real getter
and setter identities. This resolves WPF's Thread.CurrentThread and
CultureInfo.InvariantCulture calls. Imported delegates with a supported Invoke
signature receive typed static/member binding constructors in the C++ facade;
the emitter uses the real CLR Object/native-int constructor and ldftn/ldvirtftn.
The original Factory `gcnew Action(CleanupTimeStampCache)` now passes Clang Sema.
The WPF gate also defines UNICODE and _UNICODE, matching the upstream Windows
build and selecting CreateFileW for its pinned wide-string buffer.

Focused validation on win11:

```sh
MSYSTEM=CLANG64 M612=/home/topkek/dotnetboot/m612 \
  bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh build
MSYSTEM=CLANG64 M612=/home/topkek/dotnetboot/m612 \
  bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh \
  test-constructors test-objects test-delegates test-arrays test-boxed-values test-static-fields
MSYSTEM=CLANG64 M612=/home/topkek/dotnetboot/m612 \
  bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh \
  test-static-properties test-constructors test-generics test-com
MSYSTEM=CLANG64 M612=/home/topkek/dotnetboot/m612 \
  bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh \
  test-delegates test-static-properties test-constructors
M612=/home/topkek/dotnetboot/m612 MSYSTEM=CLANG64 \
  WPF_BUILD=/home/topkek/wpfboot/build \
  WPF_CPPCLI_BUILD=/home/topkek/dotnetboot/m612/build/cppcli \
  bash ~/wpfboot/driver/bootstrap/m612/wpf.sh bridge-gate
```

The builds and focused checks pass. The new C# consumer verifies actual private
and protected constructor visibility, factory execution, Unicode and static/accessor
String literals, the exception ParamName, Object identity, inherited interface
dispatch, and the real CriticalHandle base. A second CLI assembly reads and writes
imported static properties and rejects read-only and protected accessors. Protected
accessors remain callable by their original method names with Clang access checks;
the public property facade currently exposes only public accessors. Delegate checks bind
imported static, bound-instance and virtual methods, invoke System.Action, verify
its actual framework type after GC, and reject incompatible signatures. Existing
native COM/delegate checks still pass. Generic C++ static-field facade warnings
remain; managed consumer builds report zero warnings and errors.

The complete bridge gate still fails: its first error is now Factory.cpp:363,
where Dispatcher.BeginInvoke needs its omitted `params Object[]` argument.
Following errors include Double.MinValue/TryParse and Int32/UInt32 static members,
Guid literal construction, a boxed KeyValuePair-to-Object return, out-of-line
generic wrapper property definitions, additional imports and native operations.
These checks do not compile all of Factory.cpp or the complete bridge. The count
remains one complete implementation body (LocalizedErrorMsgs.cpp) out of the 21
included by DirectWriteForwarder.cpp; no additional full file is claimed.
System.Printing, higher WPF assemblies, complete PresentationNative exports,
version/protocol matching, WindowsDesktop packaging and current Greenshot remain
unfinished. All new compiler features are in the development frontend; installed
frontend packages are unchanged. Logs are archived under
`tmp/wpf8-support/validation/factory-allocation-20261012` on the local build host.


## 24. Imported empty parameter arrays and Factory dispatcher execution

The next 2026-10-12 continuation reads ParamArrayAttribute from the imported
method's final parameter and verifies that it is a rank-one array. The metadata
facade retains the ordinary full-array declaration and adds a template declaration
with a typed default expression for the omitted array. Clang selects the ordinary
method when conversions are equal, while a more specific fixed parameter in a
params form can still win. This avoids the Console.WriteLine(String) ambiguity
that a default argument on the ordinary params declaration would introduce.
Both declarations refer to the same original CLR method or constructor identity;
no generic method is emitted for the template facade.

The default expression emits length zero followed by newarr for the actual
element type, so each omitted array is fresh and nonnull. Existing supplied arrays
and explicit null arguments are passed through. Required ordinary array parameters
remain required. Expansion of individual arguments, source params declarations
and general imported optional arguments remain unsupported and are not claimed.

`cppcli.sh test-paramarrays` builds a C# fixture with the source-built SDK and
calls it through the frontend. It checks primitive/reference/String arrays,
static/instance methods, constructor omission and explicit arrays, inherited
virtual dispatch, null distinction, distinct empty arrays across calls, and both
ordinary-overload preference and the more-specific params overload. Negative
checks reject omission of an ordinary array and unsupported individual argument
expansion. `test-core` verifies the Console overloads and existing preprocessing,
control flow and primitive operations after this metadata facade change.

`tools/wpf-dispatcher-check.py` copies Factory.cpp's original dispatcher lookup
and its exact `_timeStampCacheCleanupOp = currentDispatcher->BeginInvoke(gcnew
Action(CleanupTimeStampCache));` statement into a small probe. Its replacement
callback records a count and thread ID; it does not claim to implement the font
cache. `wpf.sh check-factory-dispatcher` compiles that expression against the
source-built WindowsBase, then runs a C# consumer with the source-built CoreCLR
and the existing PresentationNative Win32 subset. The consumer verifies that the
operation initially remains pending, forces a GC cycle, pumps a real dispatcher
frame, and checks completion once on the dispatcher thread. A repeated scheduling
operation also completes. The offline project includes the source-built WindowsBase
OOB dependency closure, including ConfigurationManager needed by its compatibility
preferences; no Microsoft binary package is used.

Validation on win11:

```sh
MSYSTEM=CLANG64 M612=/home/topkek/dotnetboot/m612 \
  bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh build
MSYSTEM=CLANG64 M612=/home/topkek/dotnetboot/m612 \
  bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh test-paramarrays test-core
M612=/home/topkek/dotnetboot/m612 MSYSTEM=CLANG64 \
  WPF_BUILD=/home/topkek/wpfboot/build \
  WPF_CPPCLI_BUILD=/home/topkek/dotnetboot/m612/build/cppcli \
  bash ~/wpfboot/driver/bootstrap/m612/wpf.sh check-factory-dispatcher bridge-gate
```

The rebuild and focused checks pass, with zero managed build warnings/errors;
the WPF consumer prints `upstream WPF Factory dispatcher scheduling OK`.
The complete bridge gate still fails and deliberately returns nonzero: its first
error is now Font.cpp:23, `System::Double::MinValue`, followed by primitive static
methods, NumberStyles/imports, Guid literal construction, KeyValuePair boxing,
generic out-of-line properties and native operations. Factory.cpp has no Clang
errors in this gate, but no complete Factory implementation assembly is emitted.
The complete-file count therefore remains one of the 21 included implementation
bodies, and the full DirectWriteForwarder/System.Printing bridges, higher WPF,
complete native exports, version/protocol checks, WindowsDesktop packaging and
current Greenshot remain unfinished. Logs and the generated probe/IL are archived
under `tmp/wpf8-support/validation/paramarrays-20261012` on the local build host.


## 25. Primitive static scopes, literal metadata and WPF font version parsing

The following 2026-10-12 continuation imports selected System primitive types
without replacing their C++ scalar aliases. Their generated aliases carry a
facade annotation. The private ParseExprCXX parser hook first performs ordinary
Clang lookup, including namespace qualification and using declarations. Only a
resolved annotated alias selects its facade record for static scope lookup.
Native classes, enums and variables named Int32 continue through normal Clang
lookup instead of a spelling-based rewrite. The facade emits static methods and
constants using the original CLR type identity; declarations remain native
scalar types. Namespace aliases also work. Arbitrary user typedef chains and
raw keyword scopes such as double:: remain outside this subset.

The scope replacement uses a fresh well-formed location buffer via MakeTrivial.
Clang 22's [scope builder implementation](https://raw.githubusercontent.com/llvm/llvm-project/llvmorg-22.1.8/clang/lib/AST/NestedNameSpecifier.cpp)
requires an empty builder for Make; appending a type buffer to the existing
namespace buffer caused an initial parser crash and was corrected. All four
modified parser files still start from the checksum-pinned upstream sources.

The metadata reader now retains selected numeric/Char literal fields. It stores
integer values and floating-point bit patterns, emits constexpr declarations for
Clang's checks, and emits CLR ldc instructions for reads rather than ldsfld.
Single/Double declarations use builtin bit casts and IL float32/float64 hexadecimal
operands, preserving NaN sign/payload, infinities, subnormals and signed limits.
These literals have no CLR field storage: writes are rejected by Clang and address
requests by the emitter. String/null literal fields remain excluded. Imported
primitive instance methods and boxing are not added by this change.

`cppcli.sh test-primitive-statics` checks exact Double/Single bits, signed and
unsigned 8/16/32/64-bit limits, Char.MaxValue, namespace aliases, native enum/class
shadowing, Double.TryParse/IsFinite and Int32.TryParse. The out parameter is a real
tracking reference, including the zero result after a failed parse. Negative
checks reject writes, addresses, missing members and a local value shadowing a
primitive alias. The fixture exposed an existing unrelated limitation: ordinary
global functions in different C++ namespaces share IL names, so equal-name/signature
collisions still fail; the native-shadow probes use distinct global function names.

`tools/wpf-font-version-check.py` copies Font.cpp's exact MinValue initialization
and its complete version-string length/Substring/TryParse/fallback block. A probe
supplies the input string and returns the parsed value; it does not implement
font lookup, cached flags or the rest of Font.cpp. The C# consumer sets CurrentCulture
to de-DE and verifies decimal and exponential invariant parsing, invalid/comma
fallbacks, the short-string case and the initial double.MinValue sentinel.

Validation on win11:

```sh
MSYSTEM=CLANG64 M612=/home/topkek/dotnetboot/m612 \
  bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh build
MSYSTEM=CLANG64 M612=/home/topkek/dotnetboot/m612 \
  bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh \
  test-primitive-statics test-core test-generics test-type-tokens
MSYSTEM=CLANG64 M612=/home/topkek/dotnetboot/m612 \
  bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh test-primitive-statics
M612=/home/topkek/dotnetboot/m612 MSYSTEM=CLANG64 \
  WPF_BUILD=/home/topkek/wpfboot/build \
  WPF_CPPCLI_BUILD=/home/topkek/dotnetboot/m612/build/cppcli \
  bash ~/wpfboot/driver/bootstrap/m612/wpf.sh check-font-version
M612=/home/topkek/dotnetboot/m612 MSYSTEM=CLANG64 \
  WPF_BUILD=/home/topkek/wpfboot/build \
  WPF_CPPCLI_BUILD=/home/topkek/dotnetboot/m612/build/cppcli \
  bash ~/wpfboot/driver/bootstrap/m612/wpf.sh bridge-gate
```

The rebuild and focused checks pass; managed consumer builds report zero warnings
and errors, and the WPF check prints `upstream WPF font version parsing OK`.
The generic C++ static-field facade warnings remain. The final complete bridge
gate still returns nonzero. Primitive static/limit/NumberStyles errors are gone;
its first error is FontFile.cpp:16, functional-style Guid construction from a String
literal. Later errors include an OverflowException import, KeyValuePair-to-Object
boxing, out-of-line generic wrapper properties, additional imports and native
operations. No complete new bridge implementation file or WPF package is claimed:
one of the 21 included implementation bodies remains fully verified. System.Printing,
higher WPF, complete native exports, version/protocol matching, desktop framework/SDK
packaging and current Greenshot remain unfinished. The new features are development
frontend changes; installed compiler packages remain unchanged. Logs, IL and generated
probe sources are archived in `tmp/wpf8-support/validation/primitive-statics-20261012`
on the local build host.

## 26. Functional Guid construction and managed value boxing into Object

The next 2026-10-12 continuation handles parenthesized functional managed value
construction after Clang resolves the actual target type. For a value record with
an available String-pointer constructor parameter at that argument position,
the private parser converts a literal through the existing String marker before
normal constructor overload resolution. This fixes FontFile.cpp's original
`System::Guid("b2d9f3ec-c9fe-4a11-a2ec-d86208f7c0a2")` expression. Narrow and wide
literals and namespace aliases work. Native classes or functions named Guid do
not undergo this conversion, and constructor access checks remain Clang checks.
List/direct initialization and general native character buffers remain unsupported.

Source and imported value declarations now carry a separate C++ facade conversion
to System.Object. The backend emits CLR box for the concrete value type, including
constructed generic TypeSpecs; it emits no native conversion method. A value-type
instance receiver is loaded from its managed address before boxing. A boxed value
receiver already has object identity and can reuse its existing box. This does not
add general primitive/interface boxing or implicit upcasts from typed boxed handles.
The original LocalizedStrings.Current2 getter's `return Current;` now passes Sema
with its KeyValuePair<CultureInfo,String> value property.

`cppcli.sh test-guid-object-boxing` verifies Guid values and invalid-input
FormatException behavior, a source value constructor with String and bool overloads,
Unicode, native constructor/function literal lookup in unevaluated expressions,
and rejection of a private value constructor. Its C# consumer verifies that two
box operations produce distinct objects, mutation of the source value does not
change an existing box, and instance methods box the current value from a managed
address. A generic Current/Current2 property fixture verifies the actual CLR
KeyValuePair type, one getter evaluation, reference-field values and survival after
GC. The property fixture supplies data; it does not claim to compile the complete
upstream nested enumerator or all of LocalizedStrings.cpp.

The WPF driver also imports the actual OverflowException type used by
LocalizedStrings. NativePointerWrapper out-of-line generic properties, class
methods/templates and native lifetime operations remain separate unfinished work.

Validation on win11:

```sh
MSYSTEM=CLANG64 M612=/home/topkek/dotnetboot/m612 \
  bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh build
MSYSTEM=CLANG64 M612=/home/topkek/dotnetboot/m612 \
  bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh \
  test-guid-object-boxing test-boxed-values test-values test-generics
MSYSTEM=CLANG64 M612=/home/topkek/dotnetboot/m612 \
  bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh \
  test-guid-object-boxing test-constructors test-boxed-values
M612=/home/topkek/dotnetboot/m612 MSYSTEM=CLANG64 \
  WPF_BUILD=/home/topkek/wpfboot/build \
  WPF_CPPCLI_BUILD=/home/topkek/dotnetboot/m612/build/cppcli \
  bash ~/wpfboot/driver/bootstrap/m612/wpf.sh bridge-gate
```

The rebuild and focused checks pass; C# consumer builds have zero warnings/errors,
and the generic C++ static-field facade warnings remain. The final full bridge
still fails and retains its diagnostics. Its first errors are now
NativePointerWrapper.cpp:16/22, parsing IsInvalid::get and Value::get after a
constructed generic owner. Later errors include dependent CriticalHandle fields,
additional stream/math imports, indexed calls and native operations. Guid literal,
OverflowException and Current-to-Object errors are absent. This is additional
compiler support, not a completed native bridge implementation or package: the
count remains one complete body of the 21 included by DirectWriteForwarder.cpp.
System.Printing, higher WPF assemblies, full native export coverage, protocol/version
matching, WindowsDesktop packaging and current Greenshot remain unfinished.
Changes are development frontend changes; installed compiler packages are unchanged.
Logs and generated IL are archived in
`tmp/wpf8-support/validation/guid-object-boxing-20261012` on the local build host.


## 27. Closed C++ managed templates and WPF pointer-handle properties

The 2026-10-12 continuation fixes accessor translation after a constructed
owner such as NativePointerCriticalHandle<T>::IsInvalid::get. Balanced template
angles select the class declaration's property name, and Clang still parses the
complete owner and checks the accessor definition. The allocation adapter also
preserves token separation in template argument types such as long long.

The backend collects completed source-managed class template specializations,
skips dependent primary declarations, and assigns each closed type a distinct
canonical Clang identity in the CLR type name. Referenced methods and property
accessors keep their concrete signatures and ordinary virtual/class behavior.
These are compile-time C++ templates emitted as ordinary CLR types, as described
by [Microsoft's managed-template documentation](https://learn.microsoft.com/en-us/cpp/extensions/windows-runtime-and-managed-templates-cpp-component-extensions?view=msvc-170).
They do not add open CLR generic declarations or permit runtime specialization
from another assembly. General partial/nested specialization forms, template
static fields and unused property accessor metadata remain incomplete.

`0003-mingw-dependent-members.patch` qualifies inherited handle references with
this-> in NativeIUnknownWrapper<T>/NativePointerWrapper<T>. This preserves the
source operations while making dependent-base lookup explicit for standard Clang.
It does not implement their native Release/delete operations or finalizers.
The metadata facade also provides equality/inequality friends for supported
imported value operator slots, preventing IntPtr comparison from considering
boxing conversions. The emitted call targets the original CLR op_Equality or
op_Inequality rather than pointer/object identity.

`cppcli.sh test-managed-templates` creates int and long long specializations,
checks distinct nongeneric CLR identities through C# reflection, invokes real
out-of-line getters/setters, verifies inherited virtual dispatch and confirms
an unreferenced method is absent. The equality fixture compares signed/unsigned
pointer-sized values above Int32 and Guid values through real CLR slots.

`tools/wpf-pointer-handle-check.py` copies the original NativePointerCriticalHandle
class template and all three constructor/accessor definitions into a probe. A
separate concrete subclass supplies a counted managed release operation; native
IUnknown release and native deletion are not substituted or claimed complete.
The C# consumer uses real unmanaged memory, verifies the pointer round-trip,
int-pointer property metadata, the IsInvalid base slot, nongeneric specialized
base identity and exactly-once CriticalHandle disposal, including the invalid
zero-pointer case.

Validation on win11:

```sh
MSYSTEM=CLANG64 M612=/home/topkek/dotnetboot/m612 \
  bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh build
MSYSTEM=CLANG64 M612=/home/topkek/dotnetboot/m612 \
  bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh \
  test-managed-templates test-properties test-static-properties test-generics test-nativeints
MSYSTEM=CLANG64 M612=/home/topkek/dotnetboot/m612 \
  bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh test-managed-templates
M612=/home/topkek/dotnetboot/m612 MSYSTEM=CLANG64 \
  WPF_BUILD=/home/topkek/wpfboot/build \
  WPF_CPPCLI_BUILD=/home/topkek/dotnetboot/m612/build/cppcli \
  bash ~/wpfboot/driver/bootstrap/m612/wpf.sh check-pointer-handle
M612=/home/topkek/dotnetboot/m612 MSYSTEM=CLANG64 \
  WPF_BUILD=/home/topkek/wpfboot/build \
  WPF_CPPCLI_BUILD=/home/topkek/dotnetboot/m612/build/cppcli \
  bash ~/wpfboot/driver/bootstrap/m612/wpf.sh bridge-gate
```

The focused checks pass with zero managed build warnings/errors, retaining the
known generic C++ static-field facade warnings. The WPF check prints
`upstream WPF pointer handle constructor/accessors and CLR cleanup OK`.
The driver now selects Stream, Math and GCHandle metadata required by later
bridge bodies. The final full bridge gate still returns nonzero, first at
FontFileStream.cpp:50, long::MaxValue (keyword primitive static scope); following
errors include nested indexed calls and pin-pointer lifetime/conversion forms.
NativePointerWrapper property/dependent-name and missing Stream.Close/Math/GCHandle
errors are absent. No additional complete bridge source body or WPF package is
claimed: one of the 21 included implementation bodies remains fully verified.
Full native partitioning/lifetimes, System.Printing, higher WPF, native exports,
version/protocol matching, WindowsDesktop packaging and current Greenshot remain
unfinished. New features remain development frontend changes. Logs, generated
source and IL are archived in `tmp/wpf8-support/validation/closed-templates-20261012`
on the local build host.

## 28. Primitive keyword scopes, nested indexers, assigned pins and interior returns

The next 2026-10-12 continuation removes the remaining Clang parsing/Sema errors
from the complete upstream DirectWriteForwarder amalgamation. Fundamental
keyword scopes such as long::MaxValue, unsigned long::MaxValue and
double::MinValue select the corresponding explicitly imported CLR primitive
facade. Native target widths determine the mapping: Windows long remains
Int32, while long long is Int64. Common signed/unsigned-prefix spellings are
supported; arbitrary keyword order, long double and numeric typedef chains
are not added. Ordinary native declarations and name shadowing remain intact.

The private parser completes a preceding managed property subscript before
processing a following bracket. List<array<unsigned int>^>[i][j] therefore
indexes the first getter's returned array. A bare named property still waits
for its first index. The nested-indexer test updates through side-effectful
receiver/outer/inner expressions and verifies each runs once; existing named
and default indexer checks cover the original regression.

Primitive pin_ptr locals can now be default constructed and directly assigned
tracked string/array addresses or nullptr. They keep the same CLR pinned slots
and block finally cleanup. Explicit reinterpret_cast with a spelled native
pointer target invokes the pin conversion intrinsic, retaining element const
checks and the original pin. General pointer aliases/operations and arbitrary
typedef cast targets remain incomplete. The TextAnalyzer source patch
0004-mingw-loop-variable-scope.patch moves the loop counter declaration before
the loop so its existing post-loop use is valid standard C++.

Primitive interior_ptr returns now emit genuine CLR byref signatures when the
returned expression is directly rooted in string characters, a managed array
element or a reference-class instance field. Returning through a pin cleanup
region stores the returned byref in a tracked temporary before leave. Stack
locals, copied value parameters, stack-value fields, pointer aliases, pin
returns and a conditional containing an unsafe branch are rejected. General
provenance analysis of forwarding calls and local aliases remains unfinished.
Native references are not silently converted to tracking references; their
different GC behavior is described in Microsoft's
[handle/reference examples](https://learn.microsoft.com/en-us/cpp/extensions/handle-to-object-operator-hat-cpp-component-extensions?view=msvc-170).

`wpf.sh check-string-chars` stages the original GetPtrToStringChars declaration
and body from Common.h in a public probe type. The C# consumer checks the CLR
char& return signature and every UTF-16 character, including an empty string,
after compacting collections. The pinning checks also call a source helper,
pin its returned string byref for a real native call during concurrent
compacting GC, and mutate returned array/instance-field byrefs after collection.
This compiles the original helper, not the complete Common.h or another full
DirectWrite implementation file.

Validation on win11:

```sh
MSYSTEM=CLANG64 M612=/home/topkek/dotnetboot/m612 \
  bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh build
MSYSTEM=CLANG64 M612=/home/topkek/dotnetboot/m612 \
  bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh \
  test-pinning test-primitive-statics test-nested-indexers test-indexers
MSYSTEM=CLANG64 bash -l -c "/clang64/bin/cmake.exe --build /home/topkek/dotnetboot/m612/build/cppcli --target cli_ast -j2"
MSYSTEM=CLANG64 M612=/home/topkek/dotnetboot/m612 \
  bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh test-pinning test-tracking
M612=/home/topkek/dotnetboot/m612 MSYSTEM=CLANG64 \
  WPF_BUILD=/home/topkek/wpfboot/build \
  WPF_CPPCLI_BUILD=/home/topkek/dotnetboot/m612/build/cppcli \
  bash ~/wpfboot/driver/bootstrap/m612/wpf.sh check-string-chars bridge-gate
```

Both rebuilds and all focused checks pass. Managed builds report zero warnings
and errors; the WPF helper prints `WPF string interior-pointer return and moving
GC OK`. The full bridge gate still exits 1 after 39 upstream/native C++ warnings.
Its first IL-collection error is now Common.h:80, the System::Guid& parameter of
Native::Util::ToGUID. That helper also returns a native _GUID by value and reads
it from a pinned byte buffer; it requires actual native layout/reference
handling rather than a diagnostic bypass. The earlier keyword/indexer/default
pin/cast errors are gone. No DirectWriteForwarder assembly is produced.

Complete-file evidence remains one of the 21 included implementation bodies;
the five WPF foundation assemblies still cover 620 C# source inputs. Full
native partitioning/lifetimes, System.Printing, higher WPF, 101 missing native
exports, gfx/PenImc version/protocol matching and WindowsDesktop packaging
remain open. These are development frontend changes, not a new installed
compiler or WPF package. Logs, generated helper source and IL are archived in
`tmp/wpf8-support/validation/keyword-index-pins-returns-20261012` on the local
build host.

## 29. Native reference metadata and WPF's original GUID conversion

The next 2026-10-12 continuation compiles the original Common.h ToGUID helper
without changing its System::Guid& parameter or native _GUID return. Source
native reference parameters and locals, including reference type aliases, now
use typed unmanaged pointers with
IsImplicitlyDereferenced optional metadata. Tracking references and imported
CLR byref facades retain their managed byref representation. This follows the
native-reference encoding in [ECMA-372 §33.1.5.5](https://www.ecma-international.org/wp-content/uploads/ECMA-372_1st_edition_december_2005.pdf).

Binding a native reference requires stable storage: a stack local/value
parameter, a forwarded native reference, an unmanaged dereference or a pinned
primitive dereference. Movable managed fields/array elements and unproven
tracking-reference aliases are rejected. Referenced values must have proven
GC-free storage. The metadata reader checks every instance field, including
private fields omitted from the C++ facade; unresolved external/generic field
layouts are rejected conservatively. A private-long imported struct passes,
while an imported struct with a private object field is rejected. Native
reference returns, general lifetime analysis, stack-field/temporary bindings,
imported native-reference signatures and overloaded %/& distinctions remain
incomplete.

Trivial standard-layout native records can now use opaque sequential CLR value
storage. Clang supplies the size/alignment; the emitted storage uses .size,
.pack, an alignment-sized scalar field and NativeCppClassAttribute. Native
members are not exposed as managed fields. Primitive alignment up to eight
bytes is supported, with larger alignment rejected. This uses the native
value-storage representation described in ECMA-372 §34.8, with generated
canonical type identities rather than cross-compiler metadata compatibility.
Trivial default/copy construction, by-value returns and indirect copy/store
work; nontrivial constructors, native field access, allocation/deletion and
native vtables are still unsupported. System SDK declarations are excluded
from selection as user-DLL P/Invokes, so unrelated SDK inline references do not
create imports into the wrong split DLL. Actual native SDK calls still require
implemented native partitioning or explicit bridges.

`cppcli.sh test-native-storage` checks stack reference mutation through a call
that collects, forwarding/local aliases, native references to pinned array
storage, imported private-field classification, GUID value copies and indirect
assignment. A real MinGW DLL checks the native address alignment and all 16
Windows GUID bytes. C# reflection verifies the unmanaged Guid pointer plus
modifier, sequential native value identity, pack, size and NativeCppClass flag.
Negative checks cover movable heap/tracking bindings, hidden GC fields,
nontrivial constructors and unsupported alignment.

`wpf.sh check-native-guid` stages the original ToGUID declaration/body from
Common.h in a public probe type. It verifies the actual result against the
Windows GUID bytes after collection and rejects an incorrect GUID. This is
another real helper component, not an additional complete source file or the
full native FontFile initializer.

Validation on win11:

```sh
MSYSTEM=CLANG64 M612=/home/topkek/dotnetboot/m612 \
  bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh build
MSYSTEM=CLANG64 bash -l -c "/clang64/bin/cmake.exe --build /home/topkek/dotnetboot/m612/build/cppcli --target cli_ast -j2"
MSYSTEM=CLANG64 M612=/home/topkek/dotnetboot/m612 \
  bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh \
  test-native-storage test-tracking test-pinning test-values test-interop
MSYSTEM=CLANG64 M612=/home/topkek/dotnetboot/m612 \
  bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh test-native-storage test-com
MSYSTEM=CLANG64 M612=/home/topkek/dotnetboot/m612 \
  bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh test-native-storage test-tracking
M612=/home/topkek/dotnetboot/m612 MSYSTEM=CLANG64 \
  WPF_BUILD=/home/topkek/wpfboot/build \
  WPF_CPPCLI_BUILD=/home/topkek/dotnetboot/m612/build/cppcli \
  bash ~/wpfboot/driver/bootstrap/m612/wpf.sh check-native-guid bridge-gate
```

The metadata reader and frontend rebuilds pass. Focused checks report zero
managed build warnings/errors; the upstream helper prints `original WPF ToGUID
helper and Windows GUID bytes OK`. The final full bridge gate still exits 1,
with 39 upstream/native C++ warnings. Its first error now names the missing
System.IDisposable::Dispose slot in
MS::Internal::Text::TextInterface::LocalizedStrings::LocalizedStringsEnumerator.
That nested enumerator declares ~LocalizedStringsEnumerator() {}; the compiler
must generate the actual IDisposable/destructor behavior rather than omit the
interface validation. General managed destructors/finalizers and the remaining
native operations also remain open.

No complete DirectWriteForwarder assembly or WPF package is produced. Full-file
evidence remains one of its 21 included bodies, with 620 compiled C# foundation
inputs. System.Printing, higher WPF, missing native exports, native version and
protocol matching and WindowsDesktop packaging remain unfinished. New features
are development frontend changes. Logs, generated original-helper source and
IL are archived in `tmp/wpf8-support/validation/native-guid-storage-20261012`
on the local build host.

## 30. Source destructors, CLR nesting and WPF enumerator execution

The next 2026-10-12 continuation lowers source ref-class destructors through the
CLR dispose pattern. The original body is emitted as a private '~T' helper;
Dispose(bool) is protected and virtual, with a public sealed IDisposable entry
point when needed. Successful disposal calls GC.SuppressFinalize. Destructors
alone do not introduce a Finalize method. Source-derived cleanup runs the base
Dispose(bool) in a real finally region, including when the original body
throws. Imported virtual dispose patterns and parameterless IDisposable base
patterns are also called appropriately; unrelated methods named Dispose are
not adopted as cleanup. This implements the relevant initial forms of
[ECMA-372 §34.7.13](https://www.ecma-international.org/wp-content/uploads/ECMA-372_1st_edition_december_2005.pdf).

Managed delete and explicit destructor calls evaluate the handle once, use a
runtime IDisposable cast and call the interface slot when available. Null
handles are harmless; native delete/delete[] remain rejected. No once-only
state is added: repeated disposal follows the original destructor's own body
and guards. Explicit Dispose/Finalize declarations that conflict with generated
cleanup, defaulted destructors and value/interface destructors are rejected.
User !T finalizers, full stack-semantics RAII and nontrivial member/native
cleanup remain unfinished. The System.Object metadata facade uses a potentially
throwing native surrogate destructor, avoiding a false C++ noexcept restriction
on managed destructor exceptions; it never supplies a native implementation.

Source nested ref/value classes now emit genuine nested CLR declarations and
slash-qualified member/type identities. Access to an enclosing class's private
fields/properties therefore retains CLR access rights. Nested value storage
and GC fields are checked after a compacting collection. The value-constructor
adapter also distinguishes ~T() from a zero-argument constructor. Imported
nested types and general nested generic/delegate forms remain unsupported.

`cppcli.sh test-destructors` verifies C# IDisposable calls, managed delete and
explicit destructor dispatch, single receiver evaluation, null handles,
derived/base order, base cleanup after an exception, imported MemoryStream
closure, parameterless base cleanup and exclusion of unrelated Dispose(bool)
methods. It checks real nested private access, nested value GC storage,
Dispose(bool) visibility/virtual metadata, absence of an introduced finalizer,
Dispose(false) behavior and suppression of an inherited C# finalizer.

`tools/wpf-enumerator-check.py` retains the complete original
LocalizedStringsEnumerator declaration and both Current getter bodies. Its
owner supplies managed culture/string arrays and private accessors; the
original LocalizedErrorMsgs component supplies error properties. The probe
uses no replacement enumerator or added source Dispose method. The C# check
verifies generic/nongeneric Current, real value boxing, two locale/Unicode
pairs after compacting GC, not-started/end exceptions, Reset, repeated empty
disposal and nested declaring-type/IDisposable metadata. This validates the
original nested component, not all of LocalizedStrings.cpp or DirectWrite.

Validation on win11:

```sh
MSYSTEM=CLANG64 M612=/home/topkek/dotnetboot/m612 \
  bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh build
MSYSTEM=CLANG64 bash -l -c "/clang64/bin/cmake.exe --build /home/topkek/dotnetboot/m612/build/cppcli --target cli_ast -j2"
MSYSTEM=CLANG64 M612=/home/topkek/dotnetboot/m612 \
  bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh \
  test-destructors test-virtuals test-values test-managed-templates
MSYSTEM=CLANG64 M612=/home/topkek/dotnetboot/m612 \
  bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh test-destructors
M612=/home/topkek/dotnetboot/m612 MSYSTEM=CLANG64 \
  WPF_BUILD=/home/topkek/wpfboot/build \
  WPF_CPPCLI_BUILD=/home/topkek/dotnetboot/m612/build/cppcli \
  bash ~/wpfboot/driver/bootstrap/m612/wpf.sh check-enumerator bridge-gate
```

The source-built metadata reader/frontend rebuild and all focused checks pass.
Managed builds have zero warnings/errors, with the existing C++ interface
missing-override warning retained in the virtual fixture. The WPF check prints
`original WPF nested enumerator, getters, boxing and IDisposable cleanup OK`.
The final full bridge gate exits 1 after the same 39 upstream/native C++
warnings. Missing enumerator Dispose slots and the FontFaceCacheEntry nested
value rejection are gone. The next error is FontFace.cpp:137, the GlyphMetrics*
parameter of GetDesignGlyphMetrics. Native pointers to GC-free managed value
storage need distinct handling from boxed '^' value handles; removing the
pointer declaration check alone would not provide that distinction.

No complete bridge assembly or WPF package is produced. Full implementation
file evidence remains one of 21 DirectWrite bodies; 620 foundation C# inputs
remain compiled. Finalizers, native value pointers/allocation/partitioning,
System.Printing, higher WPF, missing native exports, version/protocol matching
and WindowsDesktop packaging remain open. New features are development
frontend changes. Logs, emitted IL and original-enumerator probe sources are
archived in `tmp/wpf8-support/validation/destructors-enumerator-20261012` on the
local build host.

## 31. Native managed-value pointers and DirectWrite glyph output

The next 2026-10-12 continuation distinguishes supported source V* native
pointers from V^ boxed handles when lowering declarations and expressions.
Clang's existing surrogate pointer shape still performs ordinary C++ lookup,
but source sigils/aliases and expression origins select different CLR storage.
Native parameters, locals, fields and returns emit valuetype V*; boxed handles
retain their ValueType/IsBoxed metadata and unboxing/alias semantics. Native
member reads/writes, instance calls, value copies, indirect assignment,
forwarding, conditional/null values and scalar split-DLL calls now work for
proven GC-free value elements.

Bindings/casts do not silently interchange native pointers and boxes. Guards
cover argument/return/local bindings, conditional mixing, managed casts of raw
pointers, native deletion and incompatible native/boxed redeclarations sharing
one canonical Clang declaration. Direct addresses require stable stack/native
storage; movable managed fields remain rejected. Source boxing intrinsics are
explicitly retained as boxing operations. Unsupported native pointer properties
and array/generic elements are rejected. Imported native value-pointer
signatures, pointer-versus-caret-only overloads, general expression/lifetime
forms, value pin_ptr/interior_ptr, pointer arithmetic and native allocation/
deletion remain incomplete. This is declaration/expression provenance in the
current frontend, not a completed canonical Clang C++/CLI type system.

Ordinary unmanaged integral/pointer conversions now preserve pointer width and
sign extension. A runtime check verifies the predefined Win32 handle bit pattern
0xffffffff80000002, which removes the HKEY_LOCAL_MACHINE cast failure from the
complete WPF gate. These casts do not turn managed boxes into raw pointers.

`cppcli.sh test-value-pointers` checks typed source signatures, fields and
returns, aliases, native field/method updates, indirect struct copies and
conditional/null pointer identity. It calls a real MinGW DLL from a stack value
and from a C# fixed managed struct array, with a GC collection before the native
call. The native DLL verifies the input fields and mutates a 64-bit field. C#
reflection checks unmanaged pointer metadata, while the box factory/reader and
existing boxed-value checks retain actual boxed object behavior. Negative
checks enforce the unsupported/mixed cases described above.

`wpf.sh check-glyph-pointer` includes the unchanged upstream GlyphMetrics.h.
The private explicit-layout type stays private and preserves all seven field
offsets and its 28-byte size. A C# GCHandle pin roots an array of three actual
GlyphMetrics values. The generated CLI code accepts its address, collects, and
passes a typed GlyphMetrics* P/Invoke to a native MinGW function compiled against
DWRITE_GLYPH_METRICS. That function checks alignment and writes every field;
reflection checks the values after another compacting collection. This
validates actual native glyph-buffer representation and writes, not a complete
FontFace/DirectWrite implementation or DirectWrite font operation.

Validation on win11:

```sh
MSYSTEM=CLANG64 bash -l -c "/clang64/bin/cmake.exe --build /home/topkek/dotnetboot/m612/build/cppcli --target cli_ast -j2"
MSYSTEM=CLANG64 M612=/home/topkek/dotnetboot/m612 \
  bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh \
  test-value-pointers test-boxed-values test-native-storage test-pinning
M612=/home/topkek/dotnetboot/m612 MSYSTEM=CLANG64 \
  WPF_BUILD=/home/topkek/wpfboot/build \
  WPF_CPPCLI_BUILD=/home/topkek/dotnetboot/m612/build/cppcli \
  bash ~/wpfboot/driver/bootstrap/m612/wpf.sh check-glyph-pointer bridge-gate
```

The frontend rebuild and focused checks pass; managed builds report zero
warnings/errors. The original-header check prints `original WPF GlyphMetrics
pointer, native DirectWrite ABI and pinned GC storage OK`. The complete bridge
still exits 1 after 39 upstream/native C++ warnings. It now passes the native
GlyphMetrics pointer declarations and Win32 predefined-handle conversions. The
first IL-emission error is util2.cpp:18: RegOpenKeyExW has no emitted native
bridge. System SDK calls need real DLL binding/partitioning; suppressing that
error would not implement the registry helper or native text/font operations.

No complete DirectWriteForwarder assembly or WPF package is produced. Complete
implementation-file evidence remains one of 21 DirectWrite bodies, and 620
foundation C# inputs remain compiled. Native SDK calls/partitioning, finalizers,
System.Printing, higher WPF, missing native exports, protocol/version matching
and WindowsDesktop packaging remain open. Changes are development frontend
changes. Logs, emitted IL and glyph pointer source checks are archived in
`tmp/wpf8-support/validation/value-glyph-pointers-20261012` on the local build host.

## 32. Windows API imports and the complete registry helper

The 2026-10-12 continuation implements Windows SDK call binding through explicit
trusted MinGW import archives. `cli-clang --native-import <archive>` uses
`llvm-readobj` to read named x64 COFF short code imports and writes the resolved
DLL/export map into the assembly work directory. Ambiguous bindings fail. The
emitter selects declarations only when managed bodies call a system-header
extern-C DLL-import function; unrelated SDK inline code does not produce imports.
These methods retain scalar/pointer ABI checks, exact export spelling, Winapi
calling convention and PreserveSig. User split-DLL `--native-library` bindings
remain separate. Ordinal/data/decorated/forwarder forms, by-value native record
API signatures and automatic last-error capture remain unsupported. Registry
APIs return their LSTATUS directly; no synthetic last-error handling is added.

Native POD aggregate initialization now uses Clang's semantic field mapping and
zeroed opaque value storage. Scalar/nested-record field reads, writes and addresses
use actual Clang byte offsets. Compound updates and increments capture the
receiver address once. Unaligned memory prefixes support packed records, and
volatile field access retains the volatile prefix. Bitfield and array field
operations, allocation/deletion, nontrivial constructors/destructors and native
object dispatch remain unimplemented. Address formation still requires supported
stable native storage rather than converting movable managed storage into an
untracked address.

The native ABI test exposed a preprocessing bug: the token adapter discarded
`#pragma pack` directives. A preprocessing pragma handler now carries pack
directives into the adapted source as `_Pragma` expressions, leaving pack-stack
interpretation and layout to the real Clang parser. The test checks included-header
`#pragma`, macro-expanded `__pragma`, `_Pragma`, and restored natural alignment
against a separately compiled native DLL: a 15-byte nested packed record, a
10-byte record packed to two-byte alignment, and a 16-byte natural record. It
also checks 64-bit field values, nested field addresses, copies, union scalar
initialization, volatile writes and single evaluation of side-effectful receivers.

`wpf.sh check-registry-time` copies the complete upstream `util2.cpp` and its
original `util2.h` byte for byte and compiles them with a focused precompiled-header
context containing Windows and vcclr dependencies. No method body is extracted
or rewritten. Its actual RegOpenKeyExW, RegQueryInfoKeyW and RegCloseKey calls
are imported from `libadvapi32.a`. A reflection-based C# consumer accesses the
original internal Util2 type, verifies Out/Int64-byref metadata, compares UTC
ticks with an independent Win32 FILETIME query, exercises compacting GC and
checks handle counts across 128 reads. A nonexistent Unicode key returns false
and zero output. All registry operations are read-only.

Validation on win11 with the source-built SDK/runtime and development frontend:

```sh
MSYSTEM=CLANG64 M612=/home/topkek/dotnetboot/m612 \
  bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh build
MSYSTEM=CLANG64 M612=/home/topkek/dotnetboot/m612 \
  bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh \
  test-native-fields test-windows-api test-native-storage test-value-pointers test-interop
M612=/home/topkek/dotnetboot/m612 MSYSTEM=CLANG64 \
  WPF_BUILD=/home/topkek/wpfboot/build \
  WPF_CPPCLI_BUILD=/home/topkek/dotnetboot/m612/build/cppcli \
  bash ~/wpfboot/driver/bootstrap/m612/wpf.sh check-registry-time bridge-gate
M612=/home/topkek/dotnetboot/m612 MSYSTEM=CLANG64 \
  WPF_BUILD=/home/topkek/wpfboot/build \
  WPF_CPPCLI_BUILD=/home/topkek/dotnetboot/m612/build/cppcli \
  bash ~/wpfboot/driver/bootstrap/m612/wpf.sh check-glyph-pointer check-native-guid
```

The build and focused checks pass. The Windows API consumer also verifies exactly
three imports, both real DLL identities, ExactSpelling/Winapi/PreserveSig and
unchanged native error status. Managed check builds have zero warnings/errors.
The complete bridge gate still exits 1 after 39 upstream/native warnings, now at
`TrueTypeSubsetter/TtfDelta/ttfdelta.cpp:46`: uninitialized locals are unsupported.
The registry call, aggregate initializer and FILETIME field failures are gone.

Whole implementation-file evidence is now **two of the 21 directly included
DirectWrite bodies**: LocalizedErrorMsgs.cpp and util2.cpp, each checked in a
focused dependency context. The 620 C# foundation inputs remain compiled. No
complete DirectWriteForwarder/System.Printing assembly, WPF package or
WindowsDesktop SDK has been produced. TrueType subsetter lowering/native
partitioning, further native COM/font operations, missing PresentationNative
exports, protocol/version matching and desktop packaging remain open. Frontend
changes remain in the development build; installed packages are unchanged.
Logs, original registry sources, native ABI checks and emitted IL are archived
under `tmp/wpf8-support/validation/windows-api-registry-20261012` on the local host.

## 33. Actual TrueType subsetting through a native split bridge

The 2026-10-12 continuation first removes the declaration-initializer gate.
Every generated method already uses `.locals init`, and WPF's project explicitly
requests `/clr:initLocals`. Declarations without initializers therefore use
zeroed method-entry storage; they do not reset that storage on loop re-entry.
The focused test checks primitive, native pointer and native enum zero values
and repeated block entry. The full unsplit bridge advances to unsupported
native callback-pointer storage in TtfDelta.

`wpf.sh native-subsetter` now builds the actual upstream algorithm, including all
14 TtfDelta implementation inputs, into `wpf-subsetter.dll` and a named x64 COFF
import archive. Patch 0005 adds conditional native initialization paths to the
two original table-initialization routines: `std::call_once` replaces managed
Monitor/finally scaffolding while retaining the table assignments. The native
translation unit supplies the standard headers outside the upstream namespace,
legacy Win32 min/max semantics, ttBoolean and MinGW SAL spellings. It uses
C++14 and `-ftrivial-auto-var-init=zero`. Native initialization failure returns
the original generic font error instead of unwinding across the CLR boundary.
The DLL depends on the trusted CLANG64 libc++ runtime and Windows UCRT/OS APIs.

Patch 0005 also adds an opt-in `WPF_SUBSETTER_SPLIT` path to the complete original
managed truetype.cpp wrapper. Its native call uses the same subset format,
language/list settings and allocator/free functions as upstream. Allocation
callbacks stay wholly native. The managed pin, Marshal.Copy, finally cleanup,
FileFormatException and original-font fallback remain in the wrapper. The
upstream managed initialization path remains available when the split flags
are absent. Native DLL-import declarations in user headers can now bind against
an explicitly supplied import archive as well as system headers; unrelated
unbound functions are not assigned to the subsetter DLL.

`wpf.sh check-subsetter` compiles the complete patched truetype.cpp and its
original declaration in a focused dependency context. A C# consumer invokes
the actual internal ComputeSubset method, compares its output byte for byte
with a direct native call, and independently checks sfnt table bounds, the
requested glyph outlines and the full TrueType checksum. Windows accepts and
removes the resulting in-memory font. Concurrent first use checks native table
initialization, malformed input produces FileFormatException, and the operation
still works after compacting GC. With the Windows-installed Arial input and
glyphs 0, 36, 37 and 171, the font shrinks from **1,045,720 to 317,024 bytes**.
No font is bundled in the bootstrap or redistributed in the evidence archive.

The next full-bridge failure exposed MinGW's constexpr overload of enum `operator|`.
The emitter now asks Clang to evaluate system-header constexpr integer/enum
calls with `SE_NoSideEffects`. A proven result becomes an IL constant. Dynamic
calls and calls with side effects remain errors unless their implementation or
native bridge exists; no SDK function is executed by the compiler. Focused checks
exercise the real SDK enum operators, an unsigned 64-bit high-bit constant and
the rejection paths. The existing font-simulation check now includes the complete
MinGW Windows/dwrite headers rather than extracting only the enum declaration.

Validation on win11:

```sh
MSYSTEM=CLANG64 M612=/home/topkek/dotnetboot/m612 \
  bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh build
MSYSTEM=CLANG64 M612=/home/topkek/dotnetboot/m612 \
  bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh \
  test-sdk-constants test-init-locals test-windows-api
MSYSTEM=CLANG64 M612=/home/topkek/dotnetboot/m612 \
  bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh \
  test-native-fields test-interop test-pinning
M612=/home/topkek/dotnetboot/m612 MSYSTEM=CLANG64 \
  WPF_BUILD=/home/topkek/wpfboot/build \
  WPF_CPPCLI_BUILD=/home/topkek/dotnetboot/m612/build/cppcli \
  bash ~/wpfboot/driver/bootstrap/m612/wpf.sh \
  native-subsetter check-font-simulations check-subsetter bridge-gate
```

The frontend build and focused checks pass. The native algorithm build has
83 upstream warnings; the managed wrapper retains one upstream module-attribute
warning, and its C# consumer builds with zero warnings/errors. The full bridge
gate still exits 1, now after three warnings at NativePointerWrapper.cpp:35:
`IUnknown::Release` requires native COM method dispatch. The subsetter and constant
enum flag failures are absent. Remaining startup units, COM/native allocation,
System.Printing, PresentationCore/PresentationFramework, required native exports,
protocol/version alignment and WindowsDesktop packaging remain unfinished.

Evidence now covers **three of the 21 directly included implementation bodies**:
the two unchanged components from earlier turns plus the complete patched
truetype.cpp wrapper; its 14 nested native algorithm inputs are also built.
This does not prove the complete DirectWriteForwarder assembly. The 620 C#
foundation inputs remain compiled, and no complete WPF package is produced.
Changes remain in the development build. Logs, import/ABI manifests, native DLL,
patched sources and emitted IL are archived under
`tmp/wpf8-support/validation/native-subsetter-20261012` on the local host.

## 34. Native COM dispatch, native heap values and original handle cleanup

The 2026-10-12 continuation implements raw native COM virtual calls on Windows
x64. The supported interface shape is an IUnknown-rooted single nonvirtual
base chain at offset zero, with no instance fields and pure virtual methods.
Clang's ABI vtable context supplies the selected method slot. The emitter
evaluates the receiver once, passes native scalar/pointer arguments and native
POD/scalar references, reads the function pointer from that vtable, and emits
unmanaged `calli`. HRESULT values stay unmodified; this path does not create an
RCW or automatically marshal arguments. Zero-offset interface upcasts and raw
native-pointer boolean conversions retain the actual address. Bool returns,
by-value record signatures, variadic methods, arbitrary native class dispatch,
secondary/virtual-base vtables and non-x64 Windows remain unsupported.

The native COM test uses actual Clang-built interfaces/objects and checks inherited
IUnknown slots, AddRef/Release counts, GUID reference input, pointer/BOOL outputs,
negative HRESULT preservation and single receiver evaluation. Another interface
checks a double result with mixed float/double/64-bit integer arguments extending
past the register argument slots, plus a void return with an output pointer.
`wpf.sh check-native-com` calls actual DirectWrite factory, collection, family
and localized-string interfaces through generated calli. It enumerates 81 font
families and compares a Unicode name with independently compiled native SDK calls
after compacting GC, using CLR-pinned character buffers.

Native single scalar/trivial-POD allocation and deletion now use the source-built
NativeMemory API, whose Windows implementation calls UCRT malloc/free. The correct
reference identity is System.Runtime.InteropServices; the driver includes that
source-built facade by default and deduplicates reference paths. Clang supplies
the allocation size and resolved initializer. Value initialization, native field
access after GC, erased void-pointer cleanup and null deletion are checked.
The native test verifies the UCRT allocation with `_msize` and transfers ownership
in both directions between generated CLR code and real C++ new/delete. Custom
operators, native arrays/delete[], placement new, nontrivial objects/destructors,
managed-value heap storage and alignment above eight bytes remain unsupported.
Side-effectful initializers at an empty evaluation stack use CLR finally cleanup
if initialization throws; nested expressions with an occupied evaluation stack
are explicitly rejected until their stack preservation is implemented. Ordinary
constant/trivial initialization can appear in constructor arguments.

The complete original NativePointerWrapper.cpp and header compile byte for byte
in `wpf.sh check-native-cleanup`, with a focused Common.h dependency context.
Its actual NativeIUnknownWrapper<IUnknown> and NativePointerWrapper<_GUID>
specializations now execute both original ReleaseHandle bodies. The consumer
checks COM object counts, newly allocated GUID ownership, invalid/closed state,
32 repeated disposal cycles, compacting GC and double Dispose without a second
release. This check exposed a separate metadata bug: an unused closed-template
property getter was declared in CLR property metadata without an emitted method.
The frontend now asks Clang to instantiate instance property accessor definitions
before collecting those closed source-template bodies. Reflection invokes the
original Value getter before and after cleanup even though no C++ call uses it.

Validation on win11:

```sh
MSYSTEM=CLANG64 M612=/home/topkek/dotnetboot/m612 \
  bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh build
MSYSTEM=CLANG64 M612=/home/topkek/dotnetboot/m612 \
  bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh \
  test-native-com test-native-allocation test-managed-templates test-value-pointers test-com
MSYSTEM=CLANG64 M612=/home/topkek/dotnetboot/m612 \
  bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh test-native-allocation
M612=/home/topkek/dotnetboot/m612 MSYSTEM=CLANG64 \
  WPF_BUILD=/home/topkek/wpfboot/build \
  WPF_CPPCLI_BUILD=/home/topkek/dotnetboot/m612/build/cppcli \
  bash ~/wpfboot/driver/bootstrap/m612/wpf.sh \
  check-native-cleanup check-native-com bridge-gate
```

The frontend build and focused/regression checks pass. The original cleanup
component retains its one upstream void-delete warning; the allocation test
retains two such warnings, and C# check builds report zero warnings/errors.
The complete bridge gate still exits 1 after three warnings. Its next error is
the unsupported native DWrite factory function-pointer type
`long (enum DWRITE_FACTORY_TYPE, const struct _GUID &, struct IUnknown **)`.
IUnknown::Release and erased native deletion are no longer the gate failures.

Full implementation-body evidence is now **four of the 21 directly included
DirectWrite bodies**, with NativePointerWrapper.cpp joining the previous three
components. The 14 nested native subsetter inputs and 620 C# foundation inputs
remain built. This remains development frontend work: no complete
DirectWriteForwarder/System.Printing assembly, WPF framework or WindowsDesktop
package is produced. Function pointers/native callbacks, further native object
boundaries, remaining startup units, higher WPF, native-export coverage and
desktop packaging remain open. Logs, original cleanup sources, emitted IL and
native test artifacts are archived under
`tmp/wpf8-support/validation/native-com-cleanup-20261012` on the local host.

## 35. Native factory function pointers, SDK UUIDs and managed startup

The 2026-10-12 continuation implements Windows x64 native function-pointer
storage and invocation. Function pointers use opaque native integer storage in
supported locals, fields, parameters and returns. A fixed Clang function prototype
supplies the unmanaged calli signature; the callee is evaluated exactly once
before its arguments. Pointer-dereference call syntax works without loading a
function body as data. The native test checks mixed float/double/64-bit integer
arguments extending beyond register slots, native scalar references, void/output
calls, field/return storage and compacting GC. Variadic, managed/bool/by-value-record
marshalling, reference-return and arbitrary managed method-address cases remain
outside this path. Calli targets require Windows x64 and the platform C ABI.

The trusted MinGW headers implement `__uuidof` through `__mingw_uuidof` templates.
The frontend now inspects their Clang AST return/static-declaration shapes and
uses constant evaluation to read the SDK initializer. It validates the native
GUID's 16-byte layout, integer field widths/offsets and eight-byte tail. GUID
bytes become assembly-visible initonly RVA fields, giving both global and class
methods a stable native address without a movable GC allocation. Pointer-type
UUID spelling resolves through the SDK's actual template chain. C++14 and C++17
variants both pass real DWriteCreateFactory and QueryInterface calls, including
GC between calls. This is SDK AST handling rather than a hardcoded interface IID
table; general native constant-reference functions/globals remain unimplemented.

Clang's zero-offset COM interface cast handling also covers checked base-to-derived
casts. Native sizeof/alignof expressions use Clang constant layout evaluation and
leave their operands unevaluated. The focused test checks WCHAR, pointer, GUID and
native record sizes/alignment and a side-effectful operand that must not execute.
Managed records/facades are rejected because their surrogate C++ layout is not a
CLR layout calculation.

The next full-gate error was a real missing compilation unit: Factory calls
GetDWriteCreateFactoryFunctionPointer from main.cpp. The gate now combines the
actual DirectWriteForwarder.cpp and main.cpp sources in one CLR translation unit.
Patch 0006 adds an opt-in managed startup path in main.cpp, replacing the native
CModuleInitialize/atexit workaround with a managed owner initialized before the
first factory lookup. It retains the original NativeWPFDLLLoader load/unload/getter
methods and registers ProcessExit and DomainUnload cleanup. Native subsetter tables
retain their separate once-only initialization. The legacy path remains available
without the new flag. The unused legacy Utils.hxx include is excluded from this
path because its codeanalysis annotation header is absent in MinGW.

`wpf.sh native-dwrite-loader` builds the actual upstream dwriteloader.cpp, with
one explicit function-to-void-pointer cast for Clang, behind a small named C ABI.
Its original LoadLibraryEx search remains restricted to System32. The managed
startup calls that source-built DLL through its actual import archive; unloading
uses the original FreeLibrary/error handling. Lifecycle validation performs eight
concurrent first lookups, calls the actual factory, verifies exactly one registered
handler for each event, invokes registered cleanup twice, checks the cleared
pointer and reloads. A later ProcessExit observer verifies the WPF handler actually
clears the pointer during real runtime shutdown. The check requires and observes
`actual WPF ProcessExit cleanup OK`, not just a successful main return.

Validation on win11:

```sh
MSYSTEM=CLANG64 M612=/home/topkek/dotnetboot/m612 \
  bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh build test-function-pointers
MSYSTEM=CLANG64 M612=/home/topkek/dotnetboot/m612 \
  bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh \
  test-function-pointers test-native-com test-native-allocation test-delegates
M612=/home/topkek/dotnetboot/m612 MSYSTEM=CLANG64 \
  WPF_BUILD=/home/topkek/wpfboot/build \
  WPF_CPPCLI_BUILD=/home/topkek/dotnetboot/m612/build/cppcli \
  bash ~/wpfboot/driver/bootstrap/m612/wpf.sh \
  check-dwrite-startup check-function-pointer bridge-gate
```

The build and focused/regression checks pass. Startup retains two upstream
legacy assembly-attribute warnings; native sizeof's intentionally side-effectful
operand produces its expected unevaluated-expression warning. C# lifecycle builds
have zero warnings/errors. The final combined full gate still exits 1 after five
warnings, now at Font.cpp:193, `unsupported compound assignment`. Factory's native
prototype, missing startup helper, SDK UUIDs, interface downcast and sizeof(WCHAR)
failures are gone. Legacy NGEN dependency attributes are still unimplemented.

The 620 C# foundations, 14 nested native subsetter inputs and four complete
directly included DirectWrite implementation bodies remain verified. The ported
main.cpp startup unit and native shared loader are additionally compiled and
checked; they do not increase the four-of-21 body count. No complete
DirectWriteForwarder/System.Printing assembly, higher WPF framework or WindowsDesktop
package is produced. Native callback/general object boundaries, remaining startup
metadata/units, higher WPF, native-export coverage and packaging remain open.
Development logs, original/patched startup sources, loader import/ABI artifacts
and generated IL are archived under
`tmp/wpf8-support/validation/function-pointer-startup-20261012` on the local host.

## 36. Compound flags, native arrays and native pointer pins

The 2026-10-12 continuation passes the next three DirectWrite IL gates:
Font.cpp flag compound assignments, FontFace::GetFileZero's native pointer-array
allocation, and FontFace's native pointer parameters copied into pin_ptr locals.
The combined original DirectWriteForwarder.cpp and ported main.cpp build now
stops at FontFace.cpp:250, native pointer addition in ReadFontEmbeddingRights.

All ten builtin scalar compound operations now use Clang's computation types,
signed division/remainder/right shift, final destination narrowing and bool
normalization. The emitter captures the RHS before the LHS location and captures
field receivers, indices and pointer receivers once. CLI enum OR/AND/XOR
compounds retain the original enum lvalue while Clang checks and promotes the
underlying arithmetic. Native scoped enums, mixed CLI enum types, const enum
lvalues and unmanaged pointer arithmetic remain rejected. Runtime tests cover
byte wraparound, floats, unsigned high bits, mixed 32/64-bit computation,
64-bit shift counts, explicit/safe_cast widening and observable evaluation order.
Numeric conversions also preserve the source signedness when widening to
64 bits or converting unsigned integers to floating point.

Native scalar, unmanaged pointer and trivial POD arrays now allocate through
the source-built NativeMemory Alloc/AllocZeroed APIs and delete[] through Free.
Uninitialized and empty value-initialized arrays are supported; element stride
comes from Clang's native layout. Indexed loads, stores, addresses, compound
updates and increments preserve single evaluation and native memory prefixes.
The native oracle checks a 16-byte POD stride, 64-bit field values and the real
UCRT allocation size. Zeroed primitive/pointer storage, count/index evaluation,
GC, zero-length arrays and cleanup pass. Negative signed bounds are checked
before conversion to size_t and throw CLR OverflowException; allocation failure
uses CLR OutOfMemoryException. These are CLR exceptions, not native C++ new
exception ABI support. Nonempty array initialization, nontrivial native objects,
placement/custom allocators, larger alignment and general array-field operations
remain unsupported.

Primitive pin_ptr locals now accept native pointer initializers, as allowed by
the [C++/CLI pin_ptr specification](https://learn.microsoft.com/en-us/cpp/extensions/pin-ptr-cpp-cli?view=msvc-170).
They retain CLR pinned-byref metadata and scope-finally clearing. The focused
runtime test covers const native input parameters, native output parameters,
dereference writes/compound updates/increments, single evaluation of returned
interior-pointer receivers, native-to-managed-to-native reassignment,
null clearing and forced GC. Existing Unicode/array pinning tests also pass
under concurrent compacting GC. General native pointer arithmetic and interior
pointer assignment/escape analysis remain separate work.

The new WPF check extracts the exact GetFileZero method bytes from the original
FontFace.cpp. The surrounding FontFace/FontFile declarations and HRESULT helper
are focused fixtures; this is not a complete FontFace.cpp build. It includes the
complete unchanged original NativePointerWrapper.cpp/header. A source-built
native probe creates a real system-font IDWriteFontFace through DirectWrite.
The original method calls GetFiles, allocates/indexes/frees its native pointer
array, and transfers the first file reference into its managed wrapper. The C#
consumer disposes the font face, forces GC, and verifies the returned file using
both generated COM Analyze dispatch and an independently compiled native Analyze
oracle. Thirty-two iterations and repeated face/file disposal pass. The system
font exercises the normal success path; injected GetFiles failures and synthetic
multi-file fonts are not claimed by this check.

Validation on win11, with the source-built SDK/runtime/ilasm:

```sh
MSYSTEM=CLANG64 M612=/home/topkek/dotnetboot/m612 \
  bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh \
  build test-compound test-native-arrays test-native-pins
MSYSTEM=CLANG64 M612=/home/topkek/dotnetboot/m612 \
  bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh build test-native-pins
MSYSTEM=CLANG64 M612=/home/topkek/dotnetboot/m612 \
  bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh test-compound
MSYSTEM=CLANG64 M612=/home/topkek/dotnetboot/m612 \
  bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh \
  test-pinning test-enums test-native-fields test-native-allocation
M612=/home/topkek/dotnetboot/m612 MSYSTEM=CLANG64 \
  WPF_BUILD=/home/topkek/wpfboot/build \
  WPF_CPPCLI_BUILD=/home/topkek/dotnetboot/m612/build/cppcli \
  bash ~/wpfboot/driver/bootstrap/m612/wpf.sh check-font-file-zero bridge-gate
```

The frontend rebuild, focused checks and regressions pass. The WPF C# consumer
build has zero warnings/errors. The combined full gate exits 1 after five
upstream warnings at the explicit unsupported pointer addition; it produces no
complete bridge assembly. Complete DirectWrite implementation-body evidence
remains **four of 21**; the 620 C# foundation inputs, 14 nested native subsetter
inputs and startup/native-loader checks remain the previous verified scope.
System.Printing, higher WPF, native-export coverage and WindowsDesktop packaging
remain incomplete. This is development frontend work, with no new installed
WPF package. Logs, original extracted method/wrappers, emitted IL and native
probe artifacts are archived under
`tmp/wpf8-support/validation/compound-native-arrays-pins-20261012` on the local host.

## 37. Native pointer arithmetic, native Strings and complete FontFile

The 2026-10-13 continuation passes FontFace.cpp:250 native pointer addition,
FontFile.cpp:85's explicit REFIID reference cast and FontFile.cpp:91's native
UTF-16 String construction. The combined original DirectWriteForwarder.cpp and
ported main.cpp gate now stops at TextItemizer.cpp:30: indexing the native
DWriteTextAnalysisNode::Range inline array field. No complete bridge assembly is
produced by that gate.

Native scalar/pointer/trivial POD pointers now support addition/subtraction,
integer-plus-pointer, signed element differences, unsigned address comparisons,
compound updates and pre/post increment/decrement. Element strides come from
Clang layout, and update locations are captured once, including packed native
fields, managed fields, pointer-array slots, native references and side-effectful
indirect locations. Compound updates capture the RHS first. Managed field
locations remain tracked byrefs while native storage addresses use the same
unverifiable CLR address form. Runtime checks cover negative/unsigned offsets,
negative differences, 16-byte POD strides, all update forms, native oracle
comparison, GC and observable receiver/index evaluation counts.

Native GC-free managed-value pointers preserve pointer provenance through these
operations and native indexing. Their stride uses CLR sizeof rather than Clang's
surrogate record size. A public value type with explicit offsets and Size=32 is
pinned in a C# array; pointer returns, positive/negative offsets and differences,
field reads/writes, compound/increment updates and GC all match C#'s actual
32-byte stride. Void/function/incomplete/array/nontrivial element arithmetic,
movable unpinned storage and general intrinsic-pointer operations remain outside
this support. Existing native value-pointer/boxed-handle rejection checks pass.
The older object suite's blanket native value-pointer rejection is updated to
reject value storage containing a GC reference; that object suite also passes.

No-op explicit native reference casts recurse through native address binding,
retaining checks for stable native storage. A native POD reference cast passes;
a cast of a managed owner's integer field still fails the lifetime check.
This supports the original FontFile QueryInterface argument without turning a
reference cast into a permission to pass movable managed memory to native code.

The native wchar_t/char String constructor facades now bind to verified real
char*/sbyte* constructor signatures in source-built .NET metadata. Other
Clang-selected imported String constructors use their real newobj signatures.
The literal copy path remains unchanged. Tests cover UTF-16 lambda and surrogate
pairs, a pointer slice through the imported three-argument constructor, typed
null, narrow ASCII, source-buffer modification/freeing and GC. Narrow decoding
belongs to the CLR constructor; no encoding translator or external managed
binary is introduced.

The FontFace component check now extracts three further original methods
byte-for-byte: GetArrayOfGlyphIndices, TryGetFontTable and
ReadFontEmbeddingRights. The complete original OpenTypeTableTag header is also
included. Actual DirectWrite glyph results for Latin/Greek/supplementary code
points match independent native calls; OS/2 table copying and big-endian fsType
match a separate native parser, and a nonexistent table returns false/null.
These are focused methods with surrounding fixture declarations, not a full
FontFace.cpp runtime proof. The normal real-font paths pass; injected failure
and malformed-font paths are not claimed.

A new component check compiles the **complete unchanged FontFile.cpp and
FontFile.h**, plus the original FontFileType/FontFaceType and
NativePointerWrapper source headers/bodies. Common.h is a focused context with
the exact original ToGUID method and a basic HRESULT exception helper; the
unused DWriteTypeConverter include is an explicit placeholder. The test imports
the actual source-built WindowsBase Invariant through #using as_friend and uses
the original DirectWriteForwarder assembly name/public key. It runs the original
static GUID constructor, instance constructor, Analyze, native pointer property,
GetUriPath, ReleaseInterface and destructor. The C# consumer verifies private
source type/property metadata, IDisposable and Analyze Out parameters.

A native DirectWrite probe acquires an actual system-font file. The original
Analyze result matches an independent native Analyze call. The original UTF-16
path matches a native local-loader/GetReferenceKey/GetFilePathFromKey oracle,
exists on disk, and remains valid after the original finally frees its native
character array and the file is disposed twice. Thirty-two iterations and forced
GC pass. This validates the normal local-file path; the nonlocal-loader and
error paths are not claimed. The original loader lifetime behavior is retained.
The test DLL is named DirectWriteForwarder for friend access but contains this
focused component, not the complete bridge.

Validation on win11:

```sh
MSYSTEM=CLANG64 M612=/home/topkek/dotnetboot/m612 \
  bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh build test-pointer-arithmetic
MSYSTEM=CLANG64 M612=/home/topkek/dotnetboot/m612 \
  bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh build test-native-strings
MSYSTEM=CLANG64 M612=/home/topkek/dotnetboot/m612 \
  bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh \
  test-native-arrays test-compound test-value-pointers test-native-storage test-pinning
MSYSTEM=CLANG64 M612=/home/topkek/dotnetboot/m612 \
  bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh \
  test-core test-native-strings test-pointer-arithmetic
MSYSTEM=CLANG64 M612=/home/topkek/dotnetboot/m612 \
  bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh test-objects
M612=/home/topkek/dotnetboot/m612 MSYSTEM=CLANG64 \
  WPF_BUILD=/home/topkek/wpfboot/build \
  WPF_CPPCLI_BUILD=/home/topkek/dotnetboot/m612/build/cppcli \
  bash ~/wpfboot/driver/bootstrap/m612/wpf.sh \
  check-font-file-zero check-original-font-file bridge-gate
```

The rebuilds and focused/runtime/regression checks pass. FontFile retains one
upstream erased-void-delete warning; OpenTypeTableTag retains its upstream
trailing #endif tokens. C# builds have zero warnings/errors. The full gate exits
1 after five upstream warnings at TextItemizer.cpp:30. Complete directly
included implementation-body proof is now **five of 21**, with FontFile.cpp
joining LocalizedErrorMsgs.cpp, util2.cpp, the ported truetype.cpp and
NativePointerWrapper.cpp. The 620 C# foundation inputs, 14 nested native subsetter
inputs and startup/native-loader proof remain the previous compiled scope.
System.Printing, higher WPF, native text-export coverage and WindowsDesktop
packaging remain incomplete, and this development frontend is not a new
installed WPF package. Original component sources, generated IL/metadata, native
probes and logs are archived under
`tmp/wpf8-support/validation/pointer-arithmetic-font-file-20261013` on the local host.

## 38. Complete DirectWrite bridge and source-built PresentationCore

The 2026-10-13 continuation passes the remaining DirectWrite IL gates. All
**21 directly included implementation bodies** now compile together with the
ported managed startup and the original OtherAssemblyAttrs.cpp. Source-built
ilasm assembles the full DirectWriteForwarder.dll, CLR loads its **85 types**,
and RuntimeHelpers.PrepareMethod succeeds for **283 eligible managed method
bodies**, including global helpers. Native import declarations and abstract
interface methods have no managed body and are excluded. This validates CLR
body/metadata resolution; it does not execute every path.

Native one-dimensional inline POD array fields now decay to their actual Clang
byte offset inside the containing record. Indexed scalar/pointer/POD reads,
stores, addresses and updates reuse native indexing and preserve the original
storage. Packed offsets, 64-bit values, volatile elements, copy isolation,
receiver/index single evaluation, sizeof and GC match an independently compiled
native oracle. Whole-array field initialization and multidimensional array
strides remain unsupported.

Integral native bitfields now use Clang's bit offsets and bounded byte spans
for read/write, compound operations, pre/post updates and aggregate initialization.
Unsigned masks, signed extension and stored-width normalization preserve result
values and neighboring bits. Runtime/native comparison covers a packed 10-byte
record with 19/17/28-bit fields, cross-byte boundaries, signed overflow/truncation,
a one-byte 3/5-bit record, bool fields and volatile byte fields. Exact SDK
DWRITE_SHAPING_TEXT_PROPERTIES bits and raw two-byte representation match native
Clang. The implementation requires little-endian 8-bit bytes, integral/enum
storage up to 64 bits, at most 64 covered bits and no excess padding width;
volatile fields currently require byte-sized allocation units. Independently
addressing bitfields/native-reference temporaries remains rejected.

The range-node probe extracts the original DWriteTextAnalysisNode definition and
TextItemizer constructor/GetNextSmallestPos/SetIsDigit method bytes. It uses
native script/number linked nodes and actual managed generic digit lists, and
verifies ordering, ties, cursor/index progression, empty/exhausted sentinels and
repeated GC. This is a focused-method proof, not the complete TextItemizer runtime
API. A second probe uses actual native DirectWrite AnalyzeScript/GetGlyphs on a
real system font and the text "office". The exact original WPF shaped-alone loop
reads the returned bitfields and agrees with an independent native reader after
GC. Unused native analysis-sink callbacks return E_NOTIMPL and are not called;
no native WPF text export is fabricated.

Namespace-scope InternalsVisibleTo attributes now retain literal UTF-8 identities
through Clang annotations and emit real CLR assembly custom attributes with
compressed SerString lengths. Imported named TypeForwardedTo targets retain
Clang type resolution and emit real exported-type forwarders. Individual
canonical/unqualified attributes are supported; arbitrary assembly attributes,
nested/generic forwarders and legacy NGEN Dependency attributes remain incomplete.
The complete original OtherAssemblyAttrs.cpp supplies the PresentationCore
public-key friend identity and WindowsBase TextFormattingMode forwarder. A
source-built, public-signed C# peer named PresentationCore compiles direct calls
to the bridge's internal APIs and executes them successfully. This peer is a
validation assembly, not the actual PresentationCore build below.

The complete bridge runtime check registers its original managed COM font-file
and collection loaders with a real DirectWrite factory, retrieves 81 system font
families, reads a Unicode ordinal family name, creates a font/face, reads glyph
count and OS/2 embedding rights, retrieves the owned first font file and copies
its actual native Unicode path. The path exists on disk, and face/file repeated
disposal and forced GC pass. The check also verifies the assembly friend attribute
and resolves its forwarded TextFormattingMode to actual source-built WindowsBase.

PresentationCore then builds from **1,337 C# source inputs** against the complete
bridge and existing source-only dependencies. Its **2,290 CLR types** load. The
runtime test verifies matrix/point transformation, actual Arial FontFamily and
Typeface resolution, nonempty GlyphTypeface glyph count and character-to-glyph
metadata. It does not create or render a WPF window. The previous 620 foundation
inputs bring the compiled C# scope to 1,957 inputs; the 14 nested native subsetter
inputs remain built separately.

Validation on win11:

```sh
MSYSTEM=CLANG64 M612=/home/topkek/dotnetboot/m612 \
  bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh \
  build test-native-bitfields test-native-inline-arrays test-native-fields
MSYSTEM=CLANG64 M612=/home/topkek/dotnetboot/m612 \
  bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh \
  test-native-arrays test-pointer-arithmetic test-compound test-native-storage
MSYSTEM=CLANG64 M612=/home/topkek/dotnetboot/m612 \
  bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh build
MSYSTEM=CLANG64 M612=/home/topkek/dotnetboot/m612 \
  bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh \
  test-assembly-attributes test-native-bitfields test-native-inline-arrays test-native-fields
M612=/home/topkek/dotnetboot/m612 MSYSTEM=CLANG64 \
  WPF_BUILD=/home/topkek/wpfboot/build \
  WPF_CPPCLI_BUILD=/home/topkek/dotnetboot/m612/build/cppcli \
  bash ~/wpfboot/driver/bootstrap/m612/wpf.sh \
  check-range-nodes check-shaping-bits bridge-gate
M612=/home/topkek/dotnetboot/m612 MSYSTEM=CLANG64 \
  WPF_BUILD=/home/topkek/wpfboot/build \
  WPF_CPPCLI_BUILD=/home/topkek/dotnetboot/m612/build/cppcli \
  bash ~/wpfboot/driver/bootstrap/m612/wpf.sh \
  build-directwrite check-directwrite-assembly
M612=/home/topkek/dotnetboot/m612 MSYSTEM=CLANG64 \
  WPF_BUILD=/home/topkek/wpfboot/build \
  WPF_CPPCLI_BUILD=/home/topkek/dotnetboot/m612/build/cppcli \
  bash ~/wpfboot/driver/bootstrap/m612/wpf.sh check-directwrite-assembly
M612=/home/topkek/dotnetboot/m612 MSYSTEM=CLANG64 \
  WPF_BUILD=/home/topkek/wpfboot/build \
  WPF_CPPCLI_BUILD=/home/topkek/dotnetboot/m612/build/cppcli \
  bash ~/wpfboot/driver/bootstrap/m612/wpf.sh \
  build-presentation-core check-presentation-core
```

The frontend rebuild, focused/regression/native oracle checks, full bridge gate,
ilasm assembly, signed friend client and CLR/runtime checks pass. The full
bridge retains its upstream path/endif/erased-delete and legacy NGEN attribute
warnings. PresentationCore's first source build has 36 warnings and zero errors;
its runtime consumer has zero warnings/errors. Logs and source/IL/DLL/metadata
artifacts are archived under
`tmp/wpf8-support/validation/full-directwrite-presentation-core-20261013` on the
local host.

The bridge currently uses the default emitted assembly version; upstream
version/file metadata still needs packaging integration. System.Printing,
PresentationFramework, remaining desktop assemblies, XAML SDK/build tasks,
reference/framework packs and WindowsDesktop packaging remain incomplete.
PresentationNative still has the previously measured 38/139 required exports,
with 101 text/paragraph/natural-language exports absent; gfx/PenImc version and
protocol matching also remains open. The complete source/rendered-window/package
objective is not achieved, and no WPF package is installed by this checkpoint.

## 39. WPF assembly versions and the PresentationFramework source graph

The next 2026-10-13 continuation emits source-derived assembly version metadata
for DirectWriteForwarder. Namespace-scope System.Reflection string attributes
now emit verified CLR attribute constructors and UTF-8 values; AssemblyVersion
sets the actual assembly `.ver`. The supported version syntax has exactly four
numeric components in 0..65534, with wildcard/range/duplicate negatives. The WPF
driver mirrors the upstream Wpf.Cpp.targets generated version source: assembly
and file versions 8.0.0.0 and informational version 8.0.0. Reflection verifies
those values, the complete bridge checks pass again, and PresentationCore
rebuilds and passes its real-font runtime check against that versioned bridge.

`tools/wpfgraph.py` builds the upstream C# ProjectReference graph, including the
projects in Microsoft.DotNet.Wpf/cycle-breakers and System.Printing/ref. These
compile-only reference assemblies retain the actual upstream assembly names,
versions and public keys, stay in their individual build directories, and are
never copied into the runtime library directory. `tools/wpfplan.py` records the
resolved dependency/source map and supports explicit projects and reference-only
output. This breaks the original printing/ReachFramework/PresentationFramework/
PresentationUI compilation cycles without substituting runtime implementations.

Real ReachFramework builds from **285 source inputs** with zero warnings/errors.
Real PresentationFramework builds from **1,345 inputs** with four warnings and
zero errors. Together with the previous foundations and PresentationCore, this
is **3,587 C# implementation inputs**, plus **30 compile-only reference inputs**.
The first PresentationFramework consumer compiles with zero warnings/errors,
but runtime child insertion fails because wpfgfx_cor3.dll is absent. Control
construction before that operation is not a completed control/XAML/window proof.

Validation on win11:

```sh
MSYSTEM=CLANG64 M612=/home/topkek/dotnetboot/m612 \
  bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh \
  build test-assembly-versions test-assembly-attributes
M612=/home/topkek/dotnetboot/m612 MSYSTEM=CLANG64 \
  WPF_BUILD=/home/topkek/wpfboot/build \
  WPF_CPPCLI_BUILD=/home/topkek/dotnetboot/m612/build/cppcli \
  bash ~/wpfboot/driver/bootstrap/m612/wpf.sh \
  build-directwrite check-directwrite-assembly \
  build-presentation-core check-presentation-core
M612=/home/topkek/dotnetboot/m612 MSYSTEM=CLANG64 \
  WPF_BUILD=/home/topkek/wpfboot/build \
  bash ~/wpfboot/driver/bootstrap/m612/wpf.sh build-reach-framework
M612=/home/topkek/dotnetboot/m612 MSYSTEM=CLANG64 \
  WPF_BUILD=/home/topkek/wpfboot/build \
  bash ~/wpfboot/driver/bootstrap/m612/wpf.sh build-presentation-framework
```

All listed commands pass. System.Printing's implementation, remaining desktop
assemblies, complete native text coverage, generated themes, framework packs and
WindowsDesktop packaging remain open. No completed WPF package is claimed.

## 40. WPF 8 native compositor, rendered pixels and XAML build tasks

The WPF 8 compositor now builds with the MinGW graphics port carried in
`wpf-native/gfx` (provenance and limitations are recorded in its README).
All six graphics patches apply cleanly to the original v8.0.0 source. The
driver builds a private WpfGfx/Shared copy and regenerates shaders using the
text assembler and trusted MSYS2 vkd3d. The source version resource is 8.0.0;
the handshake test includes the actual WPF 8 protocol header. Its Ninja graph
has **466 C/C++ compilation inputs**. Every one of the **106 upstream exports**
resolves, correct/incorrect protocol handshakes behave as expected, and shader
tokens/resources validate. The inherited port disables WPP tracing and does
not implement SEH-to-C++ exception translation; these paths remain limitations.

The PresentationFramework runtime consumer now passes actual Button/StackPanel
child insertion and dependency properties, parses source XAML, and loads all
**3,248 CLR types**. It lays out a Canvas/Rectangle and uses RenderTargetBitmap
to render a 120x80 bitmap through the compositor. A copied interior pixel is
verified as opaque red before a real PNG encoder writes source-wpf-render.png.
This is a real raster-rendering proof, not yet a shown WPF application window
or a complete desktop package. The partial PresentationNative text exports
remain unchanged.

PresentationBuildTasks compiles from **96 source inputs** and one resource,
bringing implementation C# scope to **3,683 inputs**. The planner uses the
already source-built SDK's MSBuild references and OOB CodeDom/MetadataLoadContext.
For the net8.0 markup compiler, source patch 0007 selects System.Runtime as its
metadata core assembly because the source reference pack's mscorlib facade
does not forward System.Void. The consumer selects one reference per assembly,
replacing the empty core-pack WindowsBase facade with actual source WPF
WindowsBase. The real MarkupCompilePass1 task then generates MarkupView.baml
and MarkupView.g.cs, including a named Rectangle field, from source XAML.

The real ResourcesGenerator task packages that BAML into the consumer's
`.g.resources`. The consumer compiles the generated C# and embeds those
resources, calls the generated InitializeComponent, and checks the connected
Rectangle field, dimensions and red brush. Compiled BAML loading, text XAML,
control behavior, all CLR types and shape/background pixels pass together.

Validation on win11:

```sh
M612=/home/topkek/dotnetboot/m612 MSYSTEM=CLANG64 \
  WPF_BUILD=/home/topkek/wpfboot/build \
  bash ~/wpfboot/driver/bootstrap/m612/wpf.sh \
  build-presentation-tasks check-presentation-tasks
M612=/home/topkek/dotnetboot/m612 MSYSTEM=CLANG64 \
  WPF_BUILD=/home/topkek/wpfboot/build \
  bash ~/wpfboot/driver/bootstrap/m612/wpf.sh \
  build-gfx check-gfx check-presentation-tasks check-presentation-framework
```

The source task build and full markup/rendering check pass; the runtime consumer
has zero warnings/errors. Native builds retain upstream/MinGW warning noise.
Preparation now copies only changed port files, and generated CMake header/RC
content is written only when changed, to avoid unnecessary native recompilation.
Logs, source manifests, generated markup, DLLs and the verified PNG are archived
under `tmp/wpf8-support/validation/gfx-markup-20261013` on the local host.

System.Printing implementation, complete native text exports, generated themes,
remaining desktop assemblies, SDK/targeting/runtime packs, a shown application
window and installed WPF packaging still require work. The goal remains active.

## 41. Source-built themes, WPF 8 pen input DLL and a visible themed window

The next 2026-10-13 continuation builds PresentationUI and all six theme
assemblies with the actual upstream Microsoft.WinFX.targets. The planner now
preserves Page/ApplicationDefinition/Resource/Content items and their relative
resource URIs, copies source-built task dependencies into a private task folder,
and imports SDK targets before the WPF markup targets. The original
NoInternalTypeHelper.targets preserves friend-access rules and passes generated
resource designers into the temporary assembly. PresentationUI retains its
upstream resource-designer-before-markup dependency. Real two-pass markup
compilation generates BAML for local types, not just loose XAML resources.

PresentationUI has **95 C# inputs and 13 XAML pages**. The themes have **63 C#
inputs and eight pages**: Aero 12/1, Aero2 12/1, AeroLite 9/1, Classic 8/1,
Luna 11/3, Royale 11/1. All implementation builds pass with zero warnings/errors.
The total WPF implementation C# input scope is now **3,841**, excluding additional
markup-generated C# and resource designers. The native System.Printing project
can supply its original source-built reference project for these compilations;
the graph records that mapping and never copies that reference into runtime
lib. Its actual native/CLI printing implementation remains pending. Verified
implementation dependencies are reused, but an explicitly requested graph root
is rebuilt. System.DirectoryServices was separately built from runtime source
with zero C# errors; the planner now resolves its original package property.

`wpf-penimc.sh build check` builds WPF v8.0.0's pen/stylus input DLL from **14
C/C++ compilation inputs**, using trusted MSYS2 ReactOS ATL and widl. All **14
exports** resolve. The generated merged COM proxy exposes four interfaces;
PimcManager3 class-factory lookup succeeds, an unknown class is rejected, and
unload behavior changes correctly while the factory is held. This is not a
physical tablet/input-device test. Port provenance and limitations are recorded
in wpf-native/penimc/README.md.

The visible-window check runs on the active user's interactive desktop using a
temporary, uniquely named scheduled task. SSH starts processes in session 0,
whose parent desktop is invisible; the active RDP user is in session 2. The
runner sets the trusted CLANG64 runtime DLL search path, waits for the task's
actual terminal state/result, and removes its task in finally. It does not store
credentials or leave a recurring task. The app references the dynamic theme
assemblies so the .NET dependency manifest allows their normal WPF lookup.

That integration exposed a real graphics port bug. Windows' fault record and a
conditional native GDB breakpoint traced a null `this` in
CBaseMatrix::Transform2DBounds to ClipToSafeDeviceBounds. The original caller
invoked the typed null-safe member through a null matrix pointer; Clang removed
the intended null check under standard C++ rules. Graphics patch 0007 calls the
existing static null-safe overload, preserving the original null-as-identity
semantics. Only rtutils.cpp and its link outputs needed rebuilding. The patch
detector now forces reverse dry-run semantics instead of allowing patch's batch
heuristic to treat an unapplied patch as already applied.

The resulting source WPF window is verified with a valid native HWND,
IsWindowVisible, managed loaded/visible state, its actual default Button template
and nonempty visual tree. A routed click invokes its handler. RenderTargetBitmap
captures the actual themed control with a blue Ellipse and verifies rendered
blue pixels before writing source-window.png. The image was inspected locally.
This proves a shown, themed WPF control window with shape content; it does not
prove text layout or all input devices.

Validation on win11:

```sh
CSC_DOTNET=/clang64/lib/dotnet/dotnet.exe MSYSTEM=UCRT64 \
  M612=/home/topkek/dotnetboot/m612 OOB=System.DirectoryServices \
  bash ~/dotnetboot/repo/bootstrap/m612/libraries.sh \
  /clang64/lib/dotnet/sdk/8.0.131/Roslyn/bincore/csc.dll plan gens build report
M612=/home/topkek/dotnetboot/m612 MSYSTEM=CLANG64 \
  WPF_BUILD=/home/topkek/wpfboot/build \
  bash ~/wpfboot/driver/bootstrap/m612/wpf.sh build-themes
M612=/home/topkek/dotnetboot/m612 MSYSTEM=CLANG64 \
  WPF_BUILD=/home/topkek/wpfboot/build \
  bash ~/wpfboot/driver/bootstrap/m612/wpf-penimc.sh build check
M612=/home/topkek/dotnetboot/m612 MSYSTEM=CLANG64 \
  WPF_BUILD=/home/topkek/wpfboot/build \
  bash ~/wpfboot/driver/bootstrap/m612/wpf.sh build-gfx check-gfx check-window
```

All listed commands pass. The window consumer has zero warnings/errors. The
separate `wpf.sh check-window-text` fails at the original Classification type
initializer: MILGetClassificationTables is absent from PresentationNative.
That failure is retained as a real text gate; no classification or line-service
export is stubbed to make it pass. Full text/paragraph support, printing,
remaining assemblies, SDK/framework packs and installed packaging remain open.
Logs, manifests, generated resources/DLLs, debugger evidence and PNG are archived
under `tmp/wpf8-support/validation/themes-window-20261013` locally. The goal
remains active.

## 42. ICU-backed source classification and the next text-layout gate

The 2026-10-13 continuation ports managed Classification to an explicit
`WpfGetClassificationTables` native ABI. The original repository exposes the
legacy MIL table layout but not its table data. The source port supplies the
properties actually read by the managed classifier, using trusted MSYS2 ICU;
it does not fabricate a MILGetClassificationTables export or guessed legacy
mirroring/composition tables. Those unused fields were removed from the managed
source. Classification.md records the ABI, property mapping and limitations.

The native view has two pointers and an integer count (**24 bytes on x64**).
Every one of the **1,114,112 Unicode code-point values**, including standalone
UTF16 surrogate entries, maps through real plane/block pointers to a packed
**eight-byte CharacterAttribute**. Deduplication produces **464 classes** with
the installed ICU. Script/flag/item/bidi numbers are generated from original
UnicodeClasses.cs. Unsupported modern script IDs remain Default and complex;
RTL, combining marks, controls and contextual scripts stay outside the fast
text path. LineBreak stores ICU's property value, currently unused by managed
Classification callers. The old managed bidi engine has no isolate classes;
full isolate semantics and a future line-service adapter need additional work.

Native validation checks every code-point value for class bounds, bidi,
line-break, digit/letter/space/ideographic/extended properties and script/item
ranges against ICU. Eight concurrent callers repeatedly verify stable native
table pointers/counts. The C# peer walks every scalar-table index and every
UTF16 entry, verifies native/managed structure sizes/offsets, known script IDs,
Latin/RTL/CJK/space/digit/line-break flags, supplementary combining marks,
joiners and variation selectors, and repeats a lookup after forced GC.

PresentationCore rebuilds from the same **1,337 C# inputs** with the classifier
source patch. Its **2,289 CLR types** load (two old table structs were replaced
by one explicit native view). Real font-family/glyph metadata and matrix checks
pass again. The shown, themed shape-content window still passes with the new
native DLL. The text window now gets past classification and fails at the
original TextStore initializer's missing **LoGetEscString**. That is retained as
the next real Line Services gate; no escape-string or line-layout export is
stubbed to make the check pass.

Validation on win11:

```sh
M612=/home/topkek/dotnetboot/m612 MSYSTEM=CLANG64 \
  WPF_BUILD=/home/topkek/wpfboot/build \
  bash ~/wpfboot/driver/bootstrap/m612/wpf.sh native-win32 build-presentation-core
M612=/home/topkek/dotnetboot/m612 MSYSTEM=CLANG64 \
  WPF_BUILD=/home/topkek/wpfboot/build \
  bash ~/wpfboot/driver/bootstrap/m612/wpf.sh check-classification inventory
M612=/home/topkek/dotnetboot/m612 MSYSTEM=CLANG64 \
  WPF_BUILD=/home/topkek/wpfboot/build \
  bash ~/wpfboot/driver/bootstrap/m612/wpf.sh check-presentation-core check-window
```

These checks pass with zero warnings/errors in the managed peers. The separate
check-window-text step fails as described. The patched-source native inventory
now measures **39/139 required imports implemented**, with **100 missing**;
the new ICU ABI replaces one original import rather than claiming binary
compatibility with MIL's legacy table contract. The inventory description now
distinguishes demonstrated shape rendering from unfinished complete text/layout.
Logs, original classifier source, generated enums, native DLL/IL metadata and
the failure evidence are archived under
`tmp/wpf8-support/validation/classification-20261013` locally. Printing, complete
text/paragraph/natural-language support, remaining desktop assemblies and
WindowsDesktop packaging remain unfinished. The goal remains active.

## 43. Real native DirectWrite analysis callbacks and complete itemizer runtime

The next 2026-10-13 continuation supplies CreateTextAnalysisSink,
CreateTextAnalysisSource, GetScriptAnalysisList and GetNumberSubstitutionList.
These are real native COM implementations of the original DirectWrite source
and sink contracts. Script, number, bidi and line-break callbacks all retain
their results; none is a stub. The source borrows pinned UTF16 text, copies its
locale and owns a factory-created number-substitution object. The sink preserves
ordered half-open ranges and owns substitution references independently.
Transactional range replacement retains uncovered prefixes/suffixes and cleans
up new references on allocation failure. TextAnalysis.md records the contract.

The list nodes match original TextItemizer.h: Value first, Range[2] next, then
Next. Both script and number nodes have native **24-byte x64 size**, with Range
at offset eight and Next at sixteen. The actual SDK DWRITE_SCRIPT_ANALYSIS is
eight bytes because its native shapes enum is four bytes; the initial native
static assertion caught that distinction before code could be loaded.

The native peer sends mixed Latin/Arabic/Hebrew/Indic/supplementary text through
the real system IDWriteTextAnalyzer. An independent forwarding recorder checks
the original callback results against the exported script/number lists: four
script ranges cover the complete text, and one range retains actual number
substitution. Real bidi and line-break callbacks succeed too. Further checks
cover borrowed text/copy-locale lifetime, paragraph direction, COM identity,
empty sinks, invalid pointers/methods/positions, overflow and overlapping range
splits. A counted test substitution verifies exact AddRef/Release ownership;
production objects are actual DirectWrite substitutions.

The managed peer invokes the **complete original C++/CLI TextAnalyzer.Itemize**,
using the actual PresentationCore native delegates and ClassificationUtility.
It produces **six spans** from mixed text and verifies full length coverage,
Latin/Indic/combining/number properties, and original ItemProps native
script/substitution ownership after source/sink disposal and forced GC.
Original CanShapeTogether still succeeds on the retained properties. This is
broader evidence than the earlier extracted range-merge methods or shaped-alone
loop: the original source/factory/analysis/itemization/cleanup chain executes.

Validation on win11:

```sh
M612=/home/topkek/dotnetboot/m612 MSYSTEM=CLANG64 \
  WPF_BUILD=/home/topkek/wpfboot/build \
  bash ~/wpfboot/driver/bootstrap/m612/wpf.sh native-win32
M612=/home/topkek/dotnetboot/m612 MSYSTEM=CLANG64 \
  WPF_BUILD=/home/topkek/wpfboot/build \
  bash ~/wpfboot/driver/bootstrap/m612/wpf.sh \
  check-text-analysis-native check-text-analysis inventory
```

The native build and both runtime checks pass. The C# consumer has zero
warnings/errors. The patched-source imported-symbol inventory rises to
**43/139 implemented**, with **96 still missing**. Complete Line Services,
paragraph/natural-language layout and System.Printing implementation remain
unfinished; the interactive text window still requires LoGetEscString and the
later line-layout APIs. The remaining desktop assemblies and WindowsDesktop
SDK/framework-pack packaging are still open. Logs, native source/checks,
generated native DLL and managed consumer evidence are archived under
`tmp/wpf8-support/validation/text-analysis-20261013` locally. The goal remains
active.

## 44. Source Line Services contexts and original formatter initialization

The next 2026-10-13 continuation implements LoGetEscString, LoCreateContext,
LoDestroyContext, LoSetDoc, LoSetBreaking and LoSetTabs. These operations retain
real owned context/configuration state and do not fabricate a formatted line.
Selected native record layouts and LsErr values are generated from the original
LineServices.cs. The source control vocabulary uses module-lifetime UTF16
paragraph/line/hidden/NBSP/object markers; LineServicesContext.md explicitly
documents those choices and does not claim unknown legacy private marker values.

Contexts copy callback tables and installed handler methods, assign their client
cookie, and retain device/strategy/tab configuration. Native creation and
configuration validate inputs. Registry lookup rejects invalid or destroyed
handles without dereferencing them; a mutex protects concurrent access. Failed
device/tab updates preserve the previous configuration. An explicit diagnostic
state-copy ABI validates stored data without returning a fake line result.

The native and C# ABI checks agree on **LsContextInfo size 760**, pols offset
120, FetchPap offset 184 and final run-release flag offset 752; callback table
size 24, tab record size 12 and diagnostic state size 64. Native checks cover
context creation, copied inputs, control characters, configuration retention,
invalid pointers/devices/strategies/tab ordering, concurrent operations and
double-destroy/stale-handle rejection.

The managed peer constructs the **original TextFormatterContext**. Its original
constructor populates actual LineServicesCallbacks, obtains the native context,
sets all four device resolutions to **1440 twips per inch**, and selects its
breaking strategy. That constructor, original SetBreaking, forced-GC callback
lifetime and original repeated managed Destroy all pass. The themed shape
window continues to pass. The text window now reaches missing **LoCreateLine**
instead of failing at control strings or native context creation.

Validation on win11:

```sh
M612=/home/topkek/dotnetboot/m612 MSYSTEM=CLANG64 \
  WPF_BUILD=/home/topkek/wpfboot/build \
  bash ~/wpfboot/driver/bootstrap/m612/wpf.sh native-win32 check-lineservices-context
M612=/home/topkek/dotnetboot/m612 MSYSTEM=CLANG64 \
  WPF_BUILD=/home/topkek/wpfboot/build \
  bash ~/wpfboot/driver/bootstrap/m612/wpf.sh check-window
```

The native context build and native/managed peers pass; the C# consumer has zero
warnings/errors. The separate check-window-text still fails at LoCreateLine,
which remains absent. Imported-symbol coverage is **49/139**, with **90 missing**;
the diagnostic export is additional and not counted as an implemented upstream
import. Complete line/paragraph/natural-language layout, printing implementation,
remaining assemblies and WindowsDesktop packaging are still unfinished. Logs,
generated ABI header, native DLL and consumer evidence are archived under
`tmp/wpf8-support/validation/lineservices-context-20261013` locally. The goal
remains active.

## 45. Measured and shaped source lines, glyph enumeration and visible text

The next 2026-10-13 continuation implements LoCreateLine, LoDisposeLine,
LoDisplayLine and LoEnumLine using real original client callbacks. The generated
native ABI header now includes paragraph/line/run/height/geometry/output records.
Line creation fetches original text runs, paragraph and line properties, obtains
actual font metrics and nominal character widths or invokes original shaping and
glyph-position callbacks. Text, width, cluster, glyph/property/offset arrays and
context ownership are retained for later drawing/enumeration. No constant or
fabricated line metrics are returned.

Paragraph/line separators are consumed with real end positions; nominal trailing
spaces affect reported trailing widths. Construction rejects context reentry,
propagates client aborts and leaves output handles empty on failure. Allocation
failures are contained at the native ABI. Native tests supply known callback
data and verify exact ascent/descent/height, widths/trailing spaces, paragraph
end, retained draw arguments, callback abort and line disposal.

The existing visible text window now passes through the original WPF formatter
and drawing callbacks. It renders **Source WPF text** in a real themed Button.
The test counts dark glyph pixels in the interior content area, excluding the
border. A second window renders **कि** with Nirmala UI at 24 DIP, exercising
original Indic itemization/shaping and reordered vowel rendering. Both PNGs
were inspected locally; their glyph-pixel assertions pass. The shape window
continues to pass.

The public TextFormatter peer formats the same two-character Indic string and
obtains **length 3** (including paragraph end), **width 26.953 DIP**, **height
31.923 DIP** and one indexed glyph run. Original LoEnumLine callbacks return
the retained shaped run, complete text coverage and nonempty glyphs. Repeated
enumeration after forced GC has the same glyph count. The actual WPF/native
formatting/shaping/drawing/enumeration/lifetime path executes.

The interactive runner now suppresses PowerShell progress output under SSH and
reports success after task cleanup. A transient scheduled-task cleanup cmdlet
had attempted to read an unavailable console buffer; reruns pass and an explicit
query confirms zero remaining WPF test tasks.

Validation on win11:

```sh
M612=/home/topkek/dotnetboot/m612 MSYSTEM=CLANG64 \
  WPF_BUILD=/home/topkek/wpfboot/build \
  bash ~/wpfboot/driver/bootstrap/m612/wpf.sh \
  native-win32 check-lineservices-line-native check-lineservices-context check-text-line
M612=/home/topkek/dotnetboot/m612 MSYSTEM=CLANG64 \
  WPF_BUILD=/home/topkek/wpfboot/build \
  bash ~/wpfboot/driver/bootstrap/m612/wpf.sh check-window
M612=/home/topkek/dotnetboot/m612 MSYSTEM=CLANG64 \
  WPF_BUILD=/home/topkek/wpfboot/build \
  bash ~/wpfboot/driver/bootstrap/m612/wpf.sh check-window-text check-window-indic inventory
```

Native/managed line/context checks, public glyph enumeration and both visible
text checks pass. C# consumers have zero warnings/errors. Imported native
coverage is **53/139**, with **86 missing**. This is not full line-service
compatibility: wrapping overflow, optimal/min-max formatting, prior break
records, inline and bidi reversal objects, tabs, justification and decorations
remain unsupported and return explicit errors rather than fake success. Shaped
trailing spaces, geometric hit testing and complete wrapping/bidi/paragraph
behavior still require work. Printing, remaining desktop assemblies and
WindowsDesktop packaging are unfinished. Logs, generated ABI header, native
DLL, public consumer and Latin/Indic PNGs are archived under
`tmp/wpf8-support/validation/lineservices-line-20261013` locally. The goal remains
active.

## 46. Unicode wrapping and owned native continuation records

The next 2026-10-13 continuation implements source wrapping using ICU Unicode
line opportunities and emergency grapheme boundaries, filtered by the original
shaping cluster map and callback-derived advances. Overflow wrapping keeps an
oversized word; emergency wrapping guarantees a complete grapheme even at a
one-DIP column. Client run boundaries do not create artificial word breaks:
the remaining paragraph is fetched before choosing a break. A truncated shaped
run is reshaped through original callbacks. Trailing spaces are tracked across
runs and excluded from visible column fitting. Reading the paragraph suffix for
each line repeats work on long paragraphs; streaming remains an optimization.

LoAcquireBreakRecord, LoCloneBreakRecord and LoDisposeBreakRecord own real next
position, paragraph start and context identity. Clones survive line/original
record disposal. Invalid handles, double release, wrong continuation positions
and foreign contexts fail with empty outputs. Native tests cover those paths
and exact widths, run-split words, combining graphemes, overflow/emergency
policies and trailing-space paragraph termination. Public WPF single-line
GetTextLineBreak normally requests no native record, so these ownership claims
come from direct native calls, not an assumed managed invocation.

Public TextFormatter formats successive Latin and Indic lines with exact total
UTF16/paragraph-end coverage, nonempty content glyph runs and cluster progress.
Very narrow Indic columns retain the whole shaping cluster. Real visible themed
Buttons now contain wrapped TextBlocks: Latin has 106.400-DIP content height;
Indic has 53.200 DIP. Both have multiple dark glyph bands and their PNGs were
inspected locally. The shape window regression and native/managed context
checks pass. The ICU import library is named icuin in MSYS2 CLANG64; using the
Unix icui18n name had failed linking and is corrected.

Validation on win11:

```sh
M612=/home/topkek/dotnetboot/m612 MSYSTEM=CLANG64 \
  WPF_BUILD=/home/topkek/wpfboot/build \
  bash ~/wpfboot/driver/bootstrap/m612/wpf.sh \
  native-win32 check-lineservices-line-native check-text-line
M612=/home/topkek/dotnetboot/m612 MSYSTEM=CLANG64 \
  WPF_BUILD=/home/topkek/wpfboot/build \
  bash ~/wpfboot/driver/bootstrap/m612/wpf.sh \
  check-window check-window-wrap inventory
M612=/home/topkek/dotnetboot/m612 MSYSTEM=CLANG64 \
  WPF_BUILD=/home/topkek/wpfboot/build \
  bash ~/wpfboot/driver/bootstrap/m612/wpf.sh check-lineservices-context
```

All listed commands pass; C# consumers report zero warnings/errors. Native
import coverage is **56/139**, with **83 missing**. Full RTL/bidi and paragraph
layout, optimal/min-max formatting, hit testing, inline objects, tabs,
justification/decorations, System.Printing implementation and WindowsDesktop
packaging remain unfinished. Evidence is archived locally under
`tmp/wpf8-support/validation/lineservices-wrap-20261013`. The goal remains active.

## 47. Retained text geometry, caret hits and selection bounds

The same continuation adds LoQueryLineCpPpoint and LoQueryLinePointPcp.
Queries derive nominal/grapheme or shaped cluster cells from actual retained
character widths, cluster maps and glyph advances. They return the exact cell
position, length and width plus original client run identity and one-subline
geometry. Point queries clamp outside text to the first/last positive-width
cell; CP queries validate their range. A retained paragraph separator has its
own zero-width position and actual client run identity. Invalid/disposed lines
and insufficient output depth fail with cleared result outputs.

The ABI generator now supports nested records and derives LsQSubInfo and
LsTextCell from original LineServices.cs. Native x64 checks verify their 120-
and 40-byte layouts and exact Latin cell positions/advances/run identities.
Public WPF Indic tests run after forced GC: leading/trailing distances match
original shaped width, point hits and forward/backward navigation preserve
the two-code-unit cluster, and selection bounds match measured width.

Validation on win11:

```sh
M612=/home/topkek/dotnetboot/m612 MSYSTEM=CLANG64 \
  WPF_BUILD=/home/topkek/wpfboot/build \
  bash ~/wpfboot/driver/bootstrap/m612/wpf.sh \
  native-win32 check-lineservices-line-native check-text-line inventory
```

All steps pass; the C# consumer has zero warnings/errors. Native imported
coverage is **58/139**, with **81 missing**. Queries explicitly reject RTL/
nonmonotonic cluster maps and graphemes crossing client runs because their
caret ownership is not yet implemented. This establishes real caret/selection
behavior for the validated nominal/shaped paths, not complete text editing or
bidi support. Optimal/min-max layout, inline objects, tabs, justification,
decorations, paragraph/NLS exports, printing and WindowsDesktop packaging
remain unfinished. Evidence is archived under
`tmp/wpf8-support/validation/lineservices-caret-20261013` locally. The goal remains
active.

## 48. Visible WPF text editing and native character input

The next 2026-10-13 continuation adds check-window-edit. A source WPF TextBox
uses the actual default theme and renders mixed Indic/Latin text in a visible
window. The test replaces a selected word, verifies TextChanged, undo/redo,
keyboard focus, two wrapped lines, a caret rectangle and its corresponding
point hit. A real native WM_CHAR sent to the window HWND reaches WPF text
composition and appends the character; undo removes that input. This exercises
the native window-to-managed editor path without injecting global keyboard
input. Selecting the two-code-unit Indic cluster returns the expected text.

The initial screenshot was taken before queued selection invalidation completed
and still showed the earlier word selection. Capture now occurs on the next
normal dispatcher timer tick. The final check verifies 528 highlight pixels
inside the selected Indic cluster's caret bounds, plus nonempty glyph pixels.
The final image was inspected locally. The ordinary shape/button window also
passes. No runtime code change was needed for these validated editor paths.

Validation on win11:

```sh
M612=/home/topkek/dotnetboot/m612 MSYSTEM=CLANG64 \
  WPF_BUILD=/home/topkek/wpfboot/build \
  bash ~/wpfboot/driver/bootstrap/m612/wpf.sh check-window check-window-edit
```

The command passes and the C# consumer has zero warnings/errors. Logs and the
rendered selection PNG are archived locally under
`tmp/wpf8-support/validation/textbox-20261013`. This does not establish IME,
clipboard, accessibility, full bidi, rich editing or full desktop packaging.
System.Printing and remaining native/desktop work stay open; the goal remains
active.

## 49. Original printing project gate and frontend compatibility corrections

The same continuation adds printing-gate, driven by the original
System.Printing.vcxproj CLCompile items. There are **49 project compilation
inputs** and **66 C++ files in the original tree**. A private combined-source
copy preserves the original implementation/header bodies, dependency identities
and #using directives. Preparation conditionally omits a temporary MSVC static
analysis header under MinGW and disables the unavailable banned-API analysis.
It records input paths, command and scope in printing-gate/inputs.json. This is
a compiler diagnostic gate, not proof of separate-unit linking or a printing
runtime DLL. Source-built WPF dependency DLLs take precedence over empty core
facades; no printing reference DLL is copied into runtime lib.

The first gate failure was the trusted WINSPOOL.DRV import module. The driver
now accepts both DLL and DRV modules from named x64 COFF archives. MinGW's
original winspool.h functions have no dllimport annotation; they now bind when
the canonical declaration is in a system header and an explicit trusted import
archive provides the function. User declarations without that provenance and
unbound SDK functions still fail. Real read-only EnumPrintersW through the CLI
assembly exactly matches the separately compiled native SDK oracle.

The next gate failure was eager validation of every #using as_friend, even for
assemblies whose public metadata alone was used. The reader now follows the
[documented C++/CLI friend semantics](https://learn.microsoft.com/en-us/cpp/dotnet/friend-assemblies-cpp):
as_friend exposes metadata; CLR internal access depends on the actual
InternalsVisibleTo identity. Correctly signed/name-matched access passes.
Public-only usage without a grant passes. Accessing an internal member with a
wrong name or key compiles and produces MethodAccessException at runtime,
verified in separate load contexts. Private/protected members remain excluded
or protected; no-friend internal-type imports fail. Strong callers of unsigned
friend components with InternalsVisibleTo remain rejected.

The next source failure came from ordinary printing parameters named delegate.
Adaptation now recognizes delegate as contextual and preserves parameter,
field, local, expression and ordinary function identifiers alongside real
delegate declarations. Focused construction/invocation tests execute under
the CLR, including original WPF-style MulticastDelegate parameters. Attribute
failure messages now retain source path, line and attribute name.

Validation on win11:

```sh
/clang64/bin/cmake.exe --build ~/dotnetboot/m612/build/cppcli --target cli_ast -j2
CPPCLI_SDK=/clang64/lib/dotnet CPPCLI_ILASM=/clang64/bin/ilasm-dotnet.exe \
  M612=/home/topkek/dotnetboot/m612 MSYSTEM=CLANG64 \
  bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh build test-references test-native-spooler
CPPCLI_SDK=/clang64/lib/dotnet CPPCLI_ILASM=/clang64/bin/ilasm-dotnet.exe \
  M612=/home/topkek/dotnetboot/m612 MSYSTEM=CLANG64 \
  bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh test-contextual-delegate
M612=/home/topkek/dotnetboot/m612 MSYSTEM=CLANG64 \
  WPF_BUILD=/home/topkek/wpfboot/build \
  WPF_CPPCLI_BUILD=/home/topkek/dotnetboot/m612/build/cppcli \
  bash ~/wpfboot/driver/bootstrap/m612/wpf.sh build-directwrite check-directwrite-assembly
M612=/home/topkek/dotnetboot/m612 MSYSTEM=CLANG64 \
  WPF_BUILD=/home/topkek/wpfboot/build \
  bash ~/wpfboot/driver/bootstrap/m612/wpf.sh printing-gate
```

Rebuilds and focused tests pass. The full DirectWrite bridge regression still
loads 85 types, prepares 283 eligible bodies and resolves 81 real system font
families with owned font/face/file and managed COM-loader checks. The ordinary
Windows API binding/registry/process test also passes after the import change.
The final printing-gate command **fails**, now specifically at
CPP/inc/GDIExporter/nativemethods.h:737: MarshalAs currently supports one
positional argument, while this original declaration also needs named options.
No System.Printing runtime DLL is produced. SHLWAPI/KERNEL32 duplicate import
symbols also require explicit binding resolution before adding both archives;
unavailable used functions still fail rather than receiving arbitrary bindings.
Other printing/compiler/native/desktop requirements remain open. Evidence is
archived locally under `tmp/wpf8-support/validation/printing-gate-20261013`.
Installed compiler packages are unchanged; this is the development frontend.
The goal remains active.

## 50. Sized native arrays and further printing frontend compatibility

The next 2026-10-13 continuation implements MarshalAs LPArray parameter
metadata with SizeParamIndex, SizeConst and ArraySubType. The adapter separates
one positional argument from supported named options and preserves their
constant expressions without narrowing to Int32. The emitter validates array
parameters, zero-based parameter indices, integral size parameters, compressed
integer count bounds, supported element types and duplicate options. Original
source ilasm's nativeType grammar supplies the array descriptor: explicit
`[count + parameter]` preserves the size-index-specified flag, including index
zero. Fixed counts, inferred element types and explicit subtypes are retained.
String parameter marshal names (BSTR/LPStr/LPWStr/LPTStr) are also recognized.
Field/return marshalling, other options and general DllImport declarations are
not implemented by this change.

The runtime test exposes a real CLI COM interface to a native MinGW caller.
CLR constructs arrays with the requested parameter/fixed/combined lengths,
including zero-sized input and zero parameter contribution to a fixed count.
The managed implementation checks the actual Length. In/out mutations copy
back only the requested prefix; untouched tail sentinels prove bounds.
Reflection verifies descriptors and IN/OUT flags. Repeated calls after forced
GC pass. Nine malformed declarations fail: non-array targets, invalid indices,
nonintegral size parameters, negative/nonconstant/overflowing counts,
overflowing indices, duplicate options and array options on scalar marshalling.

The original printing gate gets past its LPArray failure and exposes an
explicit `(void)` static constructor. The adapter now accepts that zero-argument
spelling for inline and out-of-line declarations/definitions. Existing CLR
ordering/concurrency/once/failure-poisoning tests pass with both `(void)` and
`()` forms. Ordinary parameters and initializer lists remain rejected.

Printing then reaches ordinary Clang semantic checking. Its copied SDK headers
use legacy SAL annotations; the gate maps them to trusted MinGW SAL2 spellings
and records the aliases. Callback/typefix markers are static-analysis-only.
Two missing Vista connection flag values are taken from Microsoft's
[winspool.h source](https://raw.githubusercontent.com/microsoft/win32metadata/main/generation/WinSDK/RecompiledIdlHeaders/um/winspool.h)
and supplied only when the trusted SDK lacks them. Original CLR metadata types
used by XML, packaging, drawing, tracing and interoperability are selected from
the already source-built dependencies; namespace/type placeholders are not
fabricated. A failed optional NameValueCollection import was removed because
the printing sources do not use that type.

A metadata ordering defect placed SafeHandle's protected IntPtr value field
before IntPtr was defined when its derived class was visited early. Definition
ordering now visits selected instance value-field dependencies as well as
bases/interfaces. A real CLI-derived SafeHandle reads its protected handle,
uses the inherited invalid/closed state and disposes successfully. Printing's
`property virtual` spelling also needed its modifier removed from the surrogate
property type; modifiers now work on either side of property. Virtual dispatch
and static access tests pass. Static auto-properties now generate private
static backing fields and real static CLR accessors; default zero, mutation and
cross-assembly property consumption pass.

Validation on win11:

```sh
/clang64/bin/cmake.exe --build ~/dotnetboot/m612/build/cppcli --target cli_ast -j2
CPPCLI_SDK=/clang64/lib/dotnet CPPCLI_ILASM=/clang64/bin/ilasm-dotnet.exe \
  M612=/home/topkek/dotnetboot/m612 MSYSTEM=CLANG64 \
  bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh test-array-marshal test-static-fields
CPPCLI_SDK=/clang64/lib/dotnet CPPCLI_ILASM=/clang64/bin/ilasm-dotnet.exe \
  M612=/home/topkek/dotnetboot/m612 MSYSTEM=CLANG64 \
  bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh test-value-field-order
CPPCLI_SDK=/clang64/lib/dotnet CPPCLI_ILASM=/clang64/bin/ilasm-dotnet.exe \
  M612=/home/topkek/dotnetboot/m612 MSYSTEM=CLANG64 \
  bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh test-virtuals test-static-properties test-com
CPPCLI_SDK=/clang64/lib/dotnet CPPCLI_ILASM=/clang64/bin/ilasm-dotnet.exe \
  M612=/home/topkek/dotnetboot/m612 MSYSTEM=CLANG64 \
  bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh test-static-properties
M612=/home/topkek/dotnetboot/m612 MSYSTEM=CLANG64 \
  WPF_BUILD=/home/topkek/wpfboot/build \
  bash ~/wpfboot/driver/bootstrap/m612/wpf.sh printing-gate
```

Rebuilds and focused tests pass, including existing COM/HRESULT regressions.
C# and second-CLI-assembly callers verify the static auto-property's default,
mutation and accessor identity. `wpf.sh build-directwrite
check-directwrite-assembly` also passes with the updated frontend: 85 types,
283 prepared bodies and 81 system font families, plus original managed COM
loaders and real font/face/file ownership. It runs with the same M612/MSYSTEM/
WPF_BUILD/WPF_CPPCLI_BUILD settings as §49.
The final printing-gate command **fails** in ordinary Sema. Remaining observed
failures include System.Printing.Interop namespace availability at the first
unit's using directive, source `!PrinterDefaults(void)` finalizers, literal
String static initializers, further imported types/attributes and layout
options. The previous virtual-property/auto-property, SAL, SDK flag and IntPtr
ordering errors have been removed from the gate output. No System.Printing
runtime DLL or finished WPF package is produced. Native coverage remains the
previous 58/139; complete layout/printing/desktop requirements remain open.
Evidence is archived locally under
`tmp/wpf8-support/validation/printing-arrays-20261013`. The development frontend
is changed; installed compiler packages are unchanged. The goal remains active.

## 51. Source finalizers and literal String initialization for printing

The next 2026-10-13 continuation lowers source `!Type()` finalizers, including
inline/out-of-line definitions and explicit `(void)` parameter lists. An owned
source helper retains the actual body and source access rules. Explicit bang calls invoke only that
helper; GC uses a real protected CLR Finalize override and the generated
Dispose(false) chain. Source destructor disposal remains deterministic,
implements IDisposable, chains base cleanup and suppresses finalization after
successful disposal. A finally region preserves base cleanup when the source
finalizer helper throws. Types without a source destructor can still have a
finalizer and release native resources during GC. Finalizer methods require a
source ref class and reject parameters, virtual/static forms, missing bodies
and explicit Dispose/Finalize conflicts.

This follows the distinction between explicit finalizer calls and GC chaining
in Microsoft's [C++/CLI destructor/finalizer documentation](https://learn.microsoft.com/en-us/cpp/dotnet/how-to-define-and-consume-classes-and-structs-cpp-cli).
The runtime fixture owns real native allocations and counts live/closed
resources and release order. Deterministic IDisposable and managed delete,
explicit own-only and qualified protected-base calls, forced collection, base chaining, suppression and
idempotent source bodies pass. A finalizer-only object also releases its
allocation. A throwing finalizer is invoked through Dispose(false) under
reflection so its exception can be observed safely; base native release still
runs, and that test object is then explicitly suppressed. Protected virtual
Finalize metadata resolves to System.Object's base slot. Generic-qualified
owners and arbitrary imported-base finalization patterns remain unverified.

The old destructor test still expected deletion of a null native scalar pointer
to fail, despite the previously implemented native allocation/deletion support.
Its replacement rejects incomplete native storage. Original destructor tests
still verify derived/base order, imported Stream closure, unrelated Dispose
method handling, exception-safe cleanup, nested types and inherited-finalizer
suppression.

Static String-handle fields initialized by literal tokens now use real CLR
literal conversion in their generated initializer helpers. Both the printing
source's ASCII RAW value and adjacent wide Unicode literals are preserved.
The existing ordered/once/concurrent/failure tests now also read the ASCII and
Unicode strings after forced GC. This does not implement general contextual
literal conversion in arbitrary expressions or return statements.

The printing gate adds actual source-built PrintTicketConverter/BaseDevModeType,
CER and XPS RCW metadata, plus StringBuilder, Win32Exception, SafeFileHandle,
SafeWaitHandle and COM IStream imports. The earlier System.Printing.Interop,
RCW namespace and finalizer/string-initializer failures disappear. No namespace
or runtime type placeholder is supplied.

Validation on win11:

```sh
/clang64/bin/cmake.exe --build ~/dotnetboot/m612/build/cppcli --target cli_ast -j2
CPPCLI_SDK=/clang64/lib/dotnet CPPCLI_ILASM=/clang64/bin/ilasm-dotnet.exe \
  M612=/home/topkek/dotnetboot/m612 MSYSTEM=CLANG64 \
  bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh test-finalizers
CPPCLI_SDK=/clang64/lib/dotnet CPPCLI_ILASM=/clang64/bin/ilasm-dotnet.exe \
  M612=/home/topkek/dotnetboot/m612 MSYSTEM=CLANG64 \
  bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh test-destructors
CPPCLI_SDK=/clang64/lib/dotnet CPPCLI_ILASM=/clang64/bin/ilasm-dotnet.exe \
  M612=/home/topkek/dotnetboot/m612 MSYSTEM=CLANG64 \
  bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh test-static-fields
M612=/home/topkek/dotnetboot/m612 MSYSTEM=CLANG64 \
  WPF_BUILD=/home/topkek/wpfboot/build \
  WPF_CPPCLI_BUILD=/home/topkek/dotnetboot/m612/build/cppcli \
  bash ~/wpfboot/driver/bootstrap/m612/wpf.sh build-directwrite check-directwrite-assembly
M612=/home/topkek/dotnetboot/m612 MSYSTEM=CLANG64 \
  WPF_BUILD=/home/topkek/wpfboot/build \
  bash ~/wpfboot/driver/bootstrap/m612/wpf.sh printing-gate
```

Focused runtime/rejection tests pass, and DirectWrite still loads 85 types,
prepares 283 bodies and resolves 81 real font families with original owned
font/face/file and managed COM-loader checks. The complete printing-gate
command **fails** in ordinary Sema, beginning with missing kPropertyTypeString,
kPropertyTypeInt32 and kPropertyTypeTime native definitions at
InteropAttributeValueDictionary.hpp:29-31. Further SDK/declaration/import and
attribute/body/link/runtime work remains. No System.Printing runtime DLL or
finished WPF package is produced. Evidence is archived under
`tmp/wpf8-support/validation/printing-finalizers-20261013` locally. Native
coverage remains 58/139, installed compiler packages are unchanged, and the goal
remains active.

## 52. Native print properties, typed static arrays and accessor visibility

The next 2026-10-13 continuation supplies the missing EPrintPropertyType,
PrintPropertyValue, PrintNamedProperty and PrintPropertiesCollection SDK
declarations, using Microsoft's [winspool.h source ABI](https://raw.githubusercontent.com/microsoft/win32metadata/main/generation/WinSDK/RecompiledIdlHeaders/um/winspool.h).
A compiler probe determines whether the trusted SDK already has the records;
the compatibility header is included only when absent. Existing MinGW XPS job
enumerations are reused. The native check verifies x64 sizes/alignment/offsets
(property value 24 bytes, union offset 8, named property 32, collection 16),
reads a real typed blob through the records and resolves the actual
GetJobNamedPropertyValue export from Winspool. Its read-only invalid-handle
call returns ERROR_INVALID_HANDLE; no printer or job is opened or created.
This establishes declarations and a native failure path, not printing success.

Printing then reaches static managed array aggregate initializers. These now
lower to a typed compound-literal helper for Clang checking and AST collection;
the emitter creates a real CLR array and stores elements in order. No native
initializer storage executes. Explicit static rank-one array-handle fields
support scalar/string/type-token/object/null/empty elements. Tests verify
side-effect order, once/concurrent initialization, shared mutation, CLR type
identity and retention after forced GC. Adjacent ordinary/wide string tokens
are combined before their real CLR literal conversion; a focused rerun passes. Invalid object-to-scalar elements fail
Clang checking. Explicit numeric conversions remain Clang's conversions.
Other array initializer forms and covariance remain unfinished.

The initial std::initializer_list approach collided with already flattened
standard-library declarations in the printing input and was replaced by the
compound-literal marker. The final metadata header adds no standard-library
include. Empty array initialization also executes correctly.

Printing's properties mix public getters and internal setters, including
out-of-line definitions. Access labels within the property body now select
real public/assembly/family/private CLR method flags. Virtual slots are retained,
same-assembly setter calls work, and the enclosing class's visibility is
restored after emitting accessors. C# reflection/runtime checks cover inline
and out-of-line members. A second CLI assembly importing the public type cannot
write its internal setter. Existing property/finally and virtual/interface
checks pass. The gate additionally selects the actual source-built serializer,
document paginator/page/sequence, Visual and attribute/serialization types.

Validation on win11:

```sh
CPPCLI_SDK=/clang64/lib/dotnet CPPCLI_ILASM=/clang64/bin/ilasm-dotnet.exe \
  M612=/home/topkek/dotnetboot/m612 MSYSTEM=CLANG64 \
  bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh build
CPPCLI_SDK=/clang64/lib/dotnet CPPCLI_ILASM=/clang64/bin/ilasm-dotnet.exe \
  M612=/home/topkek/dotnetboot/m612 MSYSTEM=CLANG64 \
  bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh \
  test-array-initializers test-accessor-visibility test-properties test-virtuals
CPPCLI_SDK=/clang64/lib/dotnet CPPCLI_ILASM=/clang64/bin/ilasm-dotnet.exe \
  M612=/home/topkek/dotnetboot/m612 MSYSTEM=CLANG64 \
  bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh test-accessor-visibility
M612=/home/topkek/dotnetboot/m612 MSYSTEM=CLANG64 \
  WPF_BUILD=/home/topkek/wpfboot/build \
  WPF_CPPCLI_BUILD=/home/topkek/dotnetboot/m612/build/cppcli \
  bash ~/wpfboot/driver/bootstrap/m612/wpf.sh build-directwrite check-directwrite-assembly
M612=/home/topkek/dotnetboot/m612 MSYSTEM=CLANG64 \
  WPF_BUILD=/home/topkek/wpfboot/build \
  bash ~/wpfboot/driver/bootstrap/m612/wpf.sh printing-gate
```

Builds and focused runtime/rejection checks pass. Existing virtual fixture
warnings remain; C# consumers report zero warnings/errors. DirectWrite still
loads 85 types, prepares 283 bodies and resolves 81 system font families with
original owned font/face/file and managed COM-loader behavior. The final
printing-gate command **fails** in Sema, beginning with unsupported event
syntax in XPSDocumentWriter.hpp:559 and :741. The native SDK check passes first,
and the previous SDK-record/static-array/accessor failures are absent. Further
event/attribute/import/body/link/runtime work and all remaining native layout/
WindowsDesktop packaging requirements are unfinished. No System.Printing
runtime DLL or completed WPF package is produced. Evidence is archived under
`tmp/wpf8-support/validation/printing-sdk-arrays-20261013` locally. Native
coverage remains 58/139 and the goal remains active.

## 53. Managed events and delegate multicast for original printing sources

The 2026-10-13 continuation implements typed delegate `+=`/`-=` through actual
CLR Delegate.Combine/Remove. The private Clang parser asks Sema to check the
assignment's conversion, lvalue and constness before preserving the CLI
compound operation. The emitter retains captured receiver/index storage and
casts the returned delegate to the original CLR delegate identity. Runtime
checks cover invocation order, duplicate and multicast-subsequence removal,
null/absent handlers, immutable original lists, instance/static/array storage,
assignment expression values, single receiver/RHS evaluation, byref arguments,
imported Action, handler exceptions and GC retention. Four invalid inputs reject.

Instance events now emit genuine CLR event/accessor metadata. Custom event
bodies retain their source add/remove/raise methods; automatic void events
generate private delegate storage, add/remove bodies and private raising that
snapshots the list and skips an empty list. Parser dispatch resolves the real
accessor through Clang member lookup, so protected/private access and virtual
overrides remain checked. Metadata imports read actual event accessor identities
from source-built assemblies. The facade itself is never emitted as a field;
reading, assigning or taking its address rejects rather than exposing storage.
This follows the event/accessor model documented in Microsoft's
[C++/CLI event examples](https://learn.microsoft.com/en-us/cpp/dotnet/how-to-use-events-in-cpp-cli?view=msvc-170).

C# and a second CLI assembly subscribe/unsubscribe and run actual automatic
and custom events. Checks also cover internal accessor flags and imported
internal rejection, protected raising, virtual dispatch, typed custom return
values, byref/imported delegate signatures, out-of-line accessors, reentrant
subscription changes during dispatch, empty lists, metadata and GC. Eight
invalid event inputs reject. Static events, automatic interface/non-void events,
generic/ambiguous/aliased signature discovery, extended qualified accessor paths
and concurrent automatic subscription remain unsupported or unverified.

The source printing gate adds real serialization event types, graphics types,
PrintTicketManager, page-range types and Collection metadata from already
source-built dependencies. It now passes the previous XPSDocumentWriter event
syntax failures. That progress exposed an existing contextual-property bug:
the parameter named `property` in PrintServer::GetDataFromServer was treated as
a property declaration. Adaptation now requires a member declaration boundary;
ordinary parameter/field names `property` and `event` survive compilation.

Validation on win11 uses the source SDK and ilasm with the usual M612/CLANG64
environment:

```bash
bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh build test-events test-multicast
bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh build test-events test-multicast test-properties test-delegates test-enums
bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh build test-events test-properties test-static-properties test-accessor-visibility
bash ~/wpfboot/driver/bootstrap/m612/wpf.sh build-directwrite check-directwrite-assembly printing-gate
```

The compiler builds and focused runtime/rejection checks pass; C# consumers
report zero warnings/errors. The WPF command rebuilds and validates the complete
DirectWrite bridge: 85 types, 283 prepared bodies, 81 font families and the
original factory/COM loader/font/face/file ownership checks. It then **fails**
at printing-gate: the first source error is inherited ResourceManager.GetString
literal conversion in PrintQueue.hpp:1627. Further diagnostics include missing
ICloneable metadata, `protected private` access syntax and static CLI conversion
operators in PrintSystemAttributeValue.hpp. No System.Printing implementation
assembly is produced. The gate still covers 49 original project inputs/66 tree
C++ files as a combined diagnostic input, not separate-unit link/runtime proof.

Evidence is archived under
`tmp/wpf8-support/validation/printing-events-20261013` locally. Native import
coverage remains 58/139; full native layout, printing runtime, WindowsDesktop
packs and an installable WPF package remain unfinished. Installed compiler
packages are unchanged; these changes are in the development frontend.

## 54. Printing literals, conversion metadata and combined access

The next 2026-10-13 continuation moves the original printing gate past its
inherited GetString call, static conversion declarations and combined access
labels. The private Clang parser inspects actual managed method/overload
declarations to adapt literal arguments in String/Object slots, including
inherited instance methods. Clang still checks overloads, arity and other
argument conversions. Actual Hashtable.Add/ContainsKey calls now store/look up
managed string keys, matching the original printing delegate table shape.

The native comparison check exposed previously invalid IL for direct native
literal arguments. Array-to-pointer decay now emits an address of null-terminated
RVA character data. Managed literal markers and String constructors explicitly
emit CLR strings. Independently compiled MinGW functions verify narrow/wide
pointer ABI, raw non-UTF-8/UTF-8 bytes, adjacent literals, embedded nulls, wide
alignment and retention across GC. This does not establish collectible-assembly
unload behavior or every mixed native/managed overload policy.

Source static conversion declarations and matching out-of-line definitions now
emit real specialname op_Implicit/op_Explicit methods. C# callers exercise
reference/value inputs/results, multiple result types, null behavior and GC.
A printing-style ref-class tracking-reference operand has a CLR class-byref
signature, and reflection invokes its actual field-reading body. Invalid
arity, void results and conversions unrelated to their containing type reject.
This is conversion **declaration/body/metadata support**: general source CLI
implicit assignments, argument conversions and cast selection still remain
unfinished. No synthetic conversion body replaces original implementations.

Both orders of public/protected, protected/private and public/private labels
map to famorassem, famandassem and assembly flags. Ordinary methods, instance/
static fields and auto/custom property accessors preserve their CLR access.
Source same-assembly calls and local derived calls run; a second CLI assembly's
derived call reaches a family-or-assembly member, while unrelated foreign calls
and foreign family-and-assembly access reject. Existing event/property access
checks still pass. Printing's __finally after managed try/catch reuses the
existing CLR finally implementation; focused return and rethrow paths verify
cleanup. Native __try/SEH remains outside this proof.

Additional serialization, clone, asynchronous-result, event and dictionary
types are imported from already source-built dependencies. Importing XPathItem
preserves XPathNavigator's real base chain when ICloneable is present. The full
printing argument vector then exceeded Windows/MSYS launch limits; the driver
and metadata reader now accept UTF-8 JSON argument vectors. The gate records
both expanded arguments and the actual short invocation. A real executable
validates Unicode/spaced paths, macro quotes/metacharacters and nested metadata
transport; non-string argument vectors reject.

Focused commands on win11, with the usual source SDK/ilasm/M612/CLANG64 and
WPF_BUILD/WPF_CPPCLI_BUILD environment:

```bash
bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh build test-semantic-literals test-conversions test-combined-access
bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh build test-semantic-literals test-exceptions
bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh test-semantic-literals test-argument-files
bash ~/wpfboot/driver/bootstrap/m612/wpf.sh build-directwrite check-directwrite-assembly printing-gate
```

The listed cppcli commands pass with zero C# warnings/errors. Earlier
focused core, native-string, tracking-reference, event and property checks also
pass. The final WPF command rebuilds and validates DirectWrite: 85 types, 283
prepared bodies, 81 font families and the original factory, managed COM loaders
and owned font/face/file paths. It then **fails** in printing Sema, now at
PrintQueue.cpp:995's incomplete PrintSystemException catch type. Further errors
show missing implicit primitive/enum boxing into Object parameters, registry/
exception metadata and definition/base-chain ordering. Previous literal,
conversion-declaration, combined-label and __finally failures are absent.

Evidence is archived locally under
`tmp/wpf8-support/validation/printing-literals-conversions-20261013`.
The gate still covers 49 project inputs/66 original tree C++ files; no complete
System.Printing implementation DLL, separate-unit link/runtime proof or WPF
package is produced. Native coverage remains 58/139; full layout, printing
runtime and WindowsDesktop SDK/packs/installation remain open. Installed
compiler packages are unchanged.

## 55. Implicit Object boxing, enum calls and local arrays for printing

The next 2026-10-13 continuation implements primitive/enum boxing in managed
Object method slots without competing typed overloads, Object field/array
assignments and Object property setters. The private parser builds a typed
boxing marker before normal Clang call/assignment checking; the IL emitter
emits actual CLR box instructions. Access checks still reject a private setter,
and an unmanaged pointer cannot become Object through this path. Broader
initializer/return/constructor/interface boxing and general boxing overload
resolution remain unfinished.

Runtime checks verify Int32, Boolean, UInt64, Single, Double, Char and enum CLR
type identities/payloads, zero, copied box values, field/array/property storage,
actual Hashtable calls, field-assignment expression results, receiver/operand
evaluation once, typed-overload preservation and GC. Two invalid cases reject.
CLI enum ToString/GetHashCode/Equals/GetType member lookup now boxes the receiver
and resolves Object's methods through Clang; enum virtual dispatch is exercised
against C# ToString/GetHashCode/Equals results. Native enums are untouched, and
Enum-specific methods/format overloads still need typed discovery.

Explicit local array<T>^ brace declarations now use the existing typed array
initializer backend. Checks cover the printing shape `{downLevelPropertyName}`,
adjacent String literals, gcnew String elements followed by more literals,
null elements, scalar element order/once and empty arrays. The static String
array path also preserves native literals inside constructors instead of
wrapping their arguments as String handles. Existing static-array and value/
property/enum regressions pass. Alias/covariant/generic element discovery and
all managed array initializer forms remain incomplete.

The previously incomplete PrintSystemException/PrintCommitAttributesException
types are real C# implementation types in ReachFramework; the gate now imports
those definitions and their base chain. Registry, reflection, XPS serialization,
packaging-policy, nullable and related metadata come from already source-built
dependencies. The private SDK preparation removes MinGW's DocumentEvent macro
collision: every implementation use of that spelling is a managed property or
an unchanged DllImport entry-point string, and native declarations remain intact.

Validated commands on win11 with the usual source SDK/ilasm/M612/CLANG64 and
WPF_BUILD/WPF_CPPCLI_BUILD environment:

```bash
bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh build test-implicit-boxing test-array-initializers
bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh test-boxed-values test-properties test-enums
bash ~/wpfboot/driver/bootstrap/m612/wpf.sh build-directwrite check-directwrite-assembly printing-gate
bash ~/wpfboot/driver/bootstrap/m612/wpf.sh printing-gate
```

The cppcli checks pass; managed consumer builds have zero warnings/errors. The
full DirectWrite regression again loads 85 types, prepares 283 bodies and
enumerates 81 font families with original factory/COM loader/font/face/file
ownership checks. The complete printing gate still **fails**. Its first source
error is PrintQueue.cpp:4481's NgcSerializationManager constructor, whose real
signature takes System.Printing.PrintQueue from the assembly being built. The
metadata importer currently filters that signature because it only recognizes
selected imported types; the real source-owned type must join signature
availability without importing a duplicate implementation. PrintDialog.PrintQueue
has the same dependency. Later errors expose literal stores into String slots,
Object-return boxing and Object-to-enum cast/unboxing gaps.

Evidence is archived locally under
`tmp/wpf8-support/validation/printing-boxing-arrays-20261013`. The gate retains
all 49 original project inputs/66 tree C++ files; no System.Printing runtime
DLL or completed WPF package is claimed. Native import coverage remains 58/139,
and complete native layout, printing/link/runtime and WindowsDesktop packaging
remain open. Installed compiler packages are unchanged.

## 56. Source-owned signatures, return boxing and Object unboxing

The next 2026-10-13 continuation adds explicit --source-type reference names to
the compiler driver/metadata context. A selected imported method can refer to
those names when its metadata identifies the assembly being built. The header
supplies forward declarations, not duplicate implementations or bodies; final
IL emission requires real source ref-class definitions. Imported/source name
collisions, missing or value-type definitions, wrong assembly names and imported
inheritance from source-owned types reject. Source-owned value/generic/nested
types, imported inheritance and full version/public-key identity checks remain
unfinished.

A two-assembly runtime fixture builds a C# peer against a compile-only contract,
then supplies the genuine C++/CLI implementation with the same assembly/version
identity. The peer constructs with the source-owned type, calls its actual
virtual body and returns the original object through a property. Array identity,
byref replacement and GC are checked. The compile-only contract's method throws
if accidentally executed; only the source implementation enters the consumer's
runtime references. Three invalid source-type cases reject. This enables the
original NgcSerializationManager/Async constructors and PrintDialog.PrintQueue
signatures using the genuine System.Printing.PrintQueue source definition.

Primitive/enum returns to Object now use the existing typed box marker and CLR
box instruction. Direct String/Object literal returns and String/Object/array
stores retain Clang assignment/return checking after managed literal construction.
C-style and static casts from direct Object operands to primitives/CLI enums
resolve the existing safe_cast template through Clang lookup and call checking;
the backend emits CLR unbox.any. Checks cover exact scalar/enum payloads,
property getter operands, wrong-type InvalidCastException and null
NullReferenceException. Typed primitive/enum handles, general constructor/
initializer/interface conversions and user-defined conversion selection remain
unfinished.

The complete bridge regression caught a crash in native SIMD header parsing:
Clang uses a temporary expression without a type while assembling a vector
cast. The unboxing hook now leaves empty/untyped operands to the original parser.
The source-owned fixture includes mmintrin.h, and the full DirectWrite rebuild
passes after the fix. Normal native/value/enum cast regressions also pass.

Validated commands on win11 with the usual source SDK/ilasm/M612/CLANG64 and
WPF_BUILD/WPF_CPPCLI_BUILD environment:

```bash
bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh build test-source-owned-types test-implicit-boxing
bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh test-implicit-boxing test-semantic-literals test-exceptions
bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh test-boxed-values test-nativeints test-enums
bash ~/wpfboot/driver/bootstrap/m612/wpf.sh build-directwrite check-directwrite-assembly printing-gate
```

The cppcli runtime/rejection checks pass, with zero C# warnings/errors. The
final WPF command validates all 85 DirectWrite types, 283 prepared bodies, 81
font families and original factory/managed COM loader/font/face/file ownership
paths. It then **fails** in printing Sema, beginning at
PrintSystemAttributeValue.cpp:1097's typed enum-handle initialization. Further
errors show enum-handle casts, Int32^ initialization/return, qualified primitive
syntax, ResourceManager constructor metadata and additional source type imports.
The previous source-owned constructor/property, literal-store, Object-return
boxing and scalar/enum unboxing failures are absent.

Evidence is archived locally under
`tmp/wpf8-support/validation/printing-source-owned-unboxing-20261013`.
No full System.Printing runtime DLL, separate-unit link/runtime proof or WPF
package is produced. All 49 original project inputs/66 tree C++ files remain in
the diagnostic gate; native coverage remains 58/139. Complete native layout,
printing runtime and WindowsDesktop packaging remain open; installed compiler
packages are unchanged.

## 57. Clang-resolved primitive and enum box handles for printing

The 2026-10-13 continuation implements typed scalar/enum handles such as
`Int32^` and `PrintQueueAttributes^`. The private parser asks Clang for the
declaration's resolved base type and instantiates a reference facade only for
primitive or annotated managed-enum types. Scalar aliases, qualified aliases,
enum aliases and typedefs of boxed handles retain their identities. Comma
declarators restart from the original base type. Fundamental sign/width
spellings before a caret use the existing selected System primitive aliases;
identifier expressions and native stars stay in Clang's ordinary grammar.
Native XOR with a variable named Int32 and mixed scalar/handle/native-pointer
declarations are covered. There is no lexical list of enum or alias names.

The facade contributes no runtime class. Its signatures use genuine
System.ValueType references with the boxed element and IsBoxed optional
modifiers, matching the existing value-class handle representation. Direct
metadata imports reconstruct primitive/enum facades from those modifiers.
Implicit scalar boxing and named gcnew allocation emit CLR box operations;
copying, Object conversion and equality preserve reference identity. Checked
Object/static/C-style/safe casts use castclass, dynamic casts use isinst, and
safe scalar unboxing uses unbox.any. Null is retained until dereference, which
raises NullReferenceException; wrong box types raise InvalidCastException.
Boxed zero remains a true handle condition. Object virtual methods dispatch on
the real box. The C++ facade has one pointer-sized layout member so source
sizeof/alignof agrees with native handle size; native storage/pointers to these
GC references are rejected.

Dereference uses a tracked CLR address from unbox, then ldobj/stobj. Tracking
aliases survive collection and mutation updates the shared box. Assignment,
compound updates and prefix/postfix operations capture the receiver once;
compound arithmetic loads the boxed payload rather than treating its address
as a number. All twelve primitive identities/payloads, unsigned enum high bits,
GC rooting, field/property/constructor storage, handle equality and direct
external boxed APIs execute in source-built CoreCLR. The C# consumer prepares
every eligible managed method in the fixture and verifies its optional
modifiers and absence of emitted facade types. Native-pointer, mismatched-box,
native-enum-caret and boxed-native-storage rejection checks pass. Existing
BoxPoint/DateTime boxes and implicit Object boxing regressions also pass.

Commands on win11 use CPPCLI_SDK=/clang64/lib/dotnet,
CPPCLI_ILASM=/clang64/bin/ilasm-dotnet.exe, M612=/home/topkek/dotnetboot/m612,
MSYSTEM=CLANG64, WPF_BUILD=/home/topkek/wpfboot/build and
WPF_CPPCLI_BUILD=/home/topkek/dotnetboot/m612/build/cppcli:

```bash
bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh build test-scalar-boxes
bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh test-boxed-values test-implicit-boxing
bash ~/wpfboot/driver/bootstrap/m612/wpf.sh build-directwrite check-directwrite-assembly printing-gate
```

The focused compiler/CLR checks pass with zero C# warnings/errors. The final
WPF command passes complete DirectWrite generation/assembly, loads 85 types,
prepares 283 managed bodies and finds 81 actual font families, then **fails**
in printing Sema. The original typed enum-handle and Int32-handle initialization,
Object return and dynamic-cast failures are absent. The first remaining errors
are qualified Hashtable default-accessor syntax at
PrintSystemAttributeValue.cpp:4433/4445, a literal base-constructor argument at
PrintSystemUtil.cpp:27, and missing PrinterHResult/nested Facility imports in
PrintServer.cpp. Importing the actually used ArgumentOutOfRangeException exposes
later primitive member, array GetEnumerator/SetValue, literal, additional
metadata and IntPtr/native-cast gates rather than hiding them behind an unknown
exception type. These are not skipped or replaced with stubs.

Direct scalar/enum box signatures are proven; nested array/generic/byref box
imports, dependent element types, primitive/Enum-specific arrow members and
native IntPtr/UIntPtr boxing remain incomplete. No full System.Printing runtime
DLL or WPF package is produced. The diagnostic scope remains 49 original
printing project inputs/66 tree C++ files, with no per-file pass/fail claim.
Native import coverage remains 58/139. Complete layout, printing runtime and
WindowsDesktop SDK/targeting/runtime packaging remain required.

Evidence is archived locally under
`tmp/wpf8-support/validation/printing-scalar-boxes-20261013`.

## 58. Printing accessor, constructor and nested metadata gates

The next 2026-10-13 continuation resolves the original printing failures at
qualified Hashtable default accessors, the ResourceManager base constructor and
PrinterHResult's nested Facility enum. Imported property accessor syntax now
uses the selected metadata getter/setter names and DefaultMember designation,
including default indexers whose CLR name is not Item. Clang still checks the
resulting owner/member call and its access. Runtime checks use actual Hashtable
storage and a separate source-built C# contract with a Lookup indexer and a
private Title setter. A C# override confirms that qualified base calls bypass
virtual dispatch; missing/private setters are rejected.

The private parser resolves parenthesized base/member initializer types before
adapting String/Object literal arguments. Source inline/out-of-line constructors,
base aliases, a literal String field, an Object scalar field and the actual
ResourceManager(String, Assembly) constructor execute successfully. The selected
String constructor wins over a competing bool constructor; native char-pointer
constructor arguments retain their original storage. The same semantic helper
handles parenthesized new expressions without declarator chunks, including
nested imported constructors. Arithmetic/enum arguments are boxed only when all
arity-compatible non-copy constructor candidates use Object in that position;
typed constructor alternatives remain with ordinary Clang selection. Actual
ArgumentOutOfRangeException(String, Object, String) construction retains its
numeric ActualValue and parameter name. Private base constructors still fail
Clang access checks.

Non-generic nested metadata imports now close over their declaring types and
expose nested C++ aliases to private surrogate names. Those surrogates are never
runtime classes: signatures, method owners, fields, constructor calls and boxed
element modifiers retain actual CLR slash-qualified nested identities. Nested
TypeRefs recurse through declaring scopes to their defining assembly. Separate
source-built contract/peer assemblies verify nested enums, reference classes,
value structs, a deeper nested class, constructors, fields/properties, unsigned
enum payloads, typed boxes, external signatures and GC. The CLR prepares the
fixture method bodies and C# verifies exact runtime types and optional modifiers.
Public imports and internal nested imports through a valid as_friend identity
execute; nonfriend internal, private/protected and nested generic imports fail.

Commands on win11 use the same source SDK/ilasm/M612/CLANG64 and WPF build
environment recorded in §57:

```bash
bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh build test-printing-accessors test-nested-imports
bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh test-semantic-literals test-indexers test-scalar-boxes
bash ~/wpfboot/driver/bootstrap/m612/wpf.sh build-directwrite check-directwrite-assembly printing-gate
```

The focused compiler/runtime checks and rejection cases pass with zero C#
warnings/errors. The final WPF command passes full DirectWrite generation and
assembly, loads 85 types, prepares 283 managed bodies and finds 81 font families,
then **fails** in printing Sema. The previously observed qualified default
accessor, ResourceManager initializer, PrinterHResult/Facility and three-argument
Object-valued range-exception constructor failures are absent. The gate imports
the original source-built ReachFramework PrinterHResult/Facility with its actual
friend identity and adds the actually used EntryPointNotFoundException metadata.

The first remaining failure is InteropJobInfo.cpp:498's culture-aware primitive
ToString call. Later errors include array GetEnumerator/SetValue APIs, additional
literal contexts, IntPtr/native casts, qualified inherited accessors and missing
metadata/namespaces. No bodies are removed or replaced with stubs. Braced,
delegating and general direct constructor initialization, broader boxing
overload selection, aliased/inherited accessor-owner discovery and nested
generic/protected/private imports remain incomplete or rejected.

Evidence is archived locally under
`tmp/wpf8-support/validation/printing-accessors-nested-20261013`.
No full System.Printing runtime DLL or completed WPF package is produced.
Diagnostic scope remains 49 original project inputs/66 tree C++ files, without
individual-file pass/fail claims. Native import coverage stays 58/139; complete
layout, printing runtime and WindowsDesktop SDK/targeting/runtime packaging
remain required.

## 59. Primitive instance and Array APIs, printing boxing and local initializers

The 2026-10-13 continuation adds primitive instance lookup from Clang's resolved
scalar kind/width. Selected primitive metadata facades now expose their actual
instance overloads. A typed receiver marker emits a CLR box, and existing
value-type call lowering uses its managed address for the real primitive method.
GetType dispatches through boxed Object. All twelve primitive kinds execute
their real formatting methods, including culture-aware Int32/Int64/Double
overloads, signed minima, unsigned maxima, Boolean and Unicode Char. Typed/Object
Equals, CompareTo, GetHashCode, resolved scalar aliases and record lookup pass.
A side-effecting receiver evaluates once. The original printing pattern
`*(Status^)Enum::Parse(Status::typeid, value.ToString(culture))` executes against
the source-built runtime with exact enum identity.

When System.Array is imported, the rank-one array facade inherits its selected
API rather than only Object. Its template rank parameter has a private name to
avoid collision with the inherited Rank property. Real Array GetEnumerator,
SetValue/GetValue, GetLength/Rank/GetLowerBound/GetUpperBound and Clone calls
execute on actual CLR arrays. Integer stores use real Object boxing, clone
storage is independent, and string enumerators retain their arrays across GC.
Wrong element types, null arrays, invalid dimensions and out-of-range stores
raise actual CLR exceptions. The existing direct index/Length/for-each paths
remain supported; native pointer values cannot enter Array.SetValue.

The original PropertyCollectionMemorySafeHandle.SetValue overloads take Object
or Type in their third slot. Non-null scalar arguments cannot initialize the
unrelated Type reference. Object boxing now excludes that impossible reference
alternative while keeping typed scalar, interface, Enum/ValueType and null
alternatives with normal Sema. The exact printing-shaped call selects Object
and retains its integer payload; an int alternative still wins, explicitly
typed null selects Type, and ambiguous literal zero is rejected.

Unary `%` on a managed value-class operand creates a typed CLR box copy; the
original `attributeMap->Add("DriverName", (Object^)(%infoData))` now passes Sema.
Fixture box identity, copied fields, Object casts and GC are checked. Native
scalar operands are explicitly rejected. Binary remainder remains ordinary C++.
Local copy initialization resolves the variable's actual String/Object type
before managed literal conversion or scalar boxing. Literal-zero null
initialization and native character buffers keep their previous behavior;
variable-valued zero boxes as Int32. Native-header declarations bypass this
managed initializer hook, including incomplete recovery expression types.

Validation on win11 uses the source SDK/ilasm/M612/CLANG64 and WPF environment
recorded in §57:

```bash
bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh build test-primitive-array-apis test-primitive-statics test-implicit-boxing test-each
bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh test-primitive-array-apis
bash ~/wpfboot/driver/bootstrap/m612/wpf.sh build-directwrite check-directwrite-assembly printing-gate
```

Focused runtime/rejection checks and existing primitive-static, Object-boxing
and enumeration regressions pass with zero C# warnings/errors. Complete
DirectWrite generation/assembly still passes: 85 types load, 283 managed bodies
prepare and 81 font families are found. The final printing gate **fails** first
at InteropPrinterHandler.cpp:3159's IntPtr-to-void-pointer cast. The earlier
InteropJobInfo primitive formatting, array GetEnumerator, unary-percent,
local String initializer and printing SetValue overload failures are absent.
Later errors include additional native pointer casts, inherited qualified
accessors and missing namespace/type metadata. No original bodies are skipped
or replaced with stubs.

Primitive instance lookup needs the selected primitive metadata; general boxing
overload resolution, enum-specific/interface dispatch, mutating/byref-like
primitive APIs, scalar/reference-class unary-percent forms and general
braced/direct/conditional initialization remain incomplete. Evidence is archived
under `tmp/wpf8-support/validation/printing-primitive-array-20261013` locally.
The diagnostic scope remains 49 original printing project inputs/66 tree C++
files. No complete System.Printing runtime DLL or WPF package is produced.
Native import coverage remains 58/139; full layout, printing runtime and
WindowsDesktop SDK/targeting/runtime packaging remain required.

## 60. Native integer casts, inherited accessors and GDI metadata

The next 2026-10-13 continuation resolves the printing IntPtr/native-pointer
casts with explicit conversion facades on the actual imported IntPtr/UIntPtr
types. Clang selects the destination pointer type, and a marked conversion emits
the original native integer bits with conv.u. No boxed-reference reinterpretation
is used. Supported void, typed and const native targets retain full pointer
width, unsigned high bits and single receiver evaluation. A typed pointer writes
to actual Marshal-allocated memory and the original Marshal API reads the value.
Implicit conversion and casts to managed String handles are rejected; readonly
native-integer fields keep their existing access checks. Opaque/nontrivial or
GC-bearing target forms remain incomplete or rejected.

Qualified source property selectors now follow recognized source base
declarations. The original PrintSystemJobInfo::Name::set method pointer passes
Sema. A fixture with an intermediate base binds a CLR String delegate to the
inherited protected setter, calls an inherited getter through the derived
qualifier and verifies the delegate target after GC. A private base setter
remains inaccessible. General source aliases/namespaces/generic base discovery
are still incomplete.

The adapter now preprocesses the pin_ptr/interior_ptr compatibility declarations
with the source, exposing the forms that MSVC supplies as language intrinsics.
Explicit vcclr.h includes share the same preprocessing guard. Adding the header
only to the later AST pass was rejected during validation because it duplicated
already adapted declarations; the final driver includes it once in cli-adapt.
Existing Unicode/native-array pinning and concurrent compacting-GC checks pass.

The printing gate imports actually used RuntimeCompatibilityAttribute, FileShare,
Math, Color, Int32Rect and AlphaFlattener Utility metadata, followed by
Geometry.PathGeometryData, PathGeometry, FillRule, GlyphTypeface, PixelFormat,
Transform, SolidColorBrush and pen enums. Source assembly identities and friend
access are retained; no empty namespace or runtime placeholder supplies these
types. The geometry/pen imports expose enum-valued literal fields. Their C++
facades now cast the metadata constant to its actual enum type, retaining the
original CLR field type/token/value. A separately source-built nested enum
constant with its unsigned high bit set passes CLR identity/payload checks.

Validation on win11 uses the source SDK/ilasm/M612/CLANG64 and WPF environment
recorded in §57:

```bash
bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh build test-nativeints test-printing-accessors test-pinning
bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh test-nested-imports
bash ~/wpfboot/driver/bootstrap/m612/wpf.sh build-directwrite check-directwrite-assembly printing-gate
```

The focused runtime, metadata and rejection checks pass with zero C#
warnings/errors. Complete DirectWrite generation/assembly still passes, with
85 types loaded, 283 managed bodies prepared and 81 font families found. The
final printing gate **fails** first at gdidevice.h:45/60's interior_ptr indexing.
The preceding native integer casts, inherited Name setter, missing namespaces
and geometry/pen enum-constant failures are absent. Later errors include
additional metadata, legacy-device interface availability, native min/max
compatibility and further drawing operations. The gate does not skip original
bodies or replace them with stubs.

Evidence is archived locally under
`tmp/wpf8-support/validation/printing-nativeint-accessor-20261013`.
Diagnostic scope remains 49 original printing project inputs/66 tree C++ files.
No complete System.Printing runtime DLL or WPF package is produced. Native
import coverage remains 58/139; complete layout, printing runtime and
WindowsDesktop SDK/targeting/runtime packaging remain required.

## 61. Tracked interior indexing and the original GDI byte snapshot

The 2026-10-13 continuation adds primitive interior_ptr/pin_ptr indexing to the
intrinsic declarations and AST emitter. The selected C++ subscript remains an
lvalue of the element type; its address adds a signed element-sized offset to
the original CLR managed byref. Reads/stores use ldobj/stobj. Indexed addresses,
compound updates and prefix/postfix operations capture the location once in a
tracked byref local. Pin/volatile memory prefixes remain in use, and element
const qualification is enforced by Clang rather than bypassed by the emitter.

The fixture executes the original CachedGDIObject pattern: a constructor takes
a const interior_ptr<Byte> object and copies its indexed bytes into an actual
CLR byte array after collection. C# passes array-element byrefs. Signed negative
offsets, 32-bit/64-bit/floating element strides, loads/stores, indexed tracking
aliases, compound/prefix/postfix values, single index evaluation, byte narrowing,
pinned operations and independent copied storage pass across GC calls. All
fixture managed methods/constructors are prepared in the source-built runtime.
Element-const writes are rejected. Existing concurrent compacting-GC string and
native-array pinning checks also pass.

The original printing gate now imports actual Stack, ImageBrush,
StyleSimulations, MatrixTransform, StreamGeometry, GeometryGroup, Freezable and
Animatable metadata needed by the drawing code. The base metadata restores
Geometry's inherited Object API. ILegacyDevice remains the original source
interface in LegacyDevice.hpp; it is not an imported replacement. With these
source signatures available, the earlier legacy-device conversion/member
errors are absent. The private source preparation restores the Windows min/max
macro definitions that trusted MinGW minwindef.h omits in C++ mode; it retains
Windows evaluation behavior rather than introducing new helper implementations.

Commands on win11 use the source SDK/ilasm/M612/CLANG64 and WPF environment
recorded in §57:

```bash
bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh build
bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh test-interior-indexing test-pinning
bash ~/wpfboot/driver/bootstrap/m612/wpf.sh build-directwrite check-directwrite-assembly printing-gate
```

The focused runtime/rejection and pinning checks pass with zero C# warnings or
errors. Complete DirectWrite generation/assembly still loads 85 types, prepares
283 managed bodies and finds 81 font families. The final printing gate **fails**
first at utils.cpp:350's Matrix multiplication operator. Subsequent failures
include more drawing metadata and native BITMAPINFO interior-pointer arrow
access. The previous byte indexing, Stack/ImageBrush/style/geometry base,
legacy-device conversion and min/max errors are absent. No original bodies are
removed or replaced with stubs.

Managed pointer indexing has low-level pointer semantics: callers supply valid
ranges, rather than retaining CLR array bounds on every index. Native/managed
record elements, arrow access, general pointer arithmetic and broader interior
return provenance remain unfinished. Evidence is archived locally under
`tmp/wpf8-support/validation/printing-interior-index-20261013`. Diagnostic scope
remains 49 original printing project inputs/66 tree C++ files; no complete
System.Printing runtime DLL or WPF package is produced. Native import coverage
remains 58/139. Full layout, printing runtime and WindowsDesktop
SDK/targeting/runtime packaging remain required.


## 62. WPF value operators and tracked native-record bitmap pointers

The 2026-10-13 continuation exposes supported public imported CLR value unary
and binary operators as annotated hidden friends. Ordinary Clang ADL selects
the exact metadata method, including asymmetric scalar/value signatures. The
real source-built WindowsBase Matrix multiplication and equality execute through
the bridge, match C# results for two nonidentity matrices, and preserve input
copies. This is imported operator dispatch, not a complete CLI operator compiler.

Pointer intrinsics now admit proven GC-free native POD elements. Their implicit
arrow operations retain CLR byrefs; original BITMAPINFO headers, RGBQUAD inline
arrays, packed fields and indexed updates use actual Clang offsets/layouts.
Captured native field/array/bitfield locations remain tracked across managed
calls. Explicit interior views accept tracked addresses and pointer facades,
reject dropped const/volatile qualifiers, and reject untracked raw pointer
inputs. Explicit arrow calls and tracked array decay cannot escape into raw
native pointer bindings without pinning. Managed/GC-bearing record elements
remain unsupported.

Interior local/parameter ++/-- and +=/-= scale by actual native element layout,
preserve prefix/postfix results and evaluate offset expressions once. Runtime
checks use managed byte buffers, the original Windows BITMAPINFO declaration,
packed native layouts, tracked aliases, GC in index/offset expressions and
four-byte DWORD stepping. Native record pointer views remain low-level operations
with caller-owned range/alignment validity; general arithmetic expressions and
interior return provenance remain unfinished.

The printing gate imports actual CombinedGeometry, PixelFormats, bitmap palette
and conversion types, drawing and geometry contexts, ContainerVisual, Brushes,
CriticalFinalizerObject, PointCollection, DoubleCollection, dash styles,
CompositionResourceManager/MilMatrix3x2D and GeometryHelper. These are actual
source-built dependencies, including the existing as_friend access boundary.
The original gate gets past Matrix multiplication, BITMAPINFO arrow operations,
bitmap interior casts/updates and drawing/geometry base discovery. Its first
remaining errors are gdipen.cpp:77/128 collection-property indexing, followed
by further missing gradient/bitmap codec/printing metadata imports.

Validation on win11 (the usual M612/MSYSTEM/WPF_BUILD/WPF_CPPCLI_BUILD environment):

```sh
CPPCLI_SDK=/clang64/lib/dotnet CPPCLI_ILASM=/clang64/bin/ilasm-dotnet.exe \
 M612=/home/topkek/dotnetboot/m612 MSYSTEM=CLANG64 \
 bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh \
 build test-interior-records test-interior-indexing test-pinning
CPPCLI_SDK=/clang64/lib/dotnet CPPCLI_ILASM=/clang64/bin/ilasm-dotnet.exe \
 M612=/home/topkek/dotnetboot/m612 MSYSTEM=CLANG64 \
 bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh build test-interior-records
M612=/home/topkek/dotnetboot/m612 MSYSTEM=CLANG64 \
 WPF_BUILD=/home/topkek/wpfboot/build \
 WPF_CPPCLI_BUILD=/home/topkek/dotnetboot/m612/build/cppcli \
 bash ~/wpfboot/driver/bootstrap/m612/wpf.sh \
 check-matrix-operators build-directwrite check-directwrite-assembly printing-gate
M612=/home/topkek/dotnetboot/m612 MSYSTEM=CLANG64 \
 WPF_BUILD=/home/topkek/wpfboot/build \
 WPF_CPPCLI_BUILD=/home/topkek/dotnetboot/m612/build/cppcli \
 bash ~/wpfboot/driver/bootstrap/m612/wpf.sh printing-gate
```

Both compiler commands pass, including raw escape, GC-bearing element,
qualification and untracked-pointer rejection checks. The second rebuild removes
an unreachable duplicate pin initializer branch. The WPF command passes Matrix,
DirectWrite rebuild and runtime checks (85 types, 283 bodies, 81 font families),
then fails at printing. A mistyped MilMatrix3x2D namespace in that intermediate
gate was corrected; the final printing-only gate reaches the remaining errors
listed above. Bash/Python/JSON syntax and git diff whitespace checks pass.

Logs, manifests, compiler work directories and runtime artifacts are archived at
`tmp/wpf8-support/validation/printing-operators-records-20261013`. Scope remains
49 original printing project inputs/66 tree C++ files as a combined diagnostic
gate, without a per-file compile count or a complete System.Printing DLL.
Native import coverage remains 58/139; full text/paragraph behavior, printing
runtime and WindowsDesktop SDK/targeting/runtime packaging remain required.
No completed WPF package is produced; the goal remains active.


## 63. Printing collection-property indexing and rendering metadata

The next 2026-10-13 continuation resolves a non-indexed instance property getter
before applying managed array/default-indexer lookup. Previously
`dashStyle->Dashes[i]` reached ordinary C++ pointer indexing and produced a
DoubleCollection value instead of a double. The private parser now asks Sema to
complete a non-indexed MSPropertyRefExpr, then performs existing type-selected
managed lookup on its actual result. Named indexed properties remain bound to
their following bracket, and nested indexed-property getter completion retains
its existing behavior.

The nested-indexer runtime fixture now includes an instance property returning
a managed List of managed arrays. Its compound update checks single getter and
outer/inner-index evaluation and actual stored data. Source/imported named and
default indexers, reference updates and malformed signature rejection checks
still pass. Static property proxy indexing remains an unimplemented separate
case; the initial fixture accidentally exercised that case and was corrected to
the instance-property operation used by the original printing source.

Printing imports actual SortedList, gradient stops/brushes, PrintingCanceledException,
bitmap codec/encoder/frame/decoder types, ImageSource/TileBrush and their used
mapping/tiling/stretch/alignment/gradient enums. The original gate now passes
both gdipen.cpp collection-property indexing failures and advances into gdirt.cpp.
Remaining observed errors begin at RectangleGeometry/IntersectionDetail imports,
then GeometryProxy tracking-reference binding to a temporary, Object boxing,
Point subtraction/Vector import, Object-to-Color casts and Uri imports.

Validation on win11 with the standard M612/MSYSTEM/compiler/WPF build environment:

```sh
bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh build test-nested-indexers test-indexers
bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh test-nested-indexers test-indexers
bash ~/wpfboot/driver/bootstrap/m612/wpf.sh printing-gate
bash ~/wpfboot/driver/bootstrap/m612/wpf.sh build-directwrite check-directwrite-assembly printing-gate
```

The first command rebuilds ParseExpr.cpp successfully, then exposes the static
proxy fixture issue described above. The second command passes the corrected
instance-property fixture and all focused indexer checks. The final WPF command
passes DirectWrite rebuild/load, 85 CLR types, 283 prepared bodies and 81 real
font families; it still fails at printing with the remaining errors above.
Bash/Python/JSON syntax and git diff checks pass. Logs, manifests and compiled
runtime evidence are archived locally under
`tmp/wpf8-support/validation/printing-property-results-20261013`.

All 49 original project inputs/66 printing tree C++ files remain in the combined
gate. No complete System.Printing runtime DLL or WPF package is produced. Native
coverage remains 58/139; full layout/printing/runtime and WindowsDesktop
SDK/targeting/runtime packaging remain required, and the full goal stays active.


## 64. Printing Object index keys, value unboxing and canonical Char arrays

The 2026-10-13 continuation resolves indexed-property argument placeholders and
uses the actual getter overload declarations to box arithmetic/enum operands
when the selected argument position is unambiguously System.Object. Native and
typed indexers retain normal Sema. This fixes original SortedList operations
such as `stops[stop->Offset]` without changing the source or treating numeric
keys as pointers. Normal source managed value-to-Object conversion supplies the
stored Color value. Explicit `->default[index]` / `.default[index]` member
spelling adapts to the existing default property facade in ordinary expressions,
member initializers and accessor bodies; declaration and switch default tokens
remain unchanged.

Object-targeted C-style/static cast discovery now recognizes actual managed
value declarations as well as primitives/CLI enums. It uses existing safe-cast
lowering to emit CLR unbox.any. The runtime fixture stores a value struct under
a side-effecting double key in actual SortedList, checks copied data after source
mutation, retrieves it through explicit default syntax and source get/set
accessors, verifies exactly two side-effecting key calls across GC, and checks
both successful casts plus wrong-type/null CLR exceptions.

Metadata Char signatures now use Windows' wchar_t, matching the existing
System.Char primitive facade. Previously the metadata reader used char16_t for
array/generic method signatures while source System.Char resolved to wchar_t,
which made original GlyphRun.Characters.CopyTo reject its actual array<Char>.
The focused API fixture calls real String.CopyTo into array<System.Char> and
verifies a Unicode string including a surrogate pair through a C# char[] consumer.
This preserves CLR char signatures and the Windows ABI; other target platforms
remain outside the current frontend scope.

The printing gate adds actual RectangleGeometry, IntersectionDetail,
ToleranceType, Vector, Uri/UriKind, Path/File, Encoding, Random and font style,
weight and stretch types. Original Point subtraction becomes unambiguous with
Vector imported. The gate gets past Object index-key errors, Color casts,
IsMappingConsistent/Char-array CopyTo, explicit font-cache default syntax and
those imports. Remaining observed errors begin with gdirt.cpp:744's temporary
GeometryProxy tracking-reference binding, then String/native-literal comparison,
property-produced enum bitwise operations, native calls using interior record
pointers, further ManualResetEvent/COMException imports and interface-to-Object
argument conversions. No complete System.Printing assembly is emitted.

Validation on win11 (with the usual CPPCLI_SDK/CPPCLI_ILASM/M612/MSYSTEM and WPF
build variables):

```sh
bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh build test-implicit-boxing test-nested-indexers test-indexers
bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh test-implicit-boxing test-nested-indexers test-indexers
bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh build test-implicit-boxing test-primitive-array-apis test-nested-indexers test-indexers
bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh build test-implicit-boxing
bash ~/wpfboot/driver/bootstrap/m612/wpf.sh build-directwrite check-directwrite-assembly printing-gate
```

The first command rebuilds successfully, then fails in the C# fixture because
`record` was a contextual keyword at the start of a statement. Renaming that
fixture variable fixes it; the second and third commands pass all focused
runtime and rejection checks. The fourth rebuild includes explicit default
syntax in accessor bodies and passes its real SortedList/runtime checks. The
final WPF command passes DirectWrite rebuild, 85 types, 283 prepared bodies,
81 font families and ownership/loader checks; printing still fails on the
remaining errors above. Bash/Python/JSON syntax and whitespace checks pass.

Logs, manifests and compiled runtime artifacts are archived locally under
`tmp/wpf8-support/validation/printing-value-char-20261013`. All 49 printing
project inputs/66 tree C++ files remain in the combined diagnostic gate; no
per-file pass count, complete printing runtime or finished WPF package is
claimed. Native coverage remains 58/139. Full text/paragraph and printing
behavior plus WindowsDesktop SDK/targeting/runtime packaging remain required;
the full goal stays active.


## 65. Ref-class temporaries passed to source tracking-reference parameters

The 2026-10-13 continuation permits a directly constructed source ref-class
prvalue to bind an exact source percent tracking-reference call parameter.
Clang's resolved method declarations, actual record identity and source sigil
select the operation. Native ampersand references retain ordinary C++ rules;
arity-compatible overloads must consistently declare the supported tracking
slot. Constructor functional casts and CXXBindTemporary wrappers are resolved
from the AST rather than rewritten from spelling.

The annotated temporary intrinsic emits the actual source constructor with
newobj, stores the handle in a CLR class local, and passes that local's address
to the method/delegate. It neither creates a native C++ object nor copies the
ref class. Deterministically disposable temporaries are rejected until
full-expression cleanup exists. Imported temporaries, arbitrary factory/value
expressions, ref-class copies and mixed overload selection remain incomplete.

The runtime fixture verifies direct calls, delegate Invoke, two constructed
arguments, exact construction/operand counts, mutation through a class byref,
constructor exception propagation without callback execution, reflected CLR
class& signatures and retained identity after forced compacting GC. Native
reference binding, missing deterministic cleanup and mixed unsupported overload
sets have targeted rejection checks. Original tracking-reference fixtures pass.
The first build exposed a missing MulticastDelegate test import and an AST
CXXBindTemporary wrapper, both fixed. A by-value ref-class rejection fixture
hit its unsupported declaration before the intended operation; the replacement
uses a supported scalar overload and validates ordinary Sema rejection.

Validation on win11 with the usual compiler and WPF environments:

```sh
bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh build test-tracking-temporaries
bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh test-tracking-temporaries test-tracking
bash ~/wpfboot/driver/bootstrap/m612/wpf.sh build-directwrite check-directwrite-assembly printing-gate
```

The frontend rebuild and positive temporary runtime proof pass; the subsequent
focused command passes the final three rejection cases and original tracking
checks. The full WPF command passes DirectWrite load, 85 types, 283 bodies and
81 real font families, then fails at printing. The original gdirt.cpp:744
GeometryProxy constructor callback now passes Sema. First remaining errors are
String/native-literal comparisons, property-produced enum bitwise operations,
interior-record native-call conversions, interface-to-Object conversions and
missing managed base chains for FixedDocument/FixedPage. Actual ManualResetEvent
and COMException imports were added to the gate. Full printing IL/link/runtime
remain unproven.

Logs, manifests and runtime artifacts are archived locally under
`tmp/wpf8-support/validation/printing-tracking-temporary-20261013`. All 49 printing
project inputs/66 tree C++ files remain in the combined diagnostic scope. No
complete System.Printing DLL or WPF package is produced; native text coverage
remains 58/139 and full text/paragraph/printing/WindowsDesktop SDK and framework
packaging remain required. The full goal stays active.


## 66. String operator binding and property-produced enum operations

The 2026-10-13 continuation exposes the source-metadata String op_Equality and
op_Inequality slots as annotated helpers. The private parser resolves actual
String handle operands, supported literals and nulls and binds those methods
for equality/inequality. It does not compare CLR String object addresses for
content equality. Native buffer operands retain ordinary C++ diagnostics and
are not implicitly turned into managed strings. Other reference operators,
String concatenation and Object/native-buffer conversion policies remain open.
Microsoft's primary C++/CLI String documentation provides the comparison and
managed literal examples used for this operation:
https://learn.microsoft.com/en-us/cpp/extensions/string-cpp-component-extensions?view=msvc-170

Before read-only equality and enum bitwise operations, Sema now completes
MSProperty placeholder expressions to their actual getter result types.
This fixes original GlyphTypeface.StyleSimulations bitwise tests while keeping
compound-assignment property locations on their existing getter/setter path.
The runtime fixture checks non-interned equal strings, unequal values,
narrow/Unicode literals, reversed operands, null comparisons, a property getter
with GC, high-bit unsigned enum flags and exactly one getter per operand.
It prepares eligible CLR method bodies and repeats comparison after forced
compacting GC. Native-buffer/String comparison remains a targeted rejection.

Validation on win11 with the usual compiler and WPF build environment:

```sh
bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh build test-string-enum-properties test-enums
bash ~/wpfboot/driver/bootstrap/m612/wpf.sh build-directwrite check-directwrite-assembly printing-gate
```

The frontend rebuild, new runtime/rejection checks and original source/imported
enum checks pass. The full WPF command passes DirectWrite load, 85 types,
283 prepared bodies and 81 font families, then fails at printing. It gets past
the original gditextrt.cpp String/literal and property enum errors. The first
remaining failure is gditextrt.cpp:798 interior_ptr<ENUMLOGFONTEXDV> conversion
for native memcpy, followed by native CreateFontIndirectEx pointer conversion
and interface/FixedDocument/FixedPage-to-Object argument conversion. Printing
assembly emission, native linking and runtime remain unproven.

Logs, manifests, compiler work files and runtime artifacts are archived locally
under `tmp/wpf8-support/validation/printing-string-enum-20261013`. Bash/JSON syntax
and git whitespace checks pass. Scope remains 49 original project inputs/66 tree
C++ files as one diagnostic gate, without a per-file pass count or complete
System.Printing DLL. Native text coverage remains 58/139; full text/paragraph/
printing behavior and WindowsDesktop SDK/reference/runtime packaging still
require work. No complete WPF package is produced; the full goal stays active.


## 67. Explicit native font-record pins and selected CRT import symbols

The 2026-10-13 continuation ports the original printing native font-record call
sites with explicit pin_ptr<ENUMLOGFONTEXDV> locals. The reviewed
`wpf-native/printing/0001-pin-native-font-record-calls.patch` changes two memcpy
sources and CreateFontIndirectEx's argument. The same interior records retain
all existing field mutation/cache behavior; the pins keep any managed backing
buffer stable until the method's lexical cleanup. The gate applies this patch
to its freshly copied private CPP tree, leaving the original source checkout
untouched. It is recorded in inputs.json's portPreparation.

This keeps the existing ban on raw interior-pointer escape. ECMA-372 §12.3.6–7
and §14.7 define tracking/pinning behavior and disallow interior-to-native
pointer conversion while permitting native pointer conversion through pins:
https://www.ecma-international.org/wp-content/uploads/ECMA-372_1st_edition_december_2005.pdf
No general implicit interior-pointer cast or automatic call pinning is claimed.

The driver adds `--native-import-symbol SYMBOL ARCHIVE`, using the same trusted
x64 named COFF code import parsing as complete archive imports. It includes only
the requested symbol, rejects missing/invalid selectors and retains conflicting
binding rejection. Printing selects memcpy from MSYS2 libmsvcrt.a alongside the
six existing Windows system archives. The first full-CRT import attempt failed
because __C_specific_handler had multiple DLL bindings; selecting the actual
required copy routine avoids importing unrelated symbols without silently
choosing between conflicting DLLs.

The runtime fixture uses original Windows ENUMLOGFONTEXDV and LOGFONT records
with actual native layouts. Managed byte buffers are viewed through tracked
interior pointers and explicitly pinned before native calls. A separately built
native DLL checks Unicode face-name fields, waits while a background collector
forces compacting collections, copies the complete record and updates its font
height. The bridge reads those actual bytes, calls imported memcpy for another
complete snapshot and checks equality. It also calls real CreateFontIndirectExW,
reads its logical font height through GetObjectW, and verifies successful
DeleteObject in a finally block. All eligible CLR bodies are prepared.

Validation on win11 with the standard compiler/WPF build environment:

```sh
bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh test-record-pins
bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh build test-record-pins
bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh test-record-pins
bash ~/wpfboot/driver/bootstrap/m612/wpf.sh build-directwrite check-directwrite-assembly printing-gate
```

The focused compiler build configures the changed driver; Ninja has no C++ work
to do. The final record check passes real native/GDI operations, 2,786 confirmed
compacting collections inside native calls and missing-symbol rejection. Earlier
record runs passed 2,581/2,223/3,011 collections while the selected-symbol and GDI
checks were added. Bash/Python/JSON syntax and whitespace checks pass. The WPF
command passes DirectWrite rebuild/load (85 types, 283 prepared bodies, 81 font
families), applies the private source patch and gets past all three native font
record call conversion errors. It still fails first at XpsCompatiblePrinter.cpp
interface-to-Object arguments, then FixedDocument/FixedPage Object conversions
and further XPS packaging/event metadata gaps. No complete printing IL/runtime
assembly is emitted.

Logs, manifests, patched source and runtime artifacts are archived locally under
`tmp/wpf8-support/validation/printing-record-pins-20261013`. Scope remains all
49 original project inputs/66 printing tree C++ files as a combined diagnostic
gate; a per-file pass count is not claimed. Native text coverage stays 58/139.
Full layout, printing runtime, WindowsDesktop SDK/reference/runtime packs and
installed WPF packaging remain required. The full goal remains active.


## 68. Interface Object identity, XPS metadata and complete printing Sema gate

The 2026-10-13 continuation converts source/imported managed interface handles
to Object through a validated identity intrinsic. It emits the same CLR object
reference, including an interface reference to an already boxed value; it does
not rebox or treat that reference as a native adjusted base pointer. Resolved
Object call slots adapt only when all arity-compatible candidates require
Object. Return/local and existing Object field/property store adaptation reuse
the same conversion. Typed interface overloads retain ordinary Sema. The
existing unrelated-reference overload filter is now limited to scalar/enum
arguments, preserving typed alternatives for reference operands. Constructor,
indexed-key, property-result/conditional and broader conversion selection remain
unfinished.

The fixture checks source interface dispatch, exact object identity, copied
reference storage in locals/fields/properties, operand counts, nulls and typed
interface-vs-Object overload selection. An imported IDisposable method path
accepts an actual MemoryStream and an already boxed C# struct implementation,
verifying identity without reboxing. Eligible CLR bodies are prepared and
retained references survive forced compacting GC. Native record pointers cannot
implicitly initialize Object. Existing primitive/enum/value boxing checks pass.

Printing imports actual FrameworkContentElement/ContentElement and
FrameworkElement/UIElement bases, so FixedDocument/FixedPage/FixedDocumentSequence
use their real CLR inheritance for Object arguments. It imports the actual XPS
fixed-document/page reader interfaces, ReadOnlyCollection and completed/progress
event delegate types, plus Application and ThreadStart. No replacement metadata
or runtime implementation is introduced.

Private printing source patch 0002 corrects four TranslateProgressChangeLevel
switch labels to the actual XpsWritingProgressChangeLevel enum. The upstream
input and output enums share values 0..3; returned values and method behavior
are unchanged. The gate applies both reviewed printing source patches to its
fresh private CPP copy and records the preparation. This preserves scoped enum
assignment rules rather than adding a general implicit cross-enum conversion.
Pinned pointer +=/-= and prefix/postfix ++/-- declarations now expose the
existing tracked update lowering. This fixes the original XPS stream write-loop
buffer increments. The native-pin fixture validates four-byte strides,
prefix/postfix/compound updates, old-pointer identity and managed array mutations
across GC, retaining existing lexical pin cleanup.

Validation on win11 with the usual compiler and WPF build environment:

```sh
bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh build test-interface-object test-implicit-boxing
bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh build test-native-pins
bash ~/wpfboot/driver/bootstrap/m612/wpf.sh build-directwrite check-directwrite-assembly printing-gate
bash ~/wpfboot/driver/bootstrap/m612/wpf.sh printing-gate
```

Both focused compiler commands pass, including runtime and rejection checks.
The WPF command passes the unchanged complete DirectWrite runtime scope
(85 types, 283 bodies, 81 font families), then advances through successive XPS
metadata errors. After the final imports and pin updates, the printing-only gate
passes Clang parsing/Sema for the complete combined source and enters the
AST-to-IL frontend. HandleTranslationUnit skips that frontend whenever Clang has
an earlier error; the observed frontend diagnostic therefore confirms this
stage boundary. It now fails in declaration validation with `tracking references
in constructors are not enabled yet`. This is not a complete IL/assembly build,
a per-file compile count, native link proof or printing runtime check.

Bash/Python/JSON syntax and whitespace checks pass. Logs, manifests, patched
source and runtime artifacts are archived locally under
`tmp/wpf8-support/validation/printing-interface-xps-20261013`. All 49 original
project inputs/66 tree C++ files remain in the diagnostic scope. Full printing
lowering/link/runtime and the previously open full text/layout and WindowsDesktop
SDK/reference/runtime packaging remain required. Native text coverage remains
58/139; no complete System.Printing DLL or WPF package is produced, and the full
goal stays active.


## 69. Tracking-reference constructor arguments and declaration diagnostics

The 2026-10-13 continuation removes the blanket constructor-reference rejection
and lowers constructor arguments through one shared operation. Tracking
references require the exact underlying referent type and pass a tracked
address. Native references retain the existing stable-address rules. Ordinary
arguments retain source/Sema-selected conversions and native-pointer binding
checks. Allocation, value construction, base constructor initialization and
source ref-class temporary construction use the same lowering.

Taking the tracked address of a dereferenced ref-class handle creates a CLR
class local holding the same object and passes that slot's address. It does not
read native object layout. This supplies source constructor class-reference
operands while preserving field mutation and GC identity. Derived-to-base
tracking constructor aliasing remains rejected to keep CLR byref types exact.
General ref-class stack storage, member cleanup, delegating constructors and
broader lifetime/escape forms remain unfinished.

The fixture verifies primitive/value/Object-handle/ref-class byref mutation,
actual class& constructor signatures, source allocation and C# constructor
invocation, base initialization before member snapshots, a managed array element
address with one index evaluation, value construction, constructor exceptions
retaining prior mutations and forced compacting GC. The first rebuild exposed
the missing dereferenced-handle address path; the corrected runtime check passes.
Original factory/private/protected/abstract/literal/base constructor checks pass.
Their native-new rejection expected an obsolete unsupported-AST diagnostic;
the expected text now matches the existing explicit native-allocation rejection.

Validation on win11 with the usual compiler/WPF build environment:

```sh
bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh build test-constructor-tracking test-constructors
bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh test-constructor-tracking test-constructors
bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh build
bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh test-tracking-temporaries
bash ~/wpfboot/driver/bootstrap/m612/wpf.sh build-directwrite check-directwrite-assembly printing-gate
bash ~/wpfboot/driver/bootstrap/m612/wpf.sh printing-gate
```

The final focused constructor command passes runtime and all rejection checks.
The subsequent build validates improved interop-target diagnostic names; the
source temporary regression also passes. The WPF command passes DirectWrite's
85 types, 283 bodies and 81-font-family runtime scope, then printing advances
past constructor declarations. The final printing-only gate still passes
Clang parsing/Sema and fails in AST-to-IL declaration validation. It now names
the exact annotation/field: `cli.param.marshal` on
`MS::Internal::PrintWin32Thunk::PrinterDefaults::defaultDataType`, whose original
LPWStr MarshalAs needs field-marshalling metadata support. No attribute is
silently discarded and no complete printing IL/DLL is produced.

JSON/Bash syntax and whitespace checks pass. Logs, manifests and runtime
artifacts are archived locally under
`tmp/wpf8-support/validation/printing-constructor-tracking-20261013`. The combined
scope retains all 49 original project inputs/66 tree C++ files without a
per-file compile count. Native coverage remains 58/139; full printing lowering,
native link/runtime behavior, full text/layout and WindowsDesktop SDK/reference/
runtime packaging remain required. The complete WPF package is unfinished and
the full goal stays active.


## 70. Instance field MarshalAs metadata for printing records

The 2026-10-13 continuation accepts MarshalAs annotations on managed instance
fields and emits actual CLR FieldMarshal descriptors through ilasm field marshal
syntax. Selected scalar, enum, string, native-integer/pointer and interface
forms use the same named native type table as parameter metadata. Field-specific
validation checks storage kinds and rejects unsupported native types/named
array options. Parameter In/Out annotations retain their parameter-only target
rules; static-field and return marshalling remain incomplete. No annotation is
silently discarded.

The runtime fixture produces sequential source ref classes corresponding to
actual Windows PRINTER_DEFAULTSW and DOCINFOW records. CLR reflection verifies
LPWStr/SysInt/U4 field descriptors, Marshal.SizeOf checks native sizes 24/40 and
Marshal.OffsetOf checks pointer/access offsets 8/16. A separately compiled native
DLL reads Unicode strings including a surrogate pair, a full-width native
pointer and a high-bit enum, waits during forced compacting GC and changes the
enum field. C# P/Invoke into that DLL marshals the generated source class and
copies the enum back, preserving Unicode content. The document record checks
its declared native size, Unicode/null strings and unsigned type bits. This
validates real native layout and marshalling, rather than only metadata spelling.

Validation on win11 with the usual compiler and WPF build environment:

```sh
bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh build test-field-marshalling
bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh test-field-marshalling
bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh test-layout
bash ~/wpfboot/driver/bootstrap/m612/wpf.sh build-directwrite check-directwrite-assembly printing-gate
```

The initial frontend build passes, then the native fixture exposes a missing
stddef.h include for offsetof; explicit standard headers fix it. The subsequent
focused marshalling command passes all native/reflection/copy-back checks with
871 confirmed compacting collections inside the native call. Wrong String
storage and unsupported named array options are rejected. Existing explicit/
sequential/overlapping/pack/size/charset layout checks and all their rejection
cases pass. Bash/JSON syntax and whitespace checks pass.

The WPF command retains the complete DirectWrite runtime proof (85 types,
283 bodies, 81 font families). The original printing combined source still
passes parsing/Sema, gets past PrinterDefaults/GdiDocInfoW field annotations,
and fails next in declaration validation with `nested/generic delegate
declarations are not enabled yet`. The original PrintSystemObject declares
nested property-change/instantiation delegates, which require the next compiler
step. Full original printing field emission, complete IL/native linking and
printing runtime remain unproven; no System.Printing DLL is produced.

Logs, manifests, compiler work files and actual runtime artifacts are archived
locally under `tmp/wpf8-support/validation/printing-field-marshalling-20261013`.
Diagnostic scope retains all 49 original project inputs/66 tree C++ files, with
no per-file compile count. Native text coverage remains 58/139. Full text/layout,
printing and WindowsDesktop SDK/reference/runtime packaging remain required;
no completed WPF package is produced and the full goal stays active.


## 71. Nested delegates and metadata-derived automatic event signatures

The 2026-10-13 continuation emits source delegates nested in ordinary ref-class
owners using the existing record nesting model. The delegate class uses its
local name inside the actual owner block, with nested public/private/family
visibility. References and method bindings retain the full CLR owner path.
Deeper ref-class nesting is supported; generic delegates and generic/value/
native owners remain rejected. Delegate runtime constructor/Invoke slots remain
unchanged. Imported nested delegate binding remains unverified.

The fixture verifies two same-named delegates under different owner depths,
actual DeclaringType/MulticastDelegate identity, public/private/protected access,
source and C# static/instance/private/byref calls and target retention after
compacting GC. Source private type access is rejected. The first fixture used an
unqualified instance method address, which ordinary Clang rejects; using the
required qualified address fixes it. Original global/imported/virtual/native
callback and multicast delegate checks still pass.

The printing gate then exposed an automatic-event signature problem: imported
Invoke metadata rendered Object parameters as C++ surrogate stars in newly
generated source. The unchanged native-pointer validator correctly rejected
those stars. CliType metadata now includes source spelling derived from its
actual reference/array/byref/generic kind, preserving caret/percent sigils and
native pointer spelling. Automatic event generation uses that spelling while
metadata facades and actual CLR signatures retain their previous Cpp/Il forms.
A real imported EventHandler automatic event now validates C# subscription,
source raise, exact sender/EventArgs identity and removal. Existing automatic/
custom/virtual/byref/imported event checks and access rejection checks pass.

Validation on win11 with the standard compiler/WPF environment:

```sh
bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh build test-nested-delegates test-delegates
bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh test-nested-delegates test-delegates
bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh build test-nested-delegates test-events
bash ~/wpfboot/driver/bootstrap/m612/wpf.sh build-directwrite check-directwrite-assembly printing-gate
```

The initial build exposes the fixture address qualification noted above. Both
subsequent focused suites pass. The final WPF command passes DirectWrite's
85 types, 283 prepared bodies and 81-font-family runtime scope. Printing passes
Clang parsing/Sema and advances past nested delegates and generated event
signatures. It fails next with `returning tracking references is not enabled
yet`; the original GeometryProxy.GetGeometryData declaration/body returns its
managed _data field by tracking reference and needs that lowering. No reference
return restriction was silently removed and no complete printing IL/DLL is
produced.

Bash/JSON syntax and whitespace checks pass. Logs, manifests and actual runtime
artifacts are archived locally under
`tmp/wpf8-support/validation/printing-nested-delegates-20261013`. Scope remains
all 49 original project inputs/66 tree C++ files as a combined diagnostic gate,
without a per-file compile count. Native coverage stays 58/139. Full printing
lowering/link/runtime, full text/layout and WindowsDesktop SDK/reference/runtime
packaging remain required. No completed WPF package is produced; the full goal
stays active.


## 72. Managed heap tracking-reference returns

The 2026-10-13 continuation implements source percent return declarations with
actual CLR byref metadata. Return bodies require an exact referent type and a
direct managed heap location: instance/static fields, nested value fields rooted
in a managed owner, or managed array elements. Stack addresses, forwarded byref
parameters and local aliases remain rejected; native ampersand returns still
fail explicitly. This is a bounded heap-location rule, not full escape analysis.
Ordinary source calls load the referred value, while alias bindings and value
field receivers retain the returned address. Protected returns store the byref
through finally cleanup.

The initial fixture produced a DLL but failed at its array return: a generic
reference-call branch bypassed the managed-array intrinsic and emitted ldelem
instead of ldelema. Address dispatch now handles array/interior intrinsics first,
and ordinary byref loads apply only to source tracking returns, imported methods
and the existing typed-box/tracking-temporary markers. Native reference facades
keep their prior behavior. The arrays regression fixture also now spells its
ToCharArray result as System::Char, matching the canonical Windows Char mapping
introduced in section 64.

The final consumer prepares managed bodies and mutates returned scalar, nested
value, static, managed-handle and array locations. It checks source reads and
writes through returned value fields, out-of-line percent definitions, finally
execution, a detached heap owner retained only by its returned byref, and twenty
forced compacting collections. Null and out-of-range array returns raise the
actual CLR exceptions. Existing tracking parameters/temporaries, interior
indexing and typed-box paths pass their runtime and rejection checks.

Validation on win11 with the standard compiler/WPF environment:

```sh
bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh build test-tracking-returns test-tracking test-tracking-temporaries
bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh test-tracking-returns test-arrays test-interior-indexing test-scalar-boxes
bash ~/wpfboot/driver/bootstrap/m612/wpf.sh build-directwrite check-directwrite-assembly printing-gate
```

Both focused compiler commands pass. DirectWrite passes its existing 85-type,
283-prepared-body and 81-font-family scope. The printing gate passes Clang
parsing/Sema and advances beyond the original GeometryProxy.GetGeometryData
tracking return. Its next failure is the unbound declared DllImport method
PresentationNativeUnsafeNativeMethods::IsStartXpsPrintJobSupportedImpl in
InteropWin32ApiThunk.hpp:313. Managed source DllImport declaration support and
its native implementations need further work; no full printing DLL is produced.

Evidence is archived under
`tmp/wpf8-support/validation/printing-tracking-returns-20261013`. All 49 original
printing project inputs/66 tree C++ files remain in the combined diagnostic gate,
without a per-file compile count. Native text coverage remains 58/139. Complete
printing lowering/link/runtime, full layout/text and WindowsDesktop SDK/reference/
runtime packaging remain required. The WPF packaging goal stays active.


## 73. Source DllImport declarations and native marshalling

The 2026-10-13 continuation preserves the original managed DllImport declarations
instead of dropping their attributes and treating them as bodyless managed
methods. The adapter converts each supported declaration into typed Clang
annotations. The emitter validates static, bodyless, non-variadic ref-class
methods and emits actual pinvokeimpl metadata under the declared owner/access.
Library and entry-point strings are checked; omitted EntryPoint uses the source
method name. Winapi/Cdecl/StdCall, Unicode/ANSI/Auto charset, ExactSpelling,
SetLastError, BestFitMapping and ThrowOnUnmappableChar become CLR import flags.
Existing parameter In/Out/MarshalAs descriptors remain attached. PreserveSig
is retained; explicitly false PreserveSig and managed byref returns are rejected.
Unknown/duplicate options, invalid charset and unsupported declaration targets
fail rather than silently changing native ABI behavior.

The native fixture is compiled separately by trusted MinGW Clang. Reflection
checks import flags and private method visibility. Actual native calls validate
Unicode and surrogate pairs, null strings, default BOOL marshalling, byref output,
LPArray In/Out mutation, a real SafeWaitHandle, Unicode StringBuilder copy-back,
ANSI strings and cached Windows last-error. Source methods call their private
and public imports. All checks repeat across twenty compacting collections.
Existing COM, LPArray and field-marshalling suites pass, including native
PRINTER_DEFAULTSW/DOCINFOW layout and concurrent GC checks.

Validation on win11 with the standard compiler/WPF environment:

```sh
bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh build
bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh test-dllimport
bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh test-dllimport test-com test-array-marshal test-field-marshalling
bash ~/wpfboot/driver/bootstrap/m612/wpf.sh printing-gate
```

Compiler rebuild and focused runtime/rejection suites pass. Printing passes
Clang parsing/Sema and advances beyond the original PresentationNative declared
import calls. Its next failure is `unsupported local declaration` at
PrintQueue.cpp:3014: IsXpsOMPrintingSupported caches a dynamic initialization in
a static bool local. That requires persistent storage and initialization semantics,
not method-entry zeroed locals. The native libraries/exports declared by the
printing imports still require complete build/link/runtime verification; emitting
those imports alone does not provide their implementations.

Bash/JSON syntax and git whitespace checks pass. Evidence is archived under
`tmp/wpf8-support/validation/printing-dllimport-20261013`. Printing retains all
49 original project inputs/66 tree C++ files in its combined diagnostic scope;
no per-file compile count or complete System.Printing DLL is claimed. Native
text coverage stays 58/139. Full printing/native text/layout and WindowsDesktop
SDK/reference/runtime packaging remain unfinished; the goal stays active.


## 74. Persistent static-local storage and initialization

The 2026-10-13 continuation gives ordinary static locals private CLR holder
fields, including a lazily created lock and initialization flag. Declaration
execution acquires Monitor, evaluates the initializer only on the first reaching
call, stores the persistent value, and releases Monitor in a CLR finally handler.
An initializer exception leaves the flag clear so a later call can retry. The
initializer retains its actual source receiver/argument context; it is not moved
into a type initializer. Distinct lexical declarations receive distinct holders.
Reads, writes and addresses use the holder field; eligible direct static-local
tracking returns retain actual CLR heap byrefs. Zero-initialized declarations
preserve subsequent mutations across calls.

The fixture checks sixteen simultaneous first callers and exactly one initializer,
first argument and first receiver caching, exceptions followed by retry from a
different thread, distinct scopes, zero/persistent scalar storage, managed String
identity and a value containing managed references. Returned static field/value
byrefs survive twenty forced compacting collections. Existing exception and
tracking-return suites pass. TLS, static tracking aliases, native arrays and
storage requiring deterministic native cleanup are rejected. General recursive
initializer behavior remains unsupported. Monitor metadata must be explicitly
imported; each declaration currently acquires its lock even after initialization.

Validation on win11 with the standard compiler/WPF environment:

```sh
bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh build
bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh test-static-locals
bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh test-static-locals test-tracking-returns test-exceptions
bash ~/wpfboot/driver/bootstrap/m612/wpf.sh build-directwrite check-directwrite-assembly printing-gate
bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh test-static-locals
```

The compiler rebuild and focused suites pass, including the final cross-thread
retry consumer. DirectWrite retains its 85-type/283-prepared-body/81-font-family
runtime proof. Printing passes the original IsXpsOMPrintingSupported static
local and advances to PrintQueue.cpp:3398, where an if condition declares its
String handle. The next failure is `if initializer is not supported yet`; the
combined printing gate still does not produce a full IL/DLL.

Bash/JSON syntax and whitespace checks pass. Evidence is archived under
`tmp/wpf8-support/validation/printing-static-locals-20261013`. Scope remains all
49 original project inputs/66 tree C++ files in the combined diagnostic gate,
without a per-file pass count. Native coverage stays 58/139; full printing,
layout/text and WindowsDesktop SDK/reference/runtime packaging remain required.
The WPF packaging goal stays active.


## 75. If declarations and original __try/__finally cleanup

The 2026-10-13 continuation handles the original PrintQueue condition declaration
by emitting the IfStmt init statement and condition-variable declaration before
testing Clang's resolved condition. Existing typed local allocation/initialization
and Clang lexical scope rules remain in use. The consumer checks String handle
identity, null and empty strings, scalar true/else bindings, single initializer
evaluation, nested shadowing, C++17 initializer order/else visibility, repeated
loop initialization and return through finally. Out-of-scope references fail Sema.

Printing first advances to PrintSystemAttributeValueFactory.cpp:78, whose
original Monitor cleanup uses __try/__finally. Clang retains this form as an
SEHTryStmt rather than a CXXTryStmt. The emitter now writes a real CLR try/finally,
maintains protected-return/loop-control depth, and maps __leave to the innermost
try completion label. Managed exception filters are not substituted for native
__except: those remain rejected. The implementation provides CLR cleanup
semantics; native hardware-fault handling is not validated or claimed.

The second consumer checks return, __leave, break, continue and nested finally
execution. A managed exception retains its type/message while cleanup releases
an actual Monitor lock; a different thread proves the lock is available. A field
byref returned through __finally remains usable after compacting GC. Attempts to
return from a finally handler and native __except filters fail. Existing managed
exception tests still pass. General __leave inside finally, while/for/switch
condition declarations and broader SEH semantics remain unfinished.

Validation on win11 with the standard compiler/WPF environment:

```sh
bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh build test-if-declarations test-exceptions
bash ~/wpfboot/driver/bootstrap/m612/wpf.sh printing-gate
bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh build test-seh-finally test-if-declarations test-exceptions
bash ~/wpfboot/driver/bootstrap/m612/wpf.sh printing-gate
```

Both focused rebuild/runtime/rejection commands pass. The first original printing
gate fails at SEHTryStmt after passing the condition declaration. The second
passes that cleanup form and fails next with `unsupported CLI type: class
System::Printing::IndexedProperties::PrintStringProperty`. Further lowering is
required; no full System.Printing IL/DLL or printing runtime proof is produced.

Bash/JSON syntax and whitespace checks pass. Source fixtures, emitted IL/DLLs,
consumers, logs and original gate manifests are archived under
`tmp/wpf8-support/validation/printing-control-20261013`. Diagnostic scope still
covers all 49 original project inputs/66 tree C++ files as a combined gate,
without a per-file compile count. Native coverage remains 58/139. Full printing,
text/layout and WindowsDesktop SDK/reference/runtime packaging remain required;
the WPF packaging goal stays active.


## 76. Tracking property receiver and null capture storage

The 2026-10-13 continuation reproduces the printing PrintStringProperty failure
with a small source ref-class tracking-reference receiver. Clang's property
semantics capture its referent as a plain record expression; expression_type had
tried to emit that as a C++ value type. Expression storage now uses the existing
reference_element_type mapping, retaining the actual CLR class object. Typed
native managed-value pointers keep their explicit pointer storage path. This does
not enable general source ref-class values or remove declaration restrictions.

The consumer checks getter/setter identity and single evaluation through percent
parameters, actual CLR byref metadata, and twenty compacting collections. A
setter RHS replaces the same handle slot passed as the tracking receiver; the
original captured object receives the setter while the replacement stays empty.
Null getter/setter values remain correct. Existing properties and tracking
constructor tests pass.

The first full gate passes the class capture and reaches unsupported nullptr_t
storage. Unconverted property null captures now use object locals containing
managed null; subsequent native null conversions emit typed zero independently.
The extended consumer checks original source nullptr assignments to both managed
Object and native void-pointer properties and verifies each setter executes once.
An attempted assignment-result test was rejected by Clang because these property
assignments have void result; the final fixture checks standalone setter effects.
No assignment-result semantics or bypass of that Sema restriction is claimed.

Validation on win11 with the standard compiler/WPF environment:

```sh
bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh build test-tracking-properties test-properties test-constructor-tracking
bash ~/wpfboot/driver/bootstrap/m612/wpf.sh printing-gate
bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh build test-tracking-properties test-properties test-constructor-tracking
bash ~/wpfboot/driver/bootstrap/m612/wpf.sh printing-gate
```

The final compiler rebuild and all focused runtime/rejection checks pass. The
original printing gate passes Clang parsing/Sema and advances beyond class and
null property captures. Its next failure is unsupported OffsetOfExpr at
InteropPrinterInfo.cpp:895. Native offsetof layout constants need lowering; a
complete printing IL/DLL and printing runtime proof remain unfinished.

Bash/JSON syntax and whitespace checks pass. Evidence is archived under
`tmp/wpf8-support/validation/printing-tracking-properties-20261013`. Scope stays
49 original project inputs/66 tree C++ files in a combined diagnostic gate,
without a per-file pass count. Native coverage remains 58/139; full printing,
layout/text and WindowsDesktop SDK/reference/runtime packaging remain required.
The WPF packaging goal stays active.


## 77. Native offsetof layout constants

The 2026-10-13 continuation emits Clang OffsetOfExpr constants for trivial native
POD records. Clang's checked record/field/index designator and side-effect-free
integer constant evaluation determine the value; the emitter preserves its
32/64-bit integer representation. Managed facade layouts are rejected instead
of using their surrogate native size. Dynamic offsets remain rejected.

The consumer passes offsets for actual PRINTER_INFO_2W printer-name, security
and status fields, a nested inline-array element field, and packed pointer/scalar
fields to a separately compiled native DLL. That oracle compares them against
actual native object/member address differences. All six locations agree across
twenty compacting collections. Managed value offsetof and a nonconstant array
index fail the compiler guards.

Validation on win11 with the standard compiler/WPF environment:

```sh
bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh build test-native-offsets
bash ~/wpfboot/driver/bootstrap/m612/wpf.sh printing-gate
```

The focused rebuild/native runtime/rejection command passes. Printing passes
Clang parsing/Sema, advances beyond InteropPrinterInfo.cpp:895 and its original
PRINTER_INFO_2W offsetof expressions, and fails next with unsupported CLI type
MS::Internal::PrintWin32Thunk::AttributeNameToInfoLevelMapping::InfoAttributeData.
The original InteropInfoLevelProfile.hpp defines this as a private nested ref
struct, used in managed attribute-coverage arrays. Its remaining expression/type
lowering needs investigation; no complete printing IL/DLL is produced.

Bash/JSON syntax and whitespace checks pass. Logs, manifests, IL/DLLs and the
native oracle are archived under
`tmp/wpf8-support/validation/printing-native-offsets-20261013`. Printing still
covers all 49 original project inputs/66 tree C++ files in a combined diagnostic
gate, without a per-file compile count. Native coverage remains 58/139. Full
printing/native text/layout and WindowsDesktop SDK/reference/runtime packaging
remain required. The WPF packaging goal stays active.


## 78. Source ref-class locals and unary percent identity

The 2026-10-13 continuation adds function/source location context to body-lowering
errors. It identifies the InfoAttributeData failure in
DriverThunkingProfile.RegisterAttributeMap (GenericDriverThunkFilter.cpp:62).
The original method declares InfoAttributeData infoData(...) and stores
(Object^)(%infoData) in a Hashtable. InteropInfoLevelProfile.hpp defines this as
a private namespace ref struct, not a CLR value type or a type nested in another
class; the earlier section 77 description of a nested ref struct was imprecise.

Ordinary non-disposable source ref-class/ref-struct locals now use CLR class
locals, with newobj invoking the actual source constructor. Member access and
tracking binding reuse existing typed field/property/address lowering. Unary
percent returns that same reference object rather than boxing a copy. The marker
facade accepts a const reference so Clang does not create a fake native copy
before the emitter sees the operand; managed value-class operands still emit
actual CLR copy boxing. Copy/move construction and deterministic disposal remain
explicitly rejected until their complete semantics are implemented.

The consumer reproduces a private ref struct, its enum/bool constructor, original
Hashtable/Add/Object percent escape, and post-insertion mutation. Reflection
checks the real CLR class identity, private visibility and mutated enum value.
Additional source ref-class locals check fields, properties, instance methods,
constructor GC, exact tracking parameters and escaped String/object identity over
twenty compacting collections. The fixture initially exposed a separate local-
constructor String literal adaptation gap; the final test passes an actual String
local to that constructor. That contextual literal gap remains open. Existing
boxed-value, implicit boxing and tracking-temporary suites pass.

Validation on win11 with the standard compiler/WPF environment:

```sh
bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh build
bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh test-ref-locals test-boxed-values
bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh test-ref-locals test-implicit-boxing test-tracking-temporaries
bash ~/wpfboot/driver/bootstrap/m612/wpf.sh build-directwrite check-directwrite-assembly printing-gate
```

The focused rebuild/runtime/rejection suites pass. DirectWrite retains its
85-type/283-prepared-body/81-font-family runtime proof. Printing passes Clang
parsing/Sema and the original InfoAttributeData local/percent body; it next fails
at wcslen in PrinterThunkHandler.ThunkIsMetroDriverEnabled,
InteropPrinterHandler.cpp:3104. That actual CRT symbol needs explicit native
binding. No complete printing IL/DLL or printing runtime proof is claimed.

Bash/JSON syntax and whitespace checks pass. Evidence is archived under
`tmp/wpf8-support/validation/printing-ref-locals-20261013`. All 49 original project
inputs/66 tree C++ files remain in the combined diagnostic gate, without a
per-file pass count. Native coverage stays 58/139. Full printing lowering/link/
runtime, text/layout and WindowsDesktop SDK/reference/runtime packaging remain
required; the WPF packaging goal stays active.


## 79. Selective CRT wide-length and buffer-fill bindings

The 2026-10-13 continuation binds the original printing wcslen and memset calls
from the trusted x64 MinGW msvcrt import archive. The gate selects only those
symbols alongside its existing memcpy selection, preserving duplicate-symbol
checks instead of importing unrelated CRT exports that overlap system libraries.
No printing source bodies or CRT algorithms are replaced.

The focused consumer calls the actual native functions. wcslen checks UTF-16
and surrogate units, offsets, empty tails, embedded NUL and original-shaped
MULTI_SZ iteration. Pinned managed String characters remain valid through twenty
collections. memset checks the actual returned native pointer, low-byte value
truncation, bounded writes with sentinel bytes, zero count, zero fill and a
managed array pin across collection. All checks pass.

Validation on win11 with the standard compiler/WPF environment:

```sh
bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh test-crt-wide-length
bash ~/wpfboot/driver/bootstrap/m612/wpf.sh printing-gate
bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh test-crt-wide-length
bash ~/wpfboot/driver/bootstrap/m612/wpf.sh printing-gate
```

The first gate passes wcslen and reaches memset in SafeMemoryHandle.TryCreate.
The final focused native runtime command passes both bindings. Printing then
passes that fill call and fails in SafeMemoryHandle.get_Size at
InteropPrinterHandler.cpp:3233: the original handle.Size accesses IntPtr's static
property through an instance expression. That member-expression lowering needs
support. No full printing IL/DLL or printing runtime proof is produced.

Bash/Python/JSON syntax and whitespace checks pass. Logs, actual native-import
metadata, IL/DLL/runtime output and original gate manifests are archived under
`tmp/wpf8-support/validation/printing-crt-length-fill-20261013`. Scope remains all
49 original project inputs/66 tree C++ files in a combined diagnostic gate,
without a per-file compile count. Native text coverage stays 58/139; full printing,
layout/text and WindowsDesktop SDK/reference/runtime packaging remain required.
The WPF packaging goal stays active.


## 80. Static members read through instance syntax

The 2026-10-13 continuation handles the original SafeMemoryHandle.Size getter's
handle.Size expression. Metadata represents IntPtr.Size as a static property
facade, but Clang retains instance spelling as a MemberExpr with a static VarDecl.
The emitter now resolves that declaration through the existing static field/getter
path, evaluating side-effectful receiver expressions exactly once and discarding
their value. It does not create an instance getter or dereference a null reference
receiver just to access a static member.

The consumer checks source static fields/properties through receiver-producing
calls, counts evaluation across GC, accepts null reference receiver values, and
compares IntPtr.Size accessed through ordinary native-integer values and a call
receiver against the actual CLR IntPtr.Size. Existing static property/imported
access/readonly/visibility rejection checks pass. This extension covers reads;
instance-spelled address/store/compound forms remain unfinished.

Validation on win11 with the standard compiler/WPF environment:

```sh
bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh build test-static-member-receivers test-static-properties
bash ~/wpfboot/driver/bootstrap/m612/wpf.sh printing-gate
```

The focused rebuild/runtime/rejection command passes. Printing passes Clang
parsing/Sema and SafeMemoryHandle.get_Size, then fails in
PrinterInfoOneSafeMemoryHandle's base initializer (InteropPrinterHandler.cpp:3354).
That initializer calls a builder returning IntPtr. The remaining managed
constructor-initializer wrapper needs investigation; no full printing IL/DLL or
printing runtime proof is produced.

Bash/JSON syntax and whitespace checks pass. Source fixtures, emitted artifacts,
consumers, logs and original manifests are archived under
`tmp/wpf8-support/validation/printing-static-member-receivers-20261013`. All
49 original project inputs/66 tree C++ files remain in the combined diagnostic
gate, without a per-file pass count. Native coverage stays 58/139. Full printing,
layout/text and WindowsDesktop SDK/reference/runtime packaging remain required;
the WPF packaging goal stays active.


## 81. Wrapped base initializers, do loops and scalar globals

The 2026-10-13 continuation resolves the selected managed base constructor after
stripping Clang cleanup/temporary/parenthesis wrappers, reusing existing exact
argument lowering. The source IntPtr builder consumer validates single argument
evaluation, base/member/body order, GC and exception propagation. Tracking
constructor regressions pass. Printing advances past
PrinterInfoOneSafeMemoryHandle's original base initializer.

Its next failure is CopyFileStreamToPrinter's DoStmt. Do/while now emits a body-
first loop, condition-targeted continue and protected-region-aware break/return.
Tests check a false initial condition still executes once, exact condition counts,
nested loops and finally cleanup across continue, break and return. Existing
exception tests pass. Printing advances to AreClosePixel's PixelEpsilon global.

Nonvolatile file/namespace const arithmetic/enum reads now emit side-effect-free
Clang-evaluated scalar constants. Signed/unsigned widening preserves the declared
integer representation; floating values use round-trip IL text. The first scalar
consumer finds that -0 text loses its sign in ilasm; explicit -0.0 fixes that.
The final test compares exact pixel-threshold bits, negative zero, the smallest
double subnormal, unsigned maximum/narrow values, bool/signed/float values and
threshold boundary behavior. Mutable, volatile and dynamic global reads remain
rejected. This does not implement writable/addressable/aggregate global storage.

Validation on win11 with the standard compiler/WPF environment:

```sh
bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh build test-base-value-initializers test-constructor-tracking
bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh build test-do-loops test-exceptions test-base-value-initializers
bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh build test-global-scalars
bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh test-global-scalars test-do-loops test-base-value-initializers
bash ~/wpfboot/driver/bootstrap/m612/wpf.sh printing-gate
```

Final focused rebuild/runtime/rejection commands pass. Original printing gate
iterations advance through the base initializer, stream-copy loop and scalar
constant. The latest passes Clang parsing/Sema and fails at TransformBounds's
delete [] vertex (utils.cpp:251). vertex is a managed Point array, so its portable
source treatment needs review; the compiler's managed array-delete restriction
has not been silently removed. No complete printing IL/DLL is produced.

Bash/JSON syntax and whitespace checks pass. Evidence is archived under
`tmp/wpf8-support/validation/printing-initializers-loops-scalars-20261013`.
Diagnostic scope remains 49 original project inputs/66 tree C++ files, without a
per-file compile count. Native coverage stays 58/139. Full printing, layout/text
and WindowsDesktop SDK/reference/runtime packaging remain required; the WPF
packaging goal stays active.


## 82. Original printing bounds and managed-value property locations

The 2026-10-13 continuation reviews TransformBounds's delete [] vertex. vertex
is a CLR Point array. ECMA-372 section 15.4.5 forbids the array form of delete on
a handle type:
https://www.ecma-international.org/wp-content/uploads/ECMA-372_1st_edition_december_2005.pdf
The private printing source port now removes that obsolete deletion and records
CLR ownership. Managed array-delete rejection stays in the compiler and has an
explicit negative test. The original source checkout remains unchanged.

A new focused WPF step extracts the actual patched TransformBounds body and
its original MATRIX_EPSILON definition, imports the source-built WindowsBase,
and executes identity/scale/reflection/shear inputs against an independent C#
corner transformation. The first fixture needed explicit Double metadata. Its
first runtime run then exposed a real compiler bug: identity returned bounds
0,0,0,0 instead of 2,3,6,6. Managed value property receivers were cached as value
copies, so Point array setters and Rect byref setters did not update storage.

Clang lvalue opaque captures for managed values now retain CLR byref locations.
Receiver use loads the captured address, while ordinary expression consumption
loads its value. Readonly defensive-copy behavior uses the existing receiver
path. Ref parameters, array elements and nested value fields keep their original
locations and CLR GC roots. The consumer checks fields/properties containing
managed String references, index evaluation once, setter GC and by-value copy
isolation. Existing ordinary/ref-class property regressions pass.

Validation on win11 with the standard compiler/WPF environment:

```sh
bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh build
bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh test-value-property-locations test-properties test-tracking-properties
bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh test-value-property-locations
bash ~/wpfboot/driver/bootstrap/m612/wpf.sh check-printing-bounds
bash ~/wpfboot/driver/bootstrap/m612/wpf.sh check-matrix-operators printing-gate
```

Final focused runtime/rejection commands pass, as does the original printing
TransformBounds consumer with all four matrix cases. Existing Matrix operator
runtime checks pass. Printing passes Clang parsing/Sema and advances beyond
TransformBounds; it next fails in CGDIDevice.Polygon (gdidevice.cpp:106), whose
pin_ptr<PointI> element is a managed value type. Extending GC-free value pinning
and validating native layout remain required. No full printing IL/DLL is produced.

Bash/Python/JSON syntax and whitespace checks pass. Original body, source patch,
emitted artifacts, baseline failure and passing runtime logs are archived under
`tmp/wpf8-support/validation/printing-value-properties-20261013`. Printing still
covers 49 original project inputs/66 tree C++ files as a combined diagnostic gate,
without per-file compile counts. Native text coverage stays 58/139. Full printing,
layout/text and WindowsDesktop SDK/reference/runtime packaging remain required;
the WPF packaging goal stays active.


## 83. GC-free managed value pinning and CLR strides

The 2026-10-13 continuation accepts managed value pin/interior elements only when
native_storage proves they contain no GC references. Indexed addresses and pointer
steps use CLR sizeof for these elements, preserving explicit CLR layouts instead
of the surrogate C++ size. Implicit/explicit pin conversion retains native value
pointer provenance and checks qualifiers. Managed value arrow receivers reuse
the actual tracked address.

The initial consumer DLL fails with an internal CLR error: the arrow receiver
path unboxes a value that is already a managed byref. The corrected receiver
recognizes the trusted interior-address marker and preserves that address without
boxing. The native oracle initially needed its missing cstddef include; subsequent
native compilation and runtime checks pass. An old rejection assertion was updated
for the expanded diagnostic; its invalid GC-reference element remains rejected.

The oracle reads/writes real POINT-compatible two-int array elements and a 32-byte
explicit value with its second field at byte 20. Source pin indexing, increments,
implicit/explicit casts and interior indexing/updates preserve actual CLR strides.
The final consumer repeats both native calls twenty times with a deliberate native
pause and a background compacting collector: **4,382 concurrent collections** are
confirmed. Sentinel elements, mutated values and tracked interior writes remain
correct. Source values containing managed Strings are rejected. Existing primitive
indexing and native POD/packed/array/qualifier/escape regression checks pass.

Validation on win11 with the standard compiler/WPF environment:

```sh
bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh build test-value-pins
bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh test-value-pins test-interior-indexing test-interior-records
bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh test-interior-records
bash ~/wpfboot/driver/bootstrap/m612/wpf.sh printing-gate
```

The corrected focused runtime and final rejection suites pass. Original printing
passes Clang parsing/Sema and advances beyond its PointI pin sites to
CGDIPath.ProcessCurve (gdipath.cpp:815), whose forward goto is unsupported.
Native layout compatibility remains a per-boundary obligation; accepting a
GC-free value does not imply arbitrary native ABI compatibility. No complete
printing IL/DLL or printing runtime proof is produced.

Bash/JSON syntax and whitespace checks pass. Source fixtures, native oracle,
emitted IL/DLLs and runtime/gate evidence are archived under
`tmp/wpf8-support/validation/printing-value-pins-20261013`. Scope remains all
49 original project inputs/66 tree C++ files as a combined diagnostic gate,
without per-file compile counts. Native coverage stays 58/139; full printing,
layout/text and WindowsDesktop SDK/reference/runtime packaging remain required.
The WPF packaging goal stays active.


## 84. Goto regions and floating-point truth conversion

The 2026-10-13 continuation adds named labels and direct goto lowering. A source
prepass records each label's protected/pinned region path. Same-region jumps use
br; exits to an ancestor region use leave so CLR cleanup executes. Jumps into
protected or pinned regions, sibling regions, and exits from finally handlers
are rejected. Named branches inside the same finally remain valid. Computed
gotos are not implemented.

The consumer checks original-shaped mixed-data classification, backward loops,
nested finally unwinding, pin cleanup and same-finally branches. The first pin-
entry negative fixture put its declaration directly under a label and hit the
existing pin-local restriction first; separating the label's empty statement from
the declaration reaches the intended region-entry rejection. Final negative and
existing exception/SEH-finally runtime suites pass.

Printing advances beyond CGDIPath.ProcessCurve and reaches a FloatingToBoolean
conversion in CanConvertDashes. Float32/64 truth conversion now compares against
matching floating zero and negates equality, making both signed zeros false and
finite nonzero, subnormal, infinity and NaN true. Logical not and conditions reuse
that conversion. The consumer verifies both widths and operand evaluation once.

Validation on win11 with the standard compiler/WPF environment:

```sh
bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh build
bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh test-gotos test-exceptions test-seh-finally
bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh build test-floating-bools test-gotos
bash ~/wpfboot/driver/bootstrap/m612/wpf.sh printing-gate
```

Focused rebuild/runtime/rejection commands pass. Original printing passes Clang
parsing/Sema, the forward goto and dash-helper float conversion. It next fails
at FillLinearGradient (gdirt.cpp:1588), which uses sizeof on the managed TriVertex
value type. That requires CLR layout sizing rather than the surrogate C++ size.
No complete printing IL/DLL or printing runtime proof is produced.

Bash/JSON syntax and whitespace checks pass. Artifacts and runtime/gate evidence
are archived under
`tmp/wpf8-support/validation/printing-gotos-floating-bools-20261013`. Diagnostic
scope remains all 49 original project inputs/66 tree C++ files, without per-file
compile counts. Native coverage stays 58/139; full printing, layout/text and
WindowsDesktop SDK/reference/runtime packaging remain required. The WPF
packaging goal stays active.


## 85. CLR managed-value sizeof

The 2026-10-13 continuation emits CLR sizeof for managed value types, followed
by unsigned widening when the source expression has 64-bit size_t type. The
operand remains unevaluated. This uses actual runtime layout rather than the
native surrogate's fields, alignment, size or interface bases. Managed ref-class
sizeof and managed record alignof remain rejected.

The consumer compares sequential, explicit 32-byte and reference-containing
value layouts against source-built Unsafe.SizeOf. A builder call inside sizeof
must not execute, and multiplied size_t values retain their width. All checks
pass. Existing native sizeof/alignof and actual function-pointer ABI tests pass;
the previous sizeof rejection now targets a ref class, while a dedicated managed
alignof rejection remains.

Validation on win11 with the standard compiler/WPF environment:

```sh
bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh build test-managed-sizeof test-function-pointers
bash ~/wpfboot/driver/bootstrap/m612/wpf.sh printing-gate
```

The focused rebuild/runtime/rejection command passes, with the expected upstream
warning for sizeof's unevaluated increment fixture. Printing passes Clang parsing/
Sema and the original TriVertex sizing in FillLinearGradient. It next fails at
RenderGlyphRun (gditextrt.cpp:164), where an existing interior_ptr local is assigned
a newly constructed view. Interior-pointer assignment lowering remains required;
no complete printing IL/DLL or printing runtime proof is produced.

Bash/JSON syntax and whitespace checks pass. Evidence is archived under
`tmp/wpf8-support/validation/printing-managed-sizeof-20261013`. Scope remains all
49 original project inputs/66 tree C++ files in the combined diagnostic gate,
without per-file compile counts. Native text coverage stays 58/139; full printing,
layout/text and WindowsDesktop SDK/reference/runtime packaging remain required.
The WPF packaging goal stays active.


## 86. Interior pointer assignment and tracked rebinding

The 2026-10-13 continuation handles interior pointer copy assignment through
Clang's selected operator. Local/parameter pointer slots store the existing CLR
byref value and retain the assignment expression result; no referent copy or
native pointer conversion is introduced. Pin-variable assignment retains its
previous dedicated block-local handling.

The consumer checks rebinding a local from one array to another, retaining an
independent alias to the first array, and reassigning a copied parameter without
changing the caller's pointer. Mutations after GC reach the correct arrays. The
original RenderGlyphRun-shaped Char-array to ushort interior view assignment
retains its location across GC. Existing interior indexing/stride/update/const
rejection checks pass.

Validation on win11 with the standard compiler/WPF environment:

```sh
bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh build test-interior-assignment test-interior-indexing
bash ~/wpfboot/driver/bootstrap/m612/wpf.sh printing-gate
```

The focused rebuild/runtime/rejection command passes. Printing passes Clang
parsing/Sema and advances beyond RenderGlyphRun's interior assignments. It next
fails at CreateFontCached (gditextrt.cpp:791), where a native inline face-name
array reached through an interior record initializes a pin_ptr. Its Clang array-
to-pointer decay must retain provenance during constructor lowering. No complete
printing IL/DLL or printing runtime proof is produced.

Bash/JSON syntax and whitespace checks pass. Evidence is archived under
`tmp/wpf8-support/validation/printing-interior-assignment-20261013`. Scope remains
all 49 original project inputs/66 tree C++ files in the combined diagnostic gate,
without per-file compile counts. Native coverage stays 58/139; full printing,
layout/text and WindowsDesktop SDK/reference/runtime packaging remain required.
The WPF packaging goal stays active.


## 87. Inline native array pin initialization

The 2026-10-13 continuation preserves Clang's array-to-pointer decay when a
native inline array initializes a pin pointer. Constructor lowering checks the
original converted argument type and emits its containing-record location;
stripping implicit casts before that check had incorrectly rejected the array.
Existing qualification and interior-pointer provenance checks remain in place.

The focused fixture pins a wchar_t field of a native record stored in a managed
array. It mutates the original field across GC, copies a Unicode surrogate-pair
string, changes the source afterward and verifies independent string ownership.
Existing native-pin and native-interior-record runtime/rejection tests pass.

Validation on win11 with the standard compiler/WPF environment:

```sh
bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh build test-inline-array-pins test-native-pins test-interior-records
bash ~/wpfboot/driver/bootstrap/m612/wpf.sh printing-gate
```

The compiler rebuild and focused tests pass. The original combined printing gate
passes parsing/Sema and CreateFontCached's face-name pin; emission now stops in
CreateSimulatedStyleFont (gditextrt.cpp:882) at the local const BYTE[17] charset
array. Native local-array storage remains required. No complete printing DLL or
printing runtime proof is produced.

Evidence is archived under
`tmp/wpf8-support/validation/printing-inline-array-pins-20261013`. Scope remains
49 project inputs/66 tree C++ files, without per-file compile counts. Native text
coverage remains 58/139. Full printing, text/layout and WindowsDesktop packaging
remain required; the WPF packaging goal stays active.


## 88. Fixed native local array storage

The 2026-10-13 continuation adds fixed one-dimensional native local arrays.
Their opaque CLR value slots use Clang's byte size/alignment and native element
strides; pointer decay addresses the local rather than allocating a managed
array or copying storage. Explicit aggregate/string declarations zero their
storage on each execution before ordered element initialization. Declarations
without initializers retain existing method-entry /clr:initLocals semantics.
Supported elements are scalars, enums, native pointers and trivial native POD.
Managed-handle elements, multidimensional arrays and nontrivial construction
remain rejected. Static arrays and whole-array native fields remain unsupported.

A runtime fixture covers the original 17-byte charset-list shape, packed
five-byte records, pointer aliases, narrow/UTF-16 strings with surrogate pairs,
enum/floating elements, ordered side effects, zero-filled tails, loop declaration
reinitialization and aliases surviving GC. All runtime and rejection checks pass.
A negative test initially expected the storage-layout rejection for a trivially
copyable record with a user constructor; the correct rejection is unsupported
non-aggregate construction, and that assertion was corrected before completion.

Validation on win11 with the standard compiler/WPF environment:

```sh
bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh build test-native-local-arrays test-inline-array-pins
bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh test-native-local-arrays test-inline-array-pins
bash ~/wpfboot/driver/bootstrap/m612/wpf.sh printing-gate
```

The compiler rebuild succeeds; final focused runtime/rejection checks pass. The
combined original printing gate passes parsing/Sema and advances beyond the
charset array and subsequent font creation to TrueTypeFont::ReplaceFontName
(FontInfo.cpp:291), where the explicit (void)version discard is unsupported.
No complete printing IL/DLL or printing runtime proof is produced.

Evidence is archived under
`tmp/wpf8-support/validation/printing-native-local-arrays-20261013`. Scope remains
49 project inputs/66 tree C++ files without per-file compile counts; native text
coverage remains 58/139. Full printing, text/layout and WindowsDesktop packaging
remain required. The WPF packaging goal stays active.


## 89. Explicit void discard casts

The 2026-10-13 continuation emits Clang CK_ToVoid casts by evaluating the
operand and popping its nonvoid result. Void operands already leave an empty
stack. A focused runtime check covers the original (void)version idiom,
side-effectful calls, nested casts, native volatile fields, managed values,
native pointers, allocated handles and exceptions from discarded operands.
The rebuild and final runtime check pass. A comma-expression probe exposed an
independent existing binary-operator gap; comma lowering remains unsupported.

Validation on win11 with the standard compiler/WPF environment:

```sh
bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh build test-void-discard
bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh test-void-discard
bash ~/wpfboot/driver/bootstrap/m612/wpf.sh printing-gate
```

The original combined printing gate passes parsing/Sema and proceeds beyond
FontInfo.cpp to XpsDeviceSimulatingPrintThunkHandler::ThunkStartDocPrinter
(XpsDeviceSimulatingInteropPrinterHandler.cpp:118). The remaining diagnostic
is a missing native _wassert binding from assert. The gate currently omits the
upstream Release configuration's NDEBUG definition; configuration fidelity needs
checking before choosing debug CRT imports or Release preprocessing. No complete
printing IL/DLL or printing runtime proof is produced.

Evidence is archived under
`tmp/wpf8-support/validation/printing-void-discard-20261013`. Scope remains
49 project inputs/66 tree C++ files without per-file compile counts. Native text
coverage remains 58/139; full printing, text/layout and WindowsDesktop packaging
remain required. The WPF packaging goal stays active.


## 90. Printing Release configuration fidelity

The 2026-10-13 continuation aligns the original printing diagnostic gate with
upstream Release preprocessing. The driver verifies NDEBUG in the Release
ItemDefinitionGroup of eng/WpfArcadeSdk/tools/Wpf.Cpp.props before defining it,
and records the configuration and source props path in inputs.json. This
matches the managed WPF builds' Release configuration and removes the debug-only
assert/_wassert import requirement without changing original printing code.

Validation on win11 with the standard WPF environment:

```sh
bash ~/wpfboot/driver/bootstrap/m612/wpf.sh printing-gate
```

Python syntax and git whitespace checks pass. The gate passes parsing/Sema,
advances beyond ThunkStartDocPrinter's native assert and stops at XpsPrintStream::
Seek (XpsPrintStream.cpp:211). Native COM signature lowering currently rejects
the eight-byte LARGE_INTEGER by-value argument to IStream::Seek. Correct Windows
x64 aggregate argument lowering remains required. No complete printing IL/DLL or
printing runtime proof is produced.

Evidence is archived under
`tmp/wpf8-support/validation/printing-release-20261013`. Scope remains
49 project inputs/66 tree C++ files without per-file compile counts. Native text
coverage remains 58/139; full printing, text/layout and WindowsDesktop packaging
remain required. The WPF packaging goal stays active.


## 91. Native COM small aggregate arguments

The 2026-10-13 continuation lowers by-value native COM arguments with trivial
native layouts of 1, 2, 4 or 8 bytes. Each operand is evaluated once and copied
to an opaque CLR value temporary; its raw bits are loaded as the corresponding
unsigned integer argument for unmanaged calli. This follows the Windows x64 ABI,
including structs containing floats and native unions. Aggregate returns and
other-size/managed/nontrivial arguments remain unsupported. Native imported
function and native function-pointer signatures retain their existing limits.

ABI reference:
https://learn.microsoft.com/en-us/cpp/build/x64-calling-convention?view=msvc-170

The source-built native COM oracle verifies all four sizes, a float-containing
record passed in an integer slot, an eight-byte pair on the stack, mixed trailing
floating/integer arguments, exactly-once argument construction, copy isolation,
and a negative LARGE_INTEGER value wider than 32 bits across GC. Actual Windows
CreateStreamOnHGlobal/IStream::Seek/Release calls match an independent native
caller at position 1234567. The first real-stream fixture failed with a beyond-
4-GB offset; the corrected stream fixture uses an in-range offset while the COM
oracle retains the wide signed union check. Existing COM receiver/GUID/HRESULT/
output checks pass. The prior aggregate rejection now tests an unsupported
16-byte record rather than the newly supported four-byte case.

Validation on win11 with the standard compiler/WPF environment:

```sh
bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh build test-native-com-aggregates test-native-com
bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh test-native-com-aggregates test-native-com
bash ~/wpfboot/driver/bootstrap/m612/wpf.sh printing-gate
```

The compiler rebuild succeeds and final focused runtime/rejection checks pass.
The original combined printing gate passes parsing/Sema and both original
IStream::Seek call sites. It next fails in AttributeValueInteropHandler::
RegisterStaticMaps (InteropAttributeValueDictionary.cpp:65): native enum typeid
metadata is unsupported for PrintPropertyTypeInterop. No complete printing
IL/DLL or printing runtime proof is produced.

Evidence is archived under
`tmp/wpf8-support/validation/printing-native-com-aggregates-20261013`. Scope
remains 49 project inputs/66 tree C++ files without per-file compile counts.
Native text coverage remains 58/139; full printing, text/layout and
WindowsDesktop packaging remain required. The WPF packaging goal stays active.


## 92. Source nested managed enums and complete printing IL

The 2026-10-13 continuation recognizes scoped enum declarations directly inside
managed classes without requiring a namespace-level visibility prefix. It emits
their real nested CLR enum metadata inside the declaring type, preserving source
public/protected/private/internal access and slash-qualified type tokens. Flags
attributes also accept that nested declaration shape. Native scoped enums outside
managed members retain their native interpretation and typeid rejection.

A C# consumer verifies declaring types and deep nested paths, same-named enums in
different owners, ushort and wide signed/unsigned literal bits, Flags metadata,
method signatures, actual boxing/unboxing, Enum.GetValues/Parse, nonpublic access
flags and the original private printing-owner shape across GC. Existing top-level
enum metadata, imported reference and primitive static/native-shadow checks pass.
The initial fixture omitted its System.Type metadata selection; it was added
before the final passing tests. An enum access annotation was moved after the
scoped enum keyword to avoid an ignored-attribute warning.

Validation on win11 with the standard compiler/WPF environment:

```sh
bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh build test-nested-enums test-enums
bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh test-primitive-statics
bash ~/wpfboot/driver/bootstrap/m612/wpf.sh printing-gate
/clang64/bin/ilasm-dotnet.exe E:/msys64/home/topkek/wpfboot/build/printing-gate/System.Printing.il -dll -quiet -nologo -output=E:/msys64/home/topkek/wpfboot/build/printing-gate/System.Printing.dll
```

The compiler rebuild and focused runtime/rejection checks pass. Printing moves
past its method-body emission to assembly metadata. Selecting the existing
InternalsVisibleTo, AssemblyFileVersion and AssemblyInformationalVersion
attribute constructors completes the combined gate: it now emits full printing
IL from all 49 project inputs/66 tree C++ files. The next, stronger ilasm check
fails on an undefined cli.array<Byte> surrogate type referenced by array typeid
operands. Array record type tokens need actual CLR vector identities. No complete
printing DLL, CLR loading/JIT proof or printing runtime proof is produced yet.

Evidence is archived under
`tmp/wpf8-support/validation/printing-nested-enums-20261013`. Native text coverage
remains 58/139; full printing, text/layout and WindowsDesktop packaging remain
required. The WPF packaging goal stays active.


## 93. CLR array record tokens and complete printing DLL

The 2026-10-13 continuation maps array<T>::typeid record operands to the actual
CLR vector type. Handle aliases already took that path; record operands had
incorrectly emitted an undefined native C++ template surrogate. The focused C#
consumer now verifies byte arrays, source managed-value arrays and jagged String
arrays alongside the existing handle-array/generic/primitive/pointer/reliability
checks. The compiler rebuild and all runtime/rejection checks pass.

New WPF build-printing/check-printing-assembly steps assemble the original
combined project IL, stage System.Printing.dll in the runtime library directory
and validate versions, the original ReachFramework friend identity, every CLR
type, all eligible managed methods/constructors/module methods and the actual
nested printing enum. The check app includes the source-built WPF/out-of-band
managed dependencies and WPF native loader/subsetter/graphics/pen libraries.
Initial stronger checks exposed missing Packaging and native DWrite loader
fixtures; adding the real dependencies eliminates those cascaded failures.

Validation on win11 with the standard compiler/WPF environment:

```sh
bash ~/dotnetboot/repo/bootstrap/m612/cppcli.sh build test-type-tokens
bash ~/wpfboot/driver/bootstrap/m612/wpf.sh build-printing check-printing-assembly
bash ~/wpfboot/driver/bootstrap/m612/wpf.sh check-printing-assembly
```

The original printing project now produces a real DLL: **235 CLR types load and
2,017 method bodies prepare with zero failures**. Assembly/file/informational
versions and the original nested enum values pass. This is assembly/loading/JIT
evidence, not an end-to-end print job proof. Original property/spooler runtime
checks and implementation dependency integration are the next steps. Scope is
49 original project inputs/66 tree C++ files; no independent per-file-linking
claim is made.

Evidence is archived under
`tmp/wpf8-support/validation/printing-array-type-tokens-20261013`. Native text
coverage remains 58/139. Full text/layout, printing runtime and WindowsDesktop
SDK/reference/runtime packaging remain required; the WPF packaging goal stays
active.


## 94. Original printing runtime, XPS writing and implementation integration

The 2026-10-13 continuation adds check-printing-runtime to the complete assembly
check. Original PrintStringProperty/PrintInt32Property/PrintBooleanProperty/
PrintByteArrayProperty constructors, getters/setters and PrintPropertyDictionary
retain Unicode and typed payloads through updates, disposal and GC. Original
PrintServer/GetPrintQueues enumeration matches direct native EnumPrintersW at
level four with Local/Connections flags: one queue on the validation host. The
fixture queries printer state and writes a local XPS file; it sends no print job.

Original XpsDocument.CreateXpsDocumentWriter/Write produces a 120x80 vector
FixedPage. Package XML has the expected red fill, black two-unit stroke and
rectangle geometry. WPF opens the resulting XPS and returns one page with the
correct size. The source-built ConfigurationManager/ProtectedData dependencies
were added after a dispatcher initialization failure; STA entry and the actual
WPF managed/native dependencies are present in the runtime fixture.

The dependency graph now maps the original System.Printing reference project to
the source-built implementation DLL once it exists. Previously even a rebuild
continued selecting the bootstrap reference; inspecting generated HintPaths
exposed that, so the graph was corrected and both consumers rebuilt again.
ReachFramework (285 source inputs) and PresentationFramework (1,345 inputs) now
compile against build/lib/System.Printing.dll. Their final generated projects
and graph manifest confirm that identity. Cycle-only references for other
assembly cycles retain their existing separation from runtime libraries.

Validation on win11 with the standard WPF environment:

```sh
bash ~/wpfboot/driver/bootstrap/m612/wpf.sh check-printing-runtime
bash ~/wpfboot/driver/bootstrap/m612/wpf.sh build-reach-framework build-presentation-framework check-presentation-framework check-printing-runtime
```

The final commands pass. Complete printing CLR validation remains 235 loaded
types and 2,017 prepared bodies with zero failures. Original property and native
spooler checks and local XPS write/XML/readback pass. PresentationFramework
controls/dependency properties, XAML, source-generated BAML/named fields,
verified red compositor pixels and 3,248-type CLR load regressions pass after
implementation integration. The local XPS artifact is 1,853 bytes.

Evidence is archived under
`tmp/wpf8-support/validation/printing-runtime-integration-20261013`. Printing
scope remains 49 original project inputs/66 tree C++ files. This proves the
exercised runtime paths, not physical printing or every method's behavior.
Native text coverage remains 58/139; full text/layout and WindowsDesktop
SDK/reference/runtime packaging remain required. The WPF packaging goal stays
active.


## 95. Remaining managed shipping assemblies

The 2026-10-13 continuation builds ten additional implementation components
listed in upstream ShippingProjects.props: UIAutomationClient (76 planned C#
inputs), UIAutomationClientSideProviders (78), the five PresentationFramework-
System extensions (2/1/1/2/1), System.Windows.Presentation (2), Ribbon (131) and
WindowsFormsIntegration (24). This adds 318 planned C# inputs; generated markup
inputs remain additional. Real implementation assemblies go to build/lib.

The planner now reads upstream UseMicrosoftSharedKeyId to choose MSShared or
ECMA public signing identity instead of assuming MSShared for all but two
assemblies. The first extension build exposed the wrong friend-access key.
Resource helper namespaces follow the upstream Ribbon and WindowsFormsIntegration
namespaces; the latter's initial build exposed the missing System.Windows SR
mapping. Both corrections preserve the original sources and access checks.

The focused runtime consumer checks all ten identities/versions, loads 674 types
and prepares 6,426 bodies. Original automation queries the real native desktop
root; Ribbon loads its original compiled generic BAML and creates a default
visual template. Original framework extensions execute dynamic binding updates,
DataRowView binding and XML XPath binding. Original WindowsFormsHost/ElementHost
construct, retain children/property maps and dispose across GC. Forms integration
currently uses the approved source-built WinForms 7.0.20 interim runtime.

Validation on win11 with the standard WPF environment:

```sh
bash ~/wpfboot/driver/bootstrap/m612/wpf.sh build-shipping-managed
bash ~/wpfboot/driver/bootstrap/m612/wpf.sh build-windowsforms-integration
bash ~/wpfboot/driver/bootstrap/m612/wpf.sh check-shipping-managed
```

Nine components pass the final bulk build; the narrowed WindowsFormsIntegration
rebuild passes after its resource fix. The runtime command passes with zero
compiler warnings/errors. Initial fixture mistakes in PropertyMap count access
and repeated ApplyTemplate return interpretation were corrected to test Keys
and the actual visual tree. Python/Bash syntax and whitespace checks pass.

Evidence is archived under
`tmp/wpf8-support/validation/shipping-managed-20261013`. All listed managed WPF
shipping components now have source-built implementation artifacts. This does
not establish full native text/paragraph layout or WindowsDesktop packaging;
PresentationNative imported coverage remains 58/139. Framework pack/SDK layout,
package recipe/build and remaining native behavior are still required. The WPF
packaging goal stays active.


## 96. Source WindowsDesktop SDK and framework-pack layout

The 2026-10-13 continuation stages the source-built WPF implementations and the
approved interim WinForms 7 components into an isolated WindowsDesktop shared
framework, targeting pack, win-x64 runtime pack and SDK overlay. Its explicit
version is 8.0.0-bootstrap.wpf.8.0.0. Forty managed assemblies use recorded real
identities; available WPF C# reference outputs are used for targeting, while the
CLI and dependency assemblies use their actual implementation metadata. Bootstrap
cycle references never enter runtime directories. The original markup targets
and source-built PresentationBuildTasks/CodeDom/MetadataLoadContext are included.
Required companion WinForms SDK props/targets and its application generator are
retained. The first prototype omitted an analyzer props companion; the corrected
merge preserves upstream WPF files and adds the missing companion imports.

Five native WPF libraries are joined by their transitive trusted MSYS2 seed
runtime dependencies: libc++.dll, libicuuc78.dll and libicudt78.dll. PE import
inspection discovers these; pacman package ownership is checked and recorded
with SHA-256 hashes. Windows system libraries remain OS dependencies. The
source-bootstrap manifest records WPF/WinForms source versions, implementation
and reference hashes, remaining work and partial PresentationNative coverage.
This remains a partial native port and WinForms 7 interim layout, not a claim of
full upstream WindowsDesktop behavior or completed MSYS2 packaging.

Validation on win11 with the standard WPF environment:

```sh
bash ~/wpfboot/driver/bootstrap/m612/wpf.sh layout check-sdk
bash ~/wpfboot/driver/bootstrap/m612/wpf.sh check-sdk
```

The check clones the installed source SDK with hard links, unlinks its desktop
SDK overlay before merging and leaves the installed files unchanged. A normal
Microsoft.NET.Sdk project with net8.0-windows/UseWPF builds with an empty NuGet
feed/cache, generates MainWindow BAML/named fields and starts an actual window.
Shared framework resolution is checked against the explicit bootstrap directory,
and a PNG contains the expected red rectangle pixels. Framework implementations
are absent from the framework-dependent app output.

Ordinary dotnet publish -r win-x64 --self-contained true includes the framework
and native dependencies. The native apphost runs with DOTNET_ROOT/DOTNET_ROOT_X64
unset and PATH restricted to /usr/bin and Windows/System32. Its loaded framework
is verified app-local, and the same BAML/window/pixel checks pass. Both final
build/run and publish/run checks pass with zero build warnings/errors. Syntax
and whitespace checks pass.

Evidence is archived under
`tmp/wpf8-support/validation/desktop-layout-20261013`. The framework/SDK layout
is concrete and tested, but an MSYS2 recipe/clean package build, remaining native
text/paragraph/NLS behavior and full runtime coverage remain required. Imported
PresentationNative coverage stays 58/139. The WPF packaging goal stays active.


## 97. Compiler package refresh prerequisite

The 2026-10-13 continuation refreshes the C++/CLI package recipe for the WPF
compiler source and adds the missing installed include/vcclr.h intrinsic header.
The clean compiler build succeeds. The package check revealed that test-core
still expected do/while to be unsupported, although section 81 implemented it.
That stale negative fixture now uses explicitly unsupported native inline
assembly. The original do-loop runtime fixtures remain positive coverage.

Validation on win11 in the private cppcli-0.3.0 package work tree:

```sh
makepkg --noconfirm --force
makepkg --noextract --force --noconfirm
```

The first check stops at the obsolete do-loop rejection. After applying the
single fixture correction to the private extracted source for diagnosis, the
retry passes the normal core/object/property/virtual/array/exception/interop/
enum/value/tracking/adapter checks and the added nested enum/native local-array/
native COM aggregate/interior assignment/inline pin/type-token/managed-sizeof
fixtures. It creates a 0.3.0 candidate archive. This diagnostic candidate is not
a final pinned package: the recipe/source archive must be repinned to this
committed correction and built cleanly before installation or final packaging
claims. The tested SDK/shared/targeting/runtime layout from section 96 remains
unchanged. Full WPF distribution packaging and native text/layout are open.


## 98. Combined desktop analyzer metadata and profile regression

The 2026-10-13 continuation validates the approved WinForms interim app against
the new combined WPF desktop SDK, in addition to the WPF application. Its first
build exposed missing assembly/version/token attributes in the WinForms analyzer
FrameworkList entry: ResolveTargetingPackAssets threw NullReferenceException.
The layout now retains the original source-built analyzer descriptor from the
installed WinForms targeting pack. WPF filtering had previously hidden this
metadata defect. check-sdk now runs both profile consumers by default.

Validation on win11 with the standard WPF environment:

```sh
bash ~/wpfboot/driver/bootstrap/m612/wpf.sh layout check-sdk
bash ~/wpfboot/driver/bootstrap/m612/winforms-tests/check-sdk.sh ~/wpfboot/build/sdk-check-root ~/wpfboot/build/winforms-sdk-check
bash ~/wpfboot/driver/bootstrap/m612/wpf.sh check-sdk
```

The final combined gate passes WPF shared-framework/BAML/window/raster checks,
portable self-contained WPF publish/run with SDK roots/toolchain PATH removed,
and WinForms application-generation/window/control/resource/event/GDI+ checks in
both framework-dependent and self-contained modes. Builds have zero warnings/
errors. Syntax and whitespace checks pass. Evidence is archived under
`tmp/wpf8-support/validation/combined-desktop-sdk-20261013`.

The compiler package diagnostic candidate from section 97 contains the required
vcclr.h header and passes its expanded checks. Its final archive must still be
repinned to the committed fixture correction and built cleanly. WPF MSYS2 recipe/
clean package build and full native text/layout behavior remain open. The WPF
packaging goal stays active.


## 99. Installed WPF compiler package and clean desktop recipe

The 2026-10-13 continuation completes the clean C++/CLI 0.3.0 package from
checksum-pinned bootstrap commit 96ae31a, including the committed test-core
fixture correction. The full package check passes, creates the final package
and installs it with pacman. Its installed launcher then compiles and executes
the tracked inline UTF-16 array pin fixture successfully. The intrinsic vcclr.h
header is present in both the archive and installed compiler directory.
The msys2-ur compiler recipe/source-export/metadata changes are committed as
24ec4dc without consuming unrelated staged changes.

Validation on win11:

```sh
# private cppcli-0.3.0-final package directory; CLANG64 package environment
makepkg --noconfirm --force
pacman -U --noconfirm mingw-w64-clang-x86_64-cppcli-0.3.0-1-any.pkg.tar.zst
/clang64/bin/cli-clang.exe /clang64/share/cppcli/tests/inline-array-pins.cpp --type System.GC --type System.InvalidOperationException -o E:/msys64/home/topkek/wpfboot/cppcli-installed-proof/inline-array-pins.dll
/clang64/lib/dotnet/dotnet.exe E:/msys64/home/topkek/wpfboot/cppcli-installed-proof/inline-array-pins.dll
```

A new msys2-ur WindowsDesktop recipe builds both WPF 8 and approved interim
WinForms 7 from declared, checksum-pinned source archives. It clones only the
installed source-built core SDK toolchain and removes all existing desktop
framework/pack/SDK inputs before privately rebuilding Drawing, WinForms and
support libraries. WPF managed/native implementations and the tested combined
layout are then built with the installed source compiler. The package replaces
the WinForms-only SDK owner while retaining freshly rebuilt legacy framework/
pack versions for existing applications. Native ICU/libc++ seed dependencies
include package-provided licenses; the ICU license lives under share/icu rather
than share/licenses and was corrected in the local recipe.

The clean makepkg run has passed source checksums/extraction, rebuilt the initial
SystemEvents and Drawing pieces, and is compiling support libraries in
~/dotnetboot/packages/windowsdesktop-8.0.0. This is a live build, not a completed
WPF package claim. Source-build fixes, full package checks, final archive and
installation verification remain required. Compiler package evidence is under
`tmp/wpf8-support/validation/cppcli-package-0.3.0`; desktop evidence is collected
under `tmp/wpf8-support/validation/windowsdesktop-package-20261013`.
Full native text/layout remains incomplete and the WPF goal stays active.


## 100. Desktop crypto dependencies and runtime closure

The 2026-10-13 clean package build reaches WindowsBase after successfully
rebuilding WinForms and the initial WPF foundations, then stops because
System.Security.Cryptography.Xml was omitted from the package's support-library
set. The recipe now source-builds Xml and its PKCS dependency. The combined
framework layout includes both implementations instead of leaving their runtime
availability implicit in the earlier broad development environment.

The desktop inventory records actual assembly dependencies from metadata TypeRef
resolution scopes and forwarded exported types. The layout requires every such
managed dependency to exist either in its explicit desktop assembly set or the
source core shared framework. Unused AssemblyRef declarations in the CLI emitter
are not treated as runtime requirements; the initial all-AssemblyRef probe
incorrectly reported unrelated metadata-selection inputs. A deliberate missing
WindowsBase dependency is rejected before any assembly staging.

Validation on win11 with the standard WPF environment:

```sh
bash ~/wpfboot/driver/bootstrap/m612/wpf.sh layout check-sdk
# metadata fixture adds Missing.Desktop.Dependency to WindowsBase dependencies
python3 ~/wpfboot/driver/bootstrap/m612/tools/wpf-layout.py <build> <oob> <source> <sdk> <negative-output> <fixture>
```

The 42-managed-assembly layout passes its dependency check, both normal offline
WPF/WinForms builds, shared-framework windows and portable self-contained
publish/run checks. The negative fixture fails with the expected missing-dependency
message. Python/Bash syntax and git whitespace checks pass. Evidence is collected
under `tmp/wpf8-support/validation/desktop-crypto-closure-20261013`.

The package retry in its own source/build tree has now built the two crypto
libraries, WindowsBase, DirectWriteForwarder and PresentationCore, and is building
ReachFramework/dependency references. A final source patch/pin must carry the
layout changes into the package's declared inputs before final package claims.
Clean package checks/archive/installation and full native text/layout remain
open. The WPF goal stays active.


## 101. WindowsDesktop package built, installed and verified

The 2026-10-13 continuation completes the private source/build-tree package run.
After the section 100 source dependency correction, all WPF managed shipping
implementations, both CLI assemblies, graphics and pen input rebuild. Full checks
pass: DirectWrite original fonts/COM/lifetimes; 235 printing types and 2,017
prepared bodies; original properties/native spooler/XPS paths; ten additional
shipping assemblies with 674 types/6,426 bodies and automation/Ribbon/interop;
106 native graphics exports/shader/pixel checks; ordinary WPF/WinForms SDK builds
and self-contained publishes. Source input checksums pass for the final declared
crypto patch and staged-payload checker.

The final source-only recipe reuses its own compiled outputs for archive
creation after the diagnostic retry. Newly declared checksum-verified files are
copied into that private source staging directory before repackage, and the
payload checker passes all 42 implementation/reference hashes, native seed hashes,
pack entries, markup tasks and retained legacy versions. Build work paths in
source-bootstrap.json remain provenance only; makepkg reports them as a srcdir
reference warning. They are not SDK imports or runtime resolution paths.

Final validation commands, with the CLANG64 package environment:

```sh
makepkg --noextract --force --noconfirm
makepkg --verifysource --force
makepkg --repackage --force --noconfirm
pacman -U mingw-w64-clang-x86_64-dotnet-windowsdesktop-bootstrap-8.0.0-1-any.pkg.tar.zst
python3 check-staged.py /clang64/lib/dotnet
bash ~/wpfboot/driver/bootstrap/m612/wpf-tests/check-sdk.sh /clang64/lib/dotnet ~/wpfboot/installed-wpf-sdk-check
bash ~/wpfboot/driver/bootstrap/m612/winforms-tests/check-sdk.sh /clang64/lib/dotnet ~/wpfboot/installed-desktop-forms-check
```

The package replaces the WinForms-only SDK package and retains its freshly
source-built legacy framework and pack versions. Installed source SDK consumers
pass WPF BAML/named fields/window/raster/shared-framework and portable app-local
publish checks, and WinForms generator/control/resource/event/GDI+ checks. A
WinForms app explicitly requesting 8.0.0-bootstrap.7.0.20 with rollForward Disable
also runs. The installed payload hash/manifest check passes. No build warnings/
errors occur in those SDK consumers.

Artifact: mingw-w64-clang-x86_64-dotnet-windowsdesktop-bootstrap-8.0.0-1-any.pkg.tar.zst
(about 53 MiB compressed / 161 MiB installed).
SHA-256: 21306aa5f12ce094a6a6498c295cc8b92d5c3ea63bf442b629037cd3ee039b2b.
A local copy and logs are archived under
`tmp/wpf8-support/validation/windowsdesktop-package-20261013`.
The msys2-ur recipe/payload verifier is committed through 903d407.

This completes a concrete experimental WPF package and installation proof. It
continues to label the partial native port: PresentationNative imported symbol
coverage remains 58/139, with bidi/paragraph/NLS/other text paths unfinished.
WinForms remains the approved 7.0.20 interim source port. The package does not
prove full upstream WPF behavior. The original full WPF goal stays active while
remaining native behavior and coverage are completed.


## 102. Original text decoration callbacks and public raster proof

The 2026-10-13 continuation replaces the native LoDisplayLine decoration
rejection with original underline/strikethrough metric and drawing callbacks.
LsULInfo and LsStInfo are generated from original LineServices.cs alongside the
existing ABI records. Each visible retained run supplies its actual identity,
width, metrics, flow, draw mode and clip. One/two-line offsets and thicknesses
are validated; metric and draw callback failures propagate unchanged.

The installed 8.0.0-1 package reproduces NotImplemented when public TextFormatter
draws decorated text. The source build passes native single/double geometry,
callback ABI/error/ownership checks and public custom red-pen raster checks for
underline, strikethrough, overline and baseline. Each has 310 red pixels; average
y coordinates are 47.50, 36.50, 11.50 and 44.50 against text baseline 44.53.
Decorated/plain line metrics agree, glyphs remain visible, and drawing survives
compacting GC. Existing public Indic caret/selection/shaping and wrapping checks
also pass. These checks cover the exercised LTR run path; complete bidi,
paragraph layout, tabs, inline objects and justification remain unfinished.

Validation with the standard win11 WPF environment:

```sh
WPF_DECORATION_NATIVE=/clang64/lib/dotnet/shared/Microsoft.WindowsDesktop.App/8.0.0-bootstrap.wpf.8.0.0 bash ~/wpfboot/driver/bootstrap/m612/wpf.sh check-text-decorations
bash ~/wpfboot/driver/bootstrap/m612/wpf.sh native-win32 check-lineservices-line-native check-text-decorations check-text-line
bash ~/wpfboot/driver/bootstrap/m612/wpf.sh check-lineservices-line-native
```

The first command fails with the expected installed-library NotImplemented;
the latter two pass, including the additional draw-callback failure check.
Evidence is archived under tmp/wpf8-support/validation/text-decorations-20261013.
This changes behavior of existing exports; imported coverage remains 58/139.
The package update is still required to deliver the new native implementation.
The full WPF goal remains active.


## 103. Installed WindowsDesktop release 2 with text decorations

The 2026-10-13 continuation packages section 102 as WindowsDesktop 8.0.0-2.
The recipe declares a checksum-verified 0002-text-decorations.patch against its
pinned bootstrap archive and adds native/public decoration checks to check().
The patch is applied to the package's existing private source tree, followed by
a focused native rebuild, native/public text checks and fresh framework layout.
Previously compiled unchanged managed/graphics/printing inputs are reused.
The initial manual recipe-helper invocation lacked makepkg's srcdir variable;
setting it to the private src directory corrects that invocation before any
rebuild. No private build is restarted because of an observation timeout.

Validation from the CLANG64 package workspace:

```sh
makepkg --verifysource --force
source PKGBUILD                 # with srcdir set to this workspace's src/
_wpf native-win32 check-lineservices-line-native check-text-decorations check-text-line layout
makepkg --repackage --force --noconfirm
pacman -U --noconfirm mingw-w64-clang-x86_64-dotnet-windowsdesktop-bootstrap-8.0.0-2-any.pkg.tar.zst
python3 check-staged.py /clang64/lib/dotnet
WPF_DECORATION_NATIVE=/clang64/lib/dotnet/shared/Microsoft.WindowsDesktop.App/8.0.0-bootstrap.wpf.8.0.0 bash ~/wpfboot/driver/bootstrap/m612/wpf.sh check-text-decorations
bash ~/wpfboot/driver/bootstrap/m612/wpf-tests/check-sdk.sh /clang64/lib/dotnet ~/wpfboot/installed-decorations-sdk-check
```

All final commands pass. Installed native output now passes the same four
public decoration raster cases that failed against release 1. The installed
payload verifier checks all 42 managed/reference pairs and native hashes.
Ordinary offline WPF SDK compilation, real BAML fields/window/raster output and
self-contained publish/run with SDK roots/toolchain PATH removed pass with zero
consumer warnings/errors. The srcdir reference warning remains the existing
provenance path in source-bootstrap.json; no runtime/import dependency uses it.

Artifact: mingw-w64-clang-x86_64-dotnet-windowsdesktop-bootstrap-8.0.0-2-any.pkg.tar.zst.
SHA-256: 4abc137973c35af10dc5d38d2d605faa432bcc3ca582aa4447f6029202f1fc2e.
Logs, declared recipe/patch/source metadata and the local artifact are archived
under tmp/wpf8-support/validation/text-decorations-package-20261013.
Imported PresentationNative coverage stays 58/139. Full bidi, paragraph layout
and remaining text/natural-language behavior remain required; the goal stays
active.


## 104. Tab layout, leaders and missing composite-font resources

The 2026-10-13 continuation removes the native tab-character rejection. Tabs
become retained layout runs with their own width and the original client run
identity/CP. The engine measures the following field across runs, aligns source
left/center/right/decimal/character stops and continues with incremental stops
after explicit ones. Nominal/shaped field widths respect original clusters.
Caret cells span the gap, and original EnumTab supplies its source position,
leader and geometry. Leaders are measured through original GetRunCharWidths
and drawn as repeated whole characters centered in the available gap through
original DrawTextRun. Callback failures and invalid/missing stops propagate.

The first public check exposes missing PresentationCore.g.resources during
composite-font fallback. The planner had preserved Resource items without
importing WinFX resource-generation targets for resource-only projects. It now
selects those targets whenever upstream Resource items exist and records them
in its source manifest. PresentationCore builds its source PresentationBuildTasks
first when absent. All four original composite fonts are embedded; original
fallback proceeds. No Microsoft binary resources or assemblies are added.

Validation with the standard win11 WPF environment:

```sh
bash ~/wpfboot/driver/bootstrap/m612/wpf.sh native-win32 check-text-tabs check-lineservices-line-native check-text-line check-text-decorations
bash ~/wpfboot/driver/bootstrap/m612/wpf.sh build-presentation-core check-text-tabs check-lineservices-line-native check-text-line check-text-decorations
bash ~/wpfboot/driver/bootstrap/m612/wpf.sh check-lineservices-line-native check-text-tabs
bash ~/wpfboot/driver/bootstrap/m612/wpf.sh check-lineservices-line-native
```

The initial public run fails with MissingManifestResourceException and motivates
the resource correction. The final commands pass. The native check covers all
five original stop kinds, cross-run field metrics, exact tab cells, EnumTab ABI,
leader widths/drawing, incremental stops and draw/enumeration errors. Its initial
leader counter also counted a literal period in the field; distinguishing the
repeated leader callback fixes that fixture. The public test checks all four
WPF alignments with/without dotted leaders and both whole/split fields: starts
160.000, 126.937, 93.873 and 130.763 pixels, with 142/102/67/108 leader pixels
and zero gap pixels without leaders. It verifies tab caret/selection geometry,
source-backed glyph enumeration, all four composite resources, incremental
stops, ordinary wrapping and moving GC. Existing Indic/decoration tests pass.

A field cut by wrapping can move its aligned stop beyond the column; this path
still explicitly rejects and is checked rather than returning false geometry.
Full bidi tabs and exact private legacy leader spacing remain open. Evidence is
archived under tmp/wpf8-support/validation/text-tabs-20261013. Imported native
coverage remains 58/139 because these are behaviors of existing exports. The
updated package is still required; the full WPF goal remains active.


## 105. Installed release 3: tab/font resources and ordinary SDK consumers

The 2026-10-13 continuation packages section 104 as WindowsDesktop 8.0.0-3.
Its declared/checksummed 0003-tabs-and-composite-fonts.patch carries the native
engine, planner, fixtures and SDK check. A clean local pinned archive plus all
three declared patches matches the committed feature/test sources byte for byte.
The private package tree rebuilds PresentationCore and PresentationNative, then
passes native/public tab, text, decoration and fresh framework-layout checks.
Unchanged managed/graphics/printing inputs remain its own earlier source builds.

The new check-text-tabs-sdk.sh compiles the fixture as a normal net8.0-windows
UseWPF application, with an empty package feed and no hand-added implementation
references. It loads the installed/overlaid shared framework, verifies all four
original composite resources, and passes tab metrics/caret/selection/enumeration,
leaders/pixels/wrapping/GC. The same app publishes self-contained; its
PresentationCore bytes equal the source SDK framework implementation, and it
runs with DOTNET_ROOT/DOTNET_ROOT_X64 and toolchain PATH removed. step_check_sdk
now includes this consumer alongside the original WPF and WinForms checks.

Validation from the CLANG64 private package workspace:

```sh
makepkg --verifysource --force
source PKGBUILD                 # with srcdir set to this workspace's src/
_wpf build-presentation-core native-win32 check-lineservices-line-native check-text-tabs check-text-line check-text-decorations layout
_wpf check-sdk
makepkg --repackage --force --noconfirm
pacman -U --noconfirm mingw-w64-clang-x86_64-dotnet-windowsdesktop-bootstrap-8.0.0-3-any.pkg.tar.zst
python3 check-staged.py /clang64/lib/dotnet
bash ~/wpfboot/driver/bootstrap/m612/wpf-tests/check-text-tabs-sdk.sh /clang64/lib/dotnet ~/wpfboot/installed-tabs-sdk-check
bash ~/wpfboot/driver/bootstrap/m612/wpf-tests/check-sdk.sh /clang64/lib/dotnet ~/wpfboot/installed-fonts-sdk-check
```

Final checks pass with zero consumer warnings/errors, including the private
normal SDK WPF/tab/WinForms build/publish checks and installed SDK consumers.
After incorporating the SDK check into declared source inputs, final source
verification/repackage/reinstallation and the 42-managed/native payload hash
check pass. Installed shared-framework and app-local tab applications are rerun
successfully after that final reinstall. The known srcdir warning remains only
source-bootstrap.json provenance. The package is about 162 MiB installed.

Artifact: mingw-w64-clang-x86_64-dotnet-windowsdesktop-bootstrap-8.0.0-3-any.pkg.tar.zst.
SHA-256: d79cd5a4783638a32018725eaa236680d2786c4ca8c443943ec8b525b4a99415.
Recipe commit: msys2-ur 9e04be1. Source feature/check commits: a6b3a1d/3f80c49.
Logs, patch/recipe/metadata and the artifact are archived under
 tmp/wpf8-support/validation/text-tabs-package-20261013.
Imported native coverage remains 58/139. Aligned-field wrapping, bidi, full
paragraph layout, justification/objects and natural-language behavior remain
required; this improves the experimental package and leaves the full goal active.


## 106. Retained-field tab wrapping and remeasured prefix validation

The 2026-10-13 continuation replaces the aligned-wrap rejection with candidate
measurement using only the field retained by each legal Unicode break. Full
paragraph tab gaps cannot predict the position of a shortened aligned field.
A shared tab-target helper now computes both retained and candidate geometry.
Trailing spaces are excluded from visible allowance while remaining in the
candidate's field alignment. Existing grapheme/shaping-cluster boundaries still
filter candidates. After shortening, actual remeasurement/reshaping and tab
layout validate the retained visible width. If contextual advances grow beyond
the column, an earlier legal candidate is tried before registering a line.
The selected wrapping policy still permits unavoidable first-unit/word overflow;
an out-of-column leading tab advances on its own rather than failing or stalling.

The original installed release 3 library reproduces NotImplemented on the
expanded public fixture. Native checks now prove a right-stop field retained on
the first line with exact end/trailing widths/tab gap, independent cloned native
continuation records, a break before an out-of-column tab and progress across
that indivisible tab. A callback fixture changes retained-prefix advances: the
engine rejects the now-too-wide three-character candidate and returns the
fitting two-character prefix. Existing wrapping/geometry/decoration tests pass.

Validation with the standard win11 WPF environment:

```sh
bash ~/wpfboot/driver/bootstrap/m612/wpf.sh native-win32 check-lineservices-line-native check-text-tabs check-text-line
WPF_TAB_NATIVE=/clang64/lib/dotnet/shared/Microsoft.WindowsDesktop.App/8.0.0-bootstrap.wpf.8.0.0 bash ~/wpfboot/driver/bootstrap/m612/wpf.sh check-text-tabs
bash ~/wpfboot/driver/bootstrap/m612/wpf.sh check-text-tabs
bash ~/wpfboot/driver/bootstrap/m612/wpf.sh native-win32 check-lineservices-line-native check-text-tabs check-text-line check-text-decorations
```

The old installed-native command fails with expected NotImplemented; final
source commands pass. Public aligned wrapping covers all four WPF alignments,
whole/split fields and 100/160-pixel stops in a 125-pixel column. In-column cases
use three/four lines without overflow; out-of-column cases cover the source in
five lines with exactly one indivisible leading-tab overflow. Checks cover
retained right-stop identity, source-backed glyph enumeration, grapheme-safe
progress, continuation clone/disposal, drawing/raster and moving GC.

Before final package installation, WPF's original paragraph-property validation
confirms zero incremental-tab distance is allowed. The initial private release 4
native output still fails the new explicit-only retained-field test: the full
field misses its right stop and has no incremental fallback. The engine now
lets wrapped explicit-stop candidates proceed, skips candidates without usable
stops, and retries if actual prefix remeasurement loses a stop. Native checks
and whole/split public explicit-only cases pass (retained first-line length 9).
The final narrow command is native-win32 check-lineservices-line-native
check-text-tabs; existing text-line checks also pass with this correction.

Evidence is archived under tmp/wpf8-support/validation/aligned-wrap-20261013.
Imported native coverage remains 58/139. Candidate evaluation scans fields and
needs a performance improvement for long tabbed paragraphs; full bidi/paragraph
layout and exact legacy leader spacing remain open. Packaging this change is
still required, and the full WPF goal stays active.


## 107. Installed release 4 with aligned-field wrapping

The 2026-10-13 continuation packages section 106 as WindowsDesktop 8.0.0-4.
The declared/checksummed 0004-aligned-field-wrapping.patch contains the native
engine and expanded native/public fixtures. A clean pinned source archive plus
all four declared patches matches committed feature sources byte for byte.
The package's own native engine is rebuilt and its source fixtures pass before
fresh framework layout and ordinary SDK checks. The initial draft rebuild/checks
pass, then the explicit-only zero-increment case is corrected before installation.
The final declared patch/source staging is verified and rebuilt again; unchanged
managed/graphics/printing artifacts remain the package's own source builds.

Validation from the CLANG64 package workspace:

```sh
makepkg --verifysource --force
source PKGBUILD                 # with srcdir set to this workspace's src/
_wpf native-win32 check-lineservices-line-native check-text-tabs layout check-sdk
makepkg --repackage --force --noconfirm
pacman -U --noconfirm mingw-w64-clang-x86_64-dotnet-windowsdesktop-bootstrap-8.0.0-4-any.pkg.tar.zst
python3 check-staged.py /clang64/lib/dotnet
bash "$(_driver)/wpf-tests/check-text-tabs-sdk.sh" /clang64/lib/dotnet ~/wpfboot/installed-aligned-wrap-sdk-check
```

All final commands pass, including zero-warning/error normal WPF/tab/WinForms
SDK builds and self-contained publishes. Installed normal UseWPF consumers
exercise all four alignments, whole/split fields, in/out-of-column stops, source
coverage, continuation/enumeration/raster/GC and explicit-only stops. A leading
indivisible tab outside the column is the only permitted overflow in the fixture;
right-stop retained fields otherwise fit. The self-contained app runs with SDK
roots/toolchain PATH removed. The 42-managed/pack/native-seed payload checker
passes before packaging and after installation. The existing srcdir warning
remains only source-bootstrap.json provenance.

Artifact: mingw-w64-clang-x86_64-dotnet-windowsdesktop-bootstrap-8.0.0-4-any.pkg.tar.zst.
SHA-256: 9bfc4c1b3a0753ad0f96f816916ba7ddc38f3c40a2155707cba7e53d06f17820.
Recipe commit: msys2-ur c69f6bb; feature commits: de03298/6ae3563.
Logs, declared recipe/patch/metadata and the artifact are archived under
 tmp/wpf8-support/validation/aligned-wrap-package-20261013.
Imported native coverage remains 58/139; full bidi/paragraph layout, inline
objects/justification and natural-language behavior remain incomplete. This
removes the documented aligned-field wrapping rejection while leaving the full
WPF goal active.


## 108. Original inline object callbacks, source break table and UI containers

The 2026-10-13 continuation replaces the native inline-object rejection for
original object ID 1 with original GetObjectHandlerInfo, InlineFormat and
InlineDraw callbacks. ObjDim/InlineInit and IdealInfiniteWidth are generated
from original LineServices.cs. The native engine validates callback records and
dimensions, measures at the preceding pen position, retains atomic object width
and line metrics, and draws through the original managed callback. Queries keep
object identity/dimensions with zero glyphs; glyph enumeration skips objects
and preserves actual source indices for surrounding text. One LS character can
represent a longer backing-source object span through original WPF mappings.

Word/emergency wrapping applies the original TextAndFontProperties.cs break
combination table at object edges. Always allows a break even against restraint;
Desired permits a break unless restrained; Possible/Possible prohibits it.
This is source-table break eligibility, not an invented hard paragraph break.
The native test uses the literal source matrix reordered to native enum values,
and checks all 16 adjacent-object combinations through actual line creation.
It also checks 24-byte InlineInit/28-byte ObjDim layouts, identities/metrics/pen
origins, zero-glyph object cells, format/draw errors and invalid-dimension outputs.

The public fixture uses original TextEmbeddedObject with a three-character
backing span. Fixed/flexible widths are 70/60 pixels, remaining width 173.047;
blue raster counts are 2100/1800. It verifies baseline/caret/selection geometry,
source glyph indices around the object, wrapping without splitting its span,
compacting GC and original format/draw exception propagation. Original TextBlock
with a real InlineUIContainer/Border measures a 45x20 child, renders 880 red
pixels and returns a real point hit after GC.

The installed release 4 native library reproduces NotImplemented on the public
fixture. Final source validation with the standard win11 WPF environment:

```sh
bash ~/wpfboot/driver/bootstrap/m612/wpf.sh native-win32 check-lineservices-line-native
WPF_INLINE_NATIVE=/clang64/lib/dotnet/shared/Microsoft.WindowsDesktop.App/8.0.0-bootstrap.wpf.8.0.0 bash ~/wpfboot/driver/bootstrap/m612/wpf.sh check-inline-objects
bash ~/wpfboot/driver/bootstrap/m612/wpf.sh check-inline-objects
bash ~/wpfboot/driver/bootstrap/m612/wpf.sh check-inline-objects-native check-text-tabs check-text-line check-text-decorations
bash ~/wpfboot/driver/bootstrap/m612/wpf.sh check-inline-objects-native
bash ~/wpfboot/driver/bootstrap/m612/wpf.sh layout check-sdk
```

All final source commands pass; the old installed-native command fails with the
expected NotImplemented. The normal SDK check now includes an ordinary offline
UseWPF inline-object consumer and self-contained publish with SDK roots/toolchain
PATH removed. All normal WPF/tab/inline/WinForms checks pass with zero consumer
warnings/errors. SDK validation also unlinks the exact replacement shared/ref/
runtime desktop directories before overlaying source output onto its hardlink
clone. Otherwise an installed WPF seed at the same framework version could be
modified by the overlay. Before/after installed native/ref/runtime SHA checks
pass, proving this corrected check leaves those installed inputs unchanged.

Evidence is archived under tmp/wpf8-support/validation/inline-objects-20261013.
Imported native coverage remains 58/139. Position-dependent objects after aligned
tabs still reject pending circular measurement support; full bidi/paragraph
layout and justification remain open. The package update is still required.
The full WPF goal stays active.


## 109. Installed release 5: original inline objects and UI containers

The 2026-10-13 continuation packages section 108 as WindowsDesktop 8.0.0-5.
The recipe declares/checksums 0005-inline-objects-and-sdk-isolation.patch,
including the source ABI generator, native engine, native/public/SDK fixtures
and SDK-overlay isolation correction. The clean pinned archive plus all five
patches matches committed feature inputs byte for byte. Its private source tree
rebuilds PresentationNative and passes source native/public object checks,
existing line/tab checks and fresh framework/pack layout. Unchanged managed,
graphics and printing artifacts remain the package's earlier source builds.

Validation from the CLANG64 package workspace:

```sh
makepkg --verifysource --force
source PKGBUILD                 # with srcdir set to this workspace's src/
_wpf native-win32 check-inline-objects-native check-inline-objects check-lineservices-line-native check-text-tabs layout check-sdk
makepkg --repackage --force --noconfirm
pacman -U --noconfirm mingw-w64-clang-x86_64-dotnet-windowsdesktop-bootstrap-8.0.0-5-any.pkg.tar.zst
python3 check-staged.py /clang64/lib/dotnet
bash "$(_driver)/wpf-tests/check-inline-objects-sdk.sh" /clang64/lib/dotnet ~/wpfboot/installed-inline-sdk-check
```

All final commands pass. Normal private SDK WPF/tab/inline/WinForms projects
build and publish with zero consumer warnings/errors. The installed normal
UseWPF consumer verifies original fixed/flexible TextEmbeddedObject metrics,
remaining width, backing-span caret/selection, glyph enumeration, wrapping,
callback exceptions and original TextBlock InlineUIContainer layout/raster/hits
across GC. Its self-contained publish executes with SDK roots/toolchain PATH
removed and verifies the source PresentationCore implementation bytes. Native
checks prove all 16 source break-condition combinations and original record/
callback ABI. The staged/installed 42-managed/pack/native-seed verifier passes.
The known srcdir warning remains only source-bootstrap.json provenance.

Artifact: mingw-w64-clang-x86_64-dotnet-windowsdesktop-bootstrap-8.0.0-5-any.pkg.tar.zst.
SHA-256: ca3c27646edb6a10fa89f75ed58f742f58809185929d437c04a124dd3a61d5fa.
Recipe commit: msys2-ur f0bc320; feature commit: 5b6ab43.
Logs, declared patch/recipe/metadata and the artifact are archived under
 tmp/wpf8-support/validation/inline-objects-package-20261013.
Imported native coverage remains 58/139. Position-dependent objects after
aligned tabs, bidi/paragraph layout, justification and natural-language behavior
remain incomplete. This delivers inline-object behavior in the experimental
package while the full WPF goal remains active.


## 110. Original expansion callbacks and regular inter-word justification

The 2026-10-13 continuation removes regular inter-word paragraph justification
rejection. It generates original nominal/glyph/neighbor record layouts, including
pointer fields and unsigned Plsrun identity. The native engine requests original
character/glyph expansion callbacks, validates output capacities/types, and
uses three provider capacity levels to distribute the missing visible width
across whitespace opportunities. Original glyph advances and nominal minimum
widths remain retained; actual glyph/character advances update run geometry,
caret/selection, enumeration and drawing. Original and adjusted glyph advances
are passed separately to DrawGlyphs. Trailing spaces are not expanded, and final
paragraph/hard-break/no-opportunity lines retain their nominal width.

Native checks prove 28/48/80-byte callback record ABI, exact 23-unit expanded
space width/47-unit following-text position, visible end 67 and total end 77,
nominal minima 54/64, original glyph advances, provider limits and error outputs.
The public fixture reproduces NotImplemented against installed release 5. Source
Latin/Indic paragraphs pass in Ideal and Display modes with 133-pixel justified
visible widths, retained selection/caret geometry, source-backed glyph advances,
raster output and compacting GC. Original justified TextBlock measure/render/
point-hit checks also pass. Final/hard-break/no-space cases stay ragged.

The initial public glyph-sum comparison overlooked original WPF's thousandth-of-
em storage in Ideal mode; it now uses the source quantization bound, while exact
native geometry and visible width checks remain strict. The initial Display
Indic column already equalled nominal width, so the fixture column changes from
130 to 133 pixels to exercise actual expansion in both formatting modes.

Validation with the standard win11 WPF environment:

```sh
bash ~/wpfboot/driver/bootstrap/m612/wpf.sh native-win32 check-lineservices-line-native check-inline-objects-native
WPF_JUSTIFICATION_NATIVE=/clang64/lib/dotnet/shared/Microsoft.WindowsDesktop.App/8.0.0-bootstrap.wpf.8.0.0 bash ~/wpfboot/driver/bootstrap/m612/wpf.sh check-text-justification
bash ~/wpfboot/driver/bootstrap/m612/wpf.sh check-justification-native check-text-justification
bash ~/wpfboot/driver/bootstrap/m612/wpf.sh check-text-justification check-text-tabs check-inline-objects check-text-decorations
bash ~/wpfboot/driver/bootstrap/m612/wpf.sh check-text-justification
```

Final commands pass with zero consumer warnings/errors; the installed-native
baseline fails as expected. Evidence is archived under
 tmp/wpf8-support/validation/justification-20261013. Imported coverage remains
58/139 because this improves existing exports. Aligned-tab/other justification
modes, ink insertion, compression/optimal algorithms and full bidi/paragraph
layout remain open. The package update is still required; the full goal stays
active.


## 111. Installed release 6 with source inter-word justification

The 2026-10-13 continuation packages section 110 as WindowsDesktop 8.0.0-6.
Its declared/checksummed 0006-interword-justification.patch carries the ABI
generator, native engine and native/public/SDK fixtures. A clean pinned archive
plus all six declared patches matches committed feature/check sources byte for
byte. The private package tree rebuilds PresentationNative, then passes exact
justification ABI/geometry/limits/error tests, public justification and existing
native line/object checks before fresh layout and ordinary SDK verification.
Unchanged managed/graphics/printing artifacts remain its earlier source builds.

Validation from the CLANG64 package workspace:

```sh
makepkg --verifysource --force
source PKGBUILD                 # with srcdir set to this workspace's src/
_wpf native-win32 check-justification-native check-text-justification check-lineservices-line-native check-inline-objects-native layout check-sdk
makepkg --repackage --force --noconfirm
pacman -U --noconfirm mingw-w64-clang-x86_64-dotnet-windowsdesktop-bootstrap-8.0.0-6-any.pkg.tar.zst
python3 check-staged.py /clang64/lib/dotnet
bash "$(_driver)/wpf-tests/check-justification-sdk.sh" /clang64/lib/dotnet ~/wpfboot/installed-justification-sdk-check
```

All final commands pass with zero consumer warnings/errors. Normal private WPF,
tab, inline-object, justification and WinForms projects build and publish. The
installed normal UseWPF justification consumer verifies Latin/Indic expansion
in Ideal/Display modes, strict visible/caret/selection geometry, final/hard/no-
opportunity line policy, source coverage and actual justified TextBlock raster/
point hits after GC. Its portable self-contained publish runs with SDK roots/
toolchain PATH removed. The staged/installed 42-managed/pack/native-seed checker
passes. The known srcdir warning remains only source-bootstrap.json provenance.

Artifact: mingw-w64-clang-x86_64-dotnet-windowsdesktop-bootstrap-8.0.0-6-any.pkg.tar.zst.
SHA-256: 71236ca4f99f0d0ca8a58d8f75c1c1827791171efa010a3798964122e6d41058.
Recipe commit: msys2-ur 8adcd79; feature commit: 7019152.
Logs, patch/recipe/metadata and the local artifact are archived under
 tmp/wpf8-support/validation/justification-package-20261013.
Imported native coverage remains 58/139. Advanced justification modes/ink
insertion/compression/optimal algorithms, aligned-tab justification and full
bidi/paragraph/natural-language behavior remain incomplete. This improves the
experimental package while the full WPF goal stays active.


## 112. Paragraph minimum/maximum widths and intrinsic text layout

The 2026-10-13 continuation removes native MinMax-mode rejection. It retains
whole paragraphs without column wrapping/justification and shares existing
Unicode prefix/cluster/object-edge geometry with ordinary wrapping. Minimum
width is the widest indivisible legal-break segment; maximum width is the widest
unwrapped hard line. Hard breaks remain zero-width retained markers in this
measurement mode and fetching continues through paragraph end. Trailing spaces
remain in total widths but are excluded from visible widths. Tabs are measured
at actual stops from each segment's paragraph origin. Objects preserve atomic
dimensions and the original break-condition table.

This follows original TextFormatterImp.FormatMinMaxParagraphWidth and
FullTextLine.MinWidth metadata consumption. Microsoft documents the
FormattedText.MinWidth word-width semantics at
https://learn.microsoft.com/en-us/dotnet/api/system.windows.media.formattedtext.minwidth?view=windowsdesktop-10.0.
Native checks verify exact column-independent max total/visible 124/104 and min
84/64 across one-character client runs; hard-line paragraph coverage 12 LS units
with max 94/min 64; NBSP max 84/min 54; tab max 60/min 40; all 16 adjacent-object
break combinations produce the correct atomic minimum. Ordinary line/object/
justification tests pass after sharing the break geometry.

The public fixture verifies nominal Latin, shaped Indic, NBSP, multiple hard
lines, whole words, trailing/empty/space-only content and cached repeat calls
across GC in Ideal/Display modes. It compares measured widths with independently
formatted source words/hard lines, checks minimum/maximum ordering and exercises
original FormattedText.MinWidth plus intrinsic/wrapped TextBlock measure/render/
point hits. The first fixture rename also changed framework type/property names;
those identifiers are restored before final validation. FormattedText is created
with the same formatting mode as its independent comparison.

Validation with the standard win11 WPF environment:

```sh
bash ~/wpfboot/driver/bootstrap/m612/wpf.sh native-win32 check-lineservices-line-native check-inline-objects-native check-justification-native
WPF_WIDTHS_NATIVE=/clang64/lib/dotnet/shared/Microsoft.WindowsDesktop.App/8.0.0-bootstrap.wpf.8.0.0 bash ~/wpfboot/driver/bootstrap/m612/wpf.sh check-paragraph-widths
bash ~/wpfboot/driver/bootstrap/m612/wpf.sh check-lineservices-line-native check-inline-objects-native check-paragraph-widths
bash ~/wpfboot/driver/bootstrap/m612/wpf.sh check-lineservices-line-native check-text-justification check-inline-objects check-text-tabs
```

Final source commands pass with zero consumer warnings/errors. The installed
release 6 native baseline fails with expected NotImplemented at the public
minimum/maximum width call. Evidence is archived under
 tmp/wpf8-support/validation/paragraph-widths-20261013. Imported native coverage
remains 58/139. Full RTL, hyphenation/compression/optimal algorithms, paragraph
layout and natural-language behavior remain open. The package update is still
required, and the full WPF goal stays active.


## 113. Installed release 7 with paragraph width measurement

The 2026-10-13 continuation packages section 112 as WindowsDesktop 8.0.0-7.
The declared/checksummed 0007-paragraph-widths.patch contains the native shared
break geometry/min-max engine and expanded native/public/SDK fixtures. A clean
pinned archive plus all seven declared patches matches committed feature/check
inputs byte for byte. Its private tree rebuilds PresentationNative and verifies
exact line/object minimum/maximum geometry, public width APIs and existing
justification before fresh framework layout and ordinary SDK checks. Unchanged
managed/graphics/printing artifacts remain its earlier source builds.

Validation from the CLANG64 package workspace:

```sh
makepkg --verifysource --force
source PKGBUILD                 # with srcdir set to this workspace's src/
_wpf native-win32 check-lineservices-line-native check-inline-objects-native check-paragraph-widths check-justification-native layout check-sdk
makepkg --repackage --force --noconfirm
pacman -U --noconfirm mingw-w64-clang-x86_64-dotnet-windowsdesktop-bootstrap-8.0.0-7-any.pkg.tar.zst
python3 check-staged.py /clang64/lib/dotnet
bash "$(_driver)/wpf-tests/check-paragraph-widths-sdk.sh" /clang64/lib/dotnet ~/wpfboot/installed-paragraph-widths-sdk-check
```

All final commands pass with zero consumer warnings/errors, including normal
private WPF/tab/object/justification/width/WinForms SDK builds and publishes.
Installed normal UseWPF consumers verify nominal/Indic/NBSP/hard-break/trailing/
empty widths, cached repeats across GC in Ideal/Display modes, original
FormattedText.MinWidth and intrinsic/wrapped TextBlock raster/point hits.
The self-contained app runs with SDK roots/toolchain PATH removed. Staged and
installed 42-managed/pack/native-seed checks pass. The existing srcdir warning
remains only source-bootstrap.json provenance.

Artifact: mingw-w64-clang-x86_64-dotnet-windowsdesktop-bootstrap-8.0.0-7-any.pkg.tar.zst.
SHA-256: bb63bd302469815d75d96f65ce42effa3af09313e91eb0902238010cb897289e.
Recipe commit: msys2-ur f960ec4; feature commit: e8fa2d6.
Logs, patch/recipe/metadata and the artifact are archived under
 tmp/wpf8-support/validation/paragraph-widths-package-20261013.
Imported native coverage remains 58/139. Full bidi/paragraph layout,
hyphenation/compression/optimal algorithms and natural-language behavior remain
incomplete; the full WPF goal stays active.


## 114. Paragraph service contexts and owned LS penalty resources

The 2026-10-13 continuation reproduces original FlowDocument finite-page failure
in installed release 7 at CreateInstalledObjectsInfo. Source-visible PtsHost
IDs assign 0/1 to container/subpage and 2/3 to floater/table. The new source
paragraph module owns opaque registration records with copied container/subpage
method tables, and document contexts retain original generated FSCONTEXTINFO
and all callbacks. Source object create/destroy callbacks receive actual client/
context/flags/IDs. Creation failure rolls back previously created objects, and
invalid/double-destroy handles reject without dereferencing unregistered data.
The registration can be released while an existing document independently owns
its copied tables. This is source-engine registration state, not the private
legacy installed-array representation or completed page layout.

Managed/native original ABI agrees: context 872, methods 136, callbacks 824 bytes.
Native tests verify copy isolation, source callbacks, identity snapshots,
independent retention, rollback and teardown. Original PtsContext construction,
GC, pooling/release and dispatcher shutdown pass in both formatting modes.
The fixture initially called Dispose directly; original cache release must be
queued via PtsCache.ReleaseContext, which clears the owner before disposal.
The corrected fixture follows that original lifecycle. A missing Media namespace
in the initial fixture is also corrected before final checks.

The isolated FlowDocument probe next reaches LoAcquirePenaltyModule because
StructuralCache constructs an optimal-capable PtsContext independently of the
document's optimal-paragraph property. LoAcquirePenaltyModule now creates a
real owned wrapper around retained LS context state; internal-handle retrieval
exposes its opaque state identity and disposal releases it. Native identity/
retention/invalid/double-release tests pass. Original TextPenaltyModule and
PtsContext true/false configurations pass in Ideal/Display modes across GC and
cache shutdown. No penalty scoring or optimal breaking is claimed. A manual
SDK overlay initially updated only shared native output; updating its runtime
pack as well completes the portable consumer. Final packaging regenerates both.

Validation with the standard win11 WPF environment:

```sh
bash ~/wpfboot/driver/bootstrap/m612/wpf.sh native-win32 inventory check-paragraph-context-native check-paragraph-context
bash ~/wpfboot/driver/bootstrap/m612/wpf.sh check-lineservices-line-native inventory
bash ~/wpfboot/driver/bootstrap/m612/wpf.sh layout check-sdk
bash ~/wpfboot/driver/bootstrap/m612/wpf-tests/check-paragraph-context-sdk.sh ~/wpfboot/build/sdk-check-root ~/wpfboot/pts-context-penalty-sdk-check
bash ~/wpfboot/driver/bootstrap/m612/wpf-tests/check-flowdocument-sdk.sh ~/wpfboot/build/sdk-check-root ~/wpfboot/flowdocument-context-penalty-check
```

Final context/SDK checks pass with zero consumer warnings/errors; original
normal WPF/tab/object/justification/width/WinForms regressions also pass. The
last FlowDocument command remains a pending gate: it now fails at absent
FsCreatePageFinite, rather than registration or penalty resource creation.
A direct DLL copy beside the original installed probe did not override its
framework native resolution; the isolated SDK selects the actual source DLL.
Native inventory now reports 65/139 imports provided (seven new resource/context
exports), with 74 absent. Stale inventory blockers for already-built printing/
WindowsDesktop packaging are corrected. Evidence is archived under
 tmp/wpf8-support/validation/paragraph-context-20261013. Page formatting/query/
break records, full bidi/optimal/natural-language behavior remain open; the full
WPF goal stays active and this source change still requires a package update.


## 115. Installed release 8: paragraph contexts and penalty-resource ownership

The 2026-10-13 continuation packages section 114 as WindowsDesktop 8.0.0-8.
Declared/checksummed 0008-paragraph-contexts-and-penalty-resources.patch carries
the source ABI generator, paragraph module, LS penalty ownership APIs and
native/public/SDK fixtures. Clean pinned sources plus all eight declared patches
match committed feature inputs byte for byte. The private package tree builds
the native modules, refreshes inventory and passes paragraph/native line tests
before regenerating both shared-framework and runtime-pack output. Unchanged
managed/graphics/printing inputs remain its earlier source builds.

Validation from the CLANG64 package workspace:

```sh
makepkg --verifysource --force
source PKGBUILD                 # with srcdir set to this workspace's src/
_wpf native-win32 inventory check-paragraph-context-native check-paragraph-context check-lineservices-line-native layout check-sdk
makepkg --repackage --force --noconfirm
pacman -U --noconfirm mingw-w64-clang-x86_64-dotnet-windowsdesktop-bootstrap-8.0.0-8-any.pkg.tar.zst
python3 check-staged.py /clang64/lib/dotnet
bash "$(_driver)/wpf-tests/check-paragraph-context-sdk.sh" /clang64/lib/dotnet ~/wpfboot/installed-paragraph-context-sdk-check
bash "$(_driver)/wpf-tests/check-flowdocument-sdk.sh" /clang64/lib/dotnet ~/wpfboot/installed-flowdocument-context-check
```

All context/package/SDK checks pass with zero consumer warnings/errors. Private
normal WPF/tab/object/justification/width/context/WinForms build and publish
regressions pass. Installed original paragraph contexts pass false/true optimal-
capable configurations in Ideal/Display modes, source object callbacks, cached
release and dispatcher shutdown across GC. Their portable publishes run with
SDK roots/toolchain PATH removed. Staged/installed 42-managed/pack/native-seed
checks pass. The known srcdir warning remains only source-bootstrap.json
provenance. The final FlowDocument command is intentionally a pending gate:
it now reproducibly fails at absent FsCreatePageFinite in the installed package.
No page visual or optimal penalty calculation is claimed.

Artifact: mingw-w64-clang-x86_64-dotnet-windowsdesktop-bootstrap-8.0.0-8-any.pkg.tar.zst.
SHA-256: 418fef04ed48108b7051af7214253720f020ee8aad7fbcf463d7ac4950f2b0e7.
Recipe commit: msys2-ur 888596e; feature commit: 793a7e9.
Logs, patch/recipe/metadata and the local artifact are archived under
 tmp/wpf8-support/validation/paragraph-context-package-20261013.
Imported native coverage increases from 58 to 65 of 139 (74 absent), adding
actual resource/context ownership interfaces. Page formatting/query/break
records, full bidi/optimal algorithms and natural-language behavior remain
incomplete; the full WPF goal stays active.

## 116. Page ABI records and paragraph writing-direction transforms

The 2026-10-13 continuation extends the source PTS ABI generator to scoped
zero-offset explicit unions and int/uint enums. It emits the original page,
section, track, paragraph, text-detail and single-line records, retaining both
union arms. Each emitted field has a diagnostic native size/offset entry;
reflection against original PresentationFramework checks all 277 fields,
including union fields, rather than only comparing outer record sizes.

PresentationNative now provides FsTransformRectangle and FsTransformBbox.
Coordinate conversion follows the original Pts.cs direction bits, including
horizontal/vertical axes and reversed u/v directions. Outputs may alias inputs,
undefined bounding boxes remain undefined, and wider intermediate arithmetic
rejects coordinate overflow. Geometry outside page boundaries is preserved.
This does not implement bidi text shaping or page formatting.

Validation on win11:

```sh
M612=/home/topkek/dotnetboot/m612 MSYSTEM=CLANG64 WPF_BUILD=/home/topkek/wpfboot/build bash ~/wpfboot/driver/bootstrap/m612/wpf.sh native-win32 inventory check-paragraph-context-native check-paragraph-context
M612=/home/topkek/dotnetboot/m612 MSYSTEM=CLANG64 WPF_BUILD=/home/topkek/wpfboot/build bash ~/wpfboot/driver/bootstrap/m612/wpf.sh check-paragraph-context-native
M612=/home/topkek/dotnetboot/m612 MSYSTEM=CLANG64 WPF_BUILD=/home/topkek/wpfboot/build bash ~/wpfboot/driver/bootstrap/m612/wpf.sh check-paragraph-context
```

All commands pass. Native geometry checks compare all 64 input/output direction
pairs against independent physical corner projection, plus aliasing, undefined/
zero-width/outside-page bounds, overflow and invalid arguments. The original
managed PTS imports pass LTR/RTL conversion and return conversion through the
real CLR marshaler. Original context lifecycle/GC/cache tests still pass, and
the managed consumer builds with zero warnings/errors. Imported-symbol coverage
is 67/139, leaving 72 absent. Evidence is archived in
 tmp/wpf8-support/validation/paragraph-geometry-20261013. FsCreatePageFinite and
full page formatting, optimal algorithms and native language features remain
incomplete. A package update is required to install this source change.

## 117. Installed release 9: paragraph geometry and page ABI validation

The 2026-10-13 continuation packages section 116 as WindowsDesktop 8.0.0-9.
Declared/checksummed 0009-paragraph-geometry-and-page-abi.patch applies to the
pinned bootstrap archive after the eight earlier patches. Clean application
matches the four committed feature files byte for byte. The package workspace
rebuilds its own PresentationNative and refreshes inventory, layout and native/
managed paragraph checks before running the normal SDK regression gates.

Validation in the CLANG64 package workspace:

```sh
makepkg --verifysource --force
source PKGBUILD                 # with srcdir set to this workspace's src/
_wpf native-win32 inventory check-paragraph-context-native check-paragraph-context check-lineservices-line-native layout check-sdk
makepkg --repackage --force --noconfirm
pacman -U --noconfirm mingw-w64-clang-x86_64-dotnet-windowsdesktop-bootstrap-8.0.0-9-any.pkg.tar.zst
python3 check-staged.py /clang64/lib/dotnet
bash "$(_driver)/wpf-tests/check-paragraph-context-sdk.sh" /clang64/lib/dotnet ~/wpfboot/installed-paragraph-geometry-sdk-check
bash "$(_driver)/wpf-tests/check-flowdocument-sdk.sh" /clang64/lib/dotnet ~/wpfboot/installed-flowdocument-geometry-check
```

Package and installed payload checks pass for 42 managed implementations/
references, eight native libraries, SDK tasks and legacy framework versions.
Private normal WPF/tab/inline-object/justification/width/context/WinForms SDK
build/publish/runtime gates pass. Installed original PTS types match all 277
native ABI fields, including nested union arms; LTR/RTL transforms and context
lifecycle/compacting-GC checks pass through the shared framework and a portable
publish with SDK roots/toolchain PATH removed. Consumer builds have zero
warnings/errors. The existing source-bootstrap.json srcdir provenance warning
remains. The last command is a pending diagnostic gate and exits 127 with
EntryPointNotFoundException for FsCreatePageFinite; no completed page-layout
claim is made.

Artifact: mingw-w64-clang-x86_64-dotnet-windowsdesktop-bootstrap-8.0.0-9-any.pkg.tar.zst.
SHA-256: aa7b1cba3024bf5138cf6df11aec4b6bad55acf9b5cf5037e756d015f7f05266.
Recipe commit: msys2-ur ad7e848; feature commit: 0f44e8a.
Artifact, recipe, metadata, patch and validation logs are archived under
 tmp/wpf8-support/validation/paragraph-geometry-package-20261013.
Imported native coverage is 67/139 with 72 absent. FsCreatePageFinite, page/
paragraph formatting and query/break-record services, full bidi/optimal
algorithms and native natural-language behavior remain unfinished. The full
WPF goal stays active.

## 118. Callback-driven finite text pages and original FlowDocument raster

The 2026-10-13 continuation implements finite single-column text pages using
original PTS geometry, section, segment, paragraph-client and line callbacks.
Container paragraphs retain their original managed formatting callback, which
applies margins/borders/padding and recursively calls FsFormatSubtrackFinite.
The source engine owns real page/track/text records and the client line and
line-break handles returned by original WPF. Query exports describe their real
paragraph and source-character ranges, line geometry and continuation records.
Update-clear state propagates through owned subtracks. Page teardown calls the
original subtrack/client/line/break destructors; context destruction rejects live
resources. Callback allocation failures unwind owned state. A callback that
successfully exports a subtrack and then resets its out handle on failure also
releases that unreported subtrack through a reentry transaction.

The ABI generator derives callback signatures from original Pts.cs and adds
FSSUBTRACKDETAILS. Original managed types match all 281 native field sizes/
offsets. PresentationNative provides 80/139 imported symbols (13 added, 59
absent). Supported pages finish on the first page. Overflow requires real page
break records and explicitly rejects; no consumed-content or empty-page success
is fabricated. Multiple columns, RTL page text, optimal/keep policies, tables,
floaters/figures and bottomless layout remain unfinished.

Validation on win11:

```sh
M612=/home/topkek/dotnetboot/m612 MSYSTEM=CLANG64 WPF_BUILD=/home/topkek/wpfboot/build bash ~/wpfboot/driver/bootstrap/m612/wpf.sh native-win32 check-paragraph-context-native
M612=/home/topkek/dotnetboot/m612 MSYSTEM=CLANG64 WPF_BUILD=/home/topkek/wpfboot/build bash ~/wpfboot/driver/bootstrap/m612/wpf.sh check-paragraph-context-native inventory check-paragraph-context layout check-sdk
bash ~/wpfboot/driver/bootstrap/m612/wpf-tests/check-flowdocument-sdk.sh ~/wpfboot/build/sdk-check-root ~/wpfboot/finite-page-sdk-check
```

All final commands pass. The native gate verifies actual callback page geometry,
paragraph and character counts, line break/height data, capacity/foreign/stale
query errors, update clearing, exact recursive teardown, failed allocations,
reset-output subtrack rollback, page overflow and unfinished column rejection.
Original FlowDocument.GetPage(0) now succeeds, replacing the previous missing
FsCreatePageFinite failure. Stronger normal SDK/shared-framework and portable
publish checks render short and wrapped two-paragraph documents, in Ideal and
Display modes, across compacting GC. Their four raster cases contain
1663/5971/1291/4732 dark glyph pixels and 20/75/20/60 ink rows respectively.
Cached pages, the real end-of-document/page count and dispatcher teardown pass.
The FlowDocument SDK check is now included in step_check_sdk. Other normal
WPF/tab/inline-object/justification/width/context/WinForms gates also pass with
zero consumer warnings/errors. Evidence is archived under
 tmp/wpf8-support/validation/finite-page-20261013. Package integration follows;
full pagination, bottomless/advanced paragraph layout and native text/language
behavior remain required for the full WPF goal.

## 119. Installed release 10: original finite FlowDocument layout and raster

The 2026-10-13 continuation packages section 118 as WindowsDesktop 8.0.0-10.
Declared/checksummed 0010-finite-text-pages.patch carries page/subtrack/text
ownership and formatting, source-derived callback signatures, native failure/
lifecycle fixtures and the original FlowDocument SDK raster gate. Clean pinned
sources plus all ten patches match the five committed feature files byte for
byte. The private package workspace rebuilds its own native library, inventory
and layout, passes the new native/original context checks, and runs the complete
normal SDK checks including FlowDocument before creating and installing the
archive. Unchanged managed/graphics/printing outputs remain its own source
builds. The README's current native-coverage count is also refreshed.

Validation in the CLANG64 package workspace:

```sh
makepkg --verifysource --force
source PKGBUILD                 # with srcdir set to this workspace's src/
_wpf native-win32 inventory check-paragraph-context-native check-paragraph-context check-lineservices-line-native layout check-sdk
makepkg --repackage --force --noconfirm
pacman -U --noconfirm mingw-w64-clang-x86_64-dotnet-windowsdesktop-bootstrap-8.0.0-10-any.pkg.tar.zst
/usr/bin/python3 check-staged.py /clang64/lib/dotnet
bash "$(_driver)/wpf-tests/check-flowdocument-sdk.sh" /clang64/lib/dotnet ~/wpfboot/installed-finite-page-sdk-check
bash "$(_driver)/wpf-tests/check-paragraph-context-sdk.sh" /clang64/lib/dotnet ~/wpfboot/installed-finite-context-sdk-check
```

All commands pass. The installed 42-managed/eight-native/SDK/legacy payload
check passes. Original FlowDocument finite-page creation, short/wrapped
paragraph raster, Ideal/Display formatting, cached page retention across
compacting GC, end-of-document page count and dispatcher cleanup pass through
both the installed shared framework and a self-contained publish with SDK
roots/toolchain PATH removed. Original PTS ABI/context/geometry checks also pass
for all 281 field sizes/offsets and resource configurations. Private normal
WPF/tab/inline-object/justification/width/context/FlowDocument/WinForms regression
gates pass. Consumer builds have zero warnings/errors; source-bootstrap.json
retains the existing srcdir provenance warning.

Artifact: mingw-w64-clang-x86_64-dotnet-windowsdesktop-bootstrap-8.0.0-10-any.pkg.tar.zst.
SHA-256: 6637f1ad9843dbba8f4ec0c2ba8f2d0ae4cf6438624cbfa8b2d38c7feaec80ef.
Recipe commit: msys2-ur 9b8ac55; feature commit: 6621bcb.
Artifact, recipe, metadata, patch and logs are archived under
 tmp/wpf8-support/validation/finite-page-package-20261013.
Imported native coverage is 80/139 with 59 absent. FsCreatePageFinite now
formats real supported text pages, resolving the release 9 entry-point failure.
Multi-page continuation/page break records, bottomless layout, multiple columns,
RTL page text, optimal/keep policies and tables/floaters/figures remain open.
Full bidi/native natural-language and broader runtime paths are also unfinished;
the full WPF goal stays active.

## 120. Bottomless scrolling/editor layout and corrected page line coordinates

The 2026-10-13 continuation implements FsCreatePageBottomless,
FsFormatSubtrackBottomless and FsUpdateBottomlessPage. Bottomless containers use
their original ObjFormatParaBottomless callback, including managed margin/
border/padding handling, and retain the shared source line/query/resource
engine. Query rectangles report actual consumed content height. Page updates
build a replacement tree, publish it under the existing page handle only after
successful formatting, and release superseded clients/lines outside the registry
lock. Failed replacement preserves the previous valid tree; the managed caller
can then dispose it normally. Updates currently reformat the full page and do
not claim the original incremental/interruptible algorithm.

The initial scrolling probe exposed an error in release 10: single-line query
records used paragraph-relative u/v positions, while original TextParaClient
positions its line visuals in page coordinates. This made paragraphs overlap
and scrolled viewports appear blank, despite the earlier nonempty-raster check.
Line descriptors and ink bounding boxes now use actual page coordinates. Native
geometry assertions and a new two-color finite-page raster test verify page
padding and nonoverlapping paragraph rows: red at y=19..38 and blue starting
y=45, with first red ink at x=14 after 12-unit page padding.

Validation on win11:

```sh
M612=/home/topkek/dotnetboot/m612 MSYSTEM=CLANG64 WPF_BUILD=/home/topkek/wpfboot/build bash ~/wpfboot/driver/bootstrap/m612/wpf.sh native-win32 check-paragraph-context-native
M612=/home/topkek/dotnetboot/m612 MSYSTEM=CLANG64 WPF_BUILD=/home/topkek/wpfboot/build bash ~/wpfboot/driver/bootstrap/m612/wpf.sh check-paragraph-context-native
bash ~/wpfboot/driver/bootstrap/m612/wpf-tests/check-bottomless-sdk.sh ~/wpfboot/build/sdk-check-root ~/wpfboot/bottomless-sdk-check
bash ~/wpfboot/driver/bootstrap/m612/wpf-tests/check-flowdocument-sdk.sh ~/wpfboot/build/sdk-check-root ~/wpfboot/bottomless-finite-sdk-check
M612=/home/topkek/dotnetboot/m612 MSYSTEM=CLANG64 WPF_BUILD=/home/topkek/wpfboot/build bash ~/wpfboot/driver/bootstrap/m612/wpf.sh inventory layout check-sdk
```

All commands pass. Native tests verify bottomless content beyond finite bounds,
actual height, stable page identity, replacement and failed-update retention,
stale query rejection and exact callback cleanup. Original
FlowDocumentScrollViewer lays out 21 paragraphs and renders top/end viewports;
editing grows the extent from 1695.8 to 1749.0 in Ideal and from 1756.0 to 1840.0
in Display. Original RichTextBox renders initial and appended text, retaining
correct text-container content and changed raster. Shared-framework and
self-contained publishes with SDK roots/toolchain PATH removed pass compacting
GC and dispatcher cleanup. Finite FlowDocument placement and the usual
WPF/tab/inline-object/justification/width/context/WinForms regressions pass;
consumer builds have zero warnings/errors. step_check_sdk now includes the
bottomless gate. Imported native coverage is 83/139 with 56 absent.
Evidence is archived under tmp/wpf8-support/validation/bottomless-20261013.
Package integration follows. Multi-page continuation, advanced/incremental/
interruptible paragraph layout, full bidi/optimal/native language behavior and
broader runtime paths remain required for the full WPF goal.

## 121. Installed release 11: scrolling/editor layout and line-placement correction

The 2026-10-13 continuation packages section 120 as WindowsDesktop 8.0.0-11.
Declared/checksummed 0011-bottomless-documents-and-page-coordinates.patch applies
after the ten earlier patches. Clean pinned sources plus all eleven patches
match the seven committed bottomless/coordinate feature files byte for byte.
The private package tree rebuilds PresentationNative, inventory and layout,
passes native ABI/resource/geometry and original context checks, and runs normal
SDK tests including finite paragraph placement and bottomless scrolling/editing
before creating and installing the archive. Unchanged managed/graphics/printing
outputs remain its own source builds.

Validation in the CLANG64 package workspace:

```sh
makepkg --verifysource --force
source PKGBUILD                 # with srcdir set to this workspace's src/
_wpf native-win32 inventory check-paragraph-context-native check-paragraph-context check-lineservices-line-native layout check-sdk
makepkg --repackage --force --noconfirm
pacman -U --noconfirm mingw-w64-clang-x86_64-dotnet-windowsdesktop-bootstrap-8.0.0-11-any.pkg.tar.zst
/usr/bin/python3 check-staged.py /clang64/lib/dotnet
bash "$(_driver)/wpf-tests/check-bottomless-sdk.sh" /clang64/lib/dotnet ~/wpfboot/installed-bottomless-sdk-check
bash "$(_driver)/wpf-tests/check-flowdocument-sdk.sh" /clang64/lib/dotnet ~/wpfboot/installed-bottomless-finite-sdk-check
```

All commands pass. Staged/installed 42-managed/eight-native/SDK/legacy payload
checks pass. Original FlowDocumentScrollViewer top/end glyph rasters, full
content extent, source-text edits and original RichTextBox insertion/raster pass
in Ideal/Display modes through the installed shared framework and self-contained
publishes with SDK roots/toolchain PATH removed. Compacting GC and dispatcher
cleanup pass. Finite FlowDocument text/count/cached-page checks pass along with
the new colored paragraph placement test, correcting the release 10 overlap.
Private normal WPF/tab/inline-object/justification/width/context/finite/bottomless/
WinForms gates pass with zero consumer warnings/errors. The known
source-bootstrap.json srcdir provenance warning remains.

Artifact: mingw-w64-clang-x86_64-dotnet-windowsdesktop-bootstrap-8.0.0-11-any.pkg.tar.zst.
SHA-256: 2dde824e74d6ac6474e5122b19380e8166a975ead46588d55ca1b7e757b9a6da.
Recipe commit: msys2-ur 4dd70f9; feature commit: dae8792.
Artifact, recipe, metadata, patch and validation logs are archived under
 tmp/wpf8-support/validation/bottomless-package-20261013.
Imported native coverage is 83/139 with 56 absent. Supported bottomless text
pages can scroll and reformat after edits; the page update currently replaces
the full layout tree. Finite multi-page continuation/break records, advanced
paragraph/incremental/interruptible algorithms, multiple columns, RTL page text,
full bidi/optimal/native language behavior and broader runtime paths remain
incomplete. The full WPF goal stays active.

## 122. Finite multi-page text continuation and independent break-record ownership

The 2026-10-13 continuation replaces the finite-page overflow rejection with
actual continuation through original paragraph/container/line callbacks.
Continuation nodes retain the segment/paragraph position, character offset,
original line-break state and nested container record. Shared owned line-break
records outlive disposed pages and are released once after all dependent pages/
continuations are gone. FsDestroyPageBreakRecord, FsDestroySubtrackBreakRecord
and FsDuplicateSubtrackBreakRecord provide original record lifetime APIs;
subtrack duplicates have independent wrapper identities and share immutable
continuation state. Wrong record kinds, stale handles and foreign contexts reject
without dereferencing client memory.

Finite formatting retains only accepted lines. Bounded lookahead checks the
original minimum widow/orphan line counts; speculative lines and output break
records are disposed, while the continuation begins at the first unaccepted
character. At a full track, a new paragraph can be deferred without claiming
that it was formatted. A resumed original container receives its own subtrack
record and preserves the original managed margin/padding and chunk callbacks.
Reset-output callback failures now roll back both subtracks and their records.
Tiny pages that cannot fit one line still reject; hard/keep/optimal/multicolumn
policies and forced oversized-line progress remain unfinished.

Validation on win11:

```sh
M612=/home/topkek/dotnetboot/m612 MSYSTEM=CLANG64 WPF_BUILD=/home/topkek/wpfboot/build bash ~/wpfboot/driver/bootstrap/m612/wpf.sh native-win32 check-paragraph-context-native
bash ~/wpfboot/driver/bootstrap/m612/wpf-tests/check-pagination-sdk.sh ~/wpfboot/build/sdk-check-root ~/wpfboot/pagination-sdk-check
M612=/home/topkek/dotnetboot/m612 MSYSTEM=CLANG64 WPF_BUILD=/home/topkek/wpfboot/build bash ~/wpfboot/driver/bootstrap/m612/wpf.sh check-paragraph-context-native inventory layout check-sdk
```

All commands pass. Native tests cover four short pages with exact paragraph/
character ranges, input/page-independent record lifetime, cloned subtrack resume
after original release, context retention, wrong/double record disposal and exact
callback cleanup. The original WPF paginator renders six pages in both Ideal
and Display modes. Its original ITextView.TextSegments cover every one of 1278
source characters exactly once; DynamicDocumentPaginator.GetPageNumber agrees
for every forward-affinity character position. Page-boundary pointer affinity
uses WPF's original Contains contract; backward pointers can belong to the
previous page at the shared boundary. All pages render real glyphs (58293 total
dark pixels Ideal, 45806 Display), disposed early pages regenerate after later
records exist, and page/break-record dispatcher cleanup passes across compacting
GC. Normal shared-framework and self-contained publishes with SDK roots/toolchain
PATH removed pass. step_check_sdk now includes the pagination gate alongside
finite placement, scrolling/editor and earlier WPF/WinForms tests; consumer
builds have zero warnings/errors. Imported native coverage is 86/139 with 53
absent. Evidence is archived under
 tmp/wpf8-support/validation/pagination-20261013. Package integration follows;
advanced layout, full bidi/optimal/native language and broader runtime behavior
remain required for the full WPF goal.

## 123. Installed release 12: finite multi-page text pagination

The 2026-10-13 continuation packages section 122 as WindowsDesktop 8.0.0-12.
Declared/checksummed 0012-finite-pagination.patch applies after the eleven
previous patches. Clean pinned sources plus all twelve patches match the six
committed pagination feature files byte for byte. The private package tree
rebuilds its native library, inventory and layout, passes continuation/clone/
callback rollback checks and original context checks, and runs all normal SDK
checks including six-page source coverage before packaging and installing.
Unchanged managed/graphics/printing outputs remain its own source builds.

Validation in the CLANG64 package workspace:

```sh
makepkg --verifysource --force
source PKGBUILD                 # with srcdir set to this workspace's src/
_wpf native-win32 inventory check-paragraph-context-native check-paragraph-context check-lineservices-line-native layout check-sdk
makepkg --repackage --force --noconfirm
pacman -U --noconfirm mingw-w64-clang-x86_64-dotnet-windowsdesktop-bootstrap-8.0.0-12-any.pkg.tar.zst
/usr/bin/python3 check-staged.py /clang64/lib/dotnet
bash "$(_driver)/wpf-tests/check-pagination-sdk.sh" /clang64/lib/dotnet ~/wpfboot/installed-pagination-sdk-check
bash "$(_driver)/wpf-tests/check-bottomless-sdk.sh" /clang64/lib/dotnet ~/wpfboot/installed-pagination-bottomless-sdk-check
```

All commands pass. Staged/installed payload checks pass for the 42 managed
assemblies/references, eight native libraries, WindowsDesktop SDK and preserved
legacy versions. Installed original FlowDocument pagination renders six pages,
covers all 1278 source characters exactly once, maps every forward-affinity
content position to its page, regenerates disposed pages and passes compacting
GC plus page/break-record dispatcher cleanup in Ideal/Display. Shared-framework
and self-contained publishes with SDK roots/toolchain PATH removed pass. Original
scrolling/editor extent, raster and text insertion checks also pass through the
installed package. Private normal WPF/tab/inline-object/justification/width/
context/finite/bottomless/pagination/WinForms regressions pass; consumer builds
have zero warnings/errors. The existing source-bootstrap.json srcdir provenance
warning remains.

Artifact: mingw-w64-clang-x86_64-dotnet-windowsdesktop-bootstrap-8.0.0-12-any.pkg.tar.zst.
SHA-256: 25cedf08bbb7c8450b2a642c20480684682919987258bb595905773363f772b4.
Recipe commit: msys2-ur c64be9d; feature commit: 680436d.
Artifact, recipe, metadata, patch and validation logs are archived under
 tmp/wpf8-support/validation/pagination-package-20261013.
Imported native coverage is 86/139 with 53 absent. Finite text continuation is
now real; tiny-page forced progress, hard/keep/optimal policies, multiple columns,
advanced/incremental/interruptible paragraph algorithms, full bidi/native language
and broader runtime behavior remain incomplete. The full WPF goal stays active.

## 124. Original multi-column text pages and section query hierarchy

The 2026-10-13 continuation reads original GetSectionColumnInfo callback widths/
gaps and formats finite tracks sequentially across the columns. Continuation
from the previous page enters the first column; subsequent columns resume the
same source position with the correct same-page break flag. Completed documents
retain empty trailing tracks without restarting their text. Before client
allocation, the complete column geometry is validated against page margins.

Complex pages own a real section and every column track. FsQueryPageSectionList,
FsQuerySectionDetails and FsQuerySectionBasicColumnList expose original ABI
records used by WPF to arrange/render columns and their rules. Resource teardown
invalidates the whole section/column hierarchy; update clearing propagates to
all tracks/paragraphs, and later-column failures unwind earlier ownership.
The source ABI generator supports pointer callback parameters and FSCOLUMNINFO;
all 283 field sizes/offsets match the original managed types. Single-column and
bottomless paths retain their existing record representation. Imported native
coverage rises to 89/139 (three new queries, 50 absent).

Validation on win11:

```sh
M612=/home/topkek/dotnetboot/m612 MSYSTEM=CLANG64 WPF_BUILD=/home/topkek/wpfboot/build bash ~/wpfboot/driver/bootstrap/m612/wpf.sh native-win32 inventory
M612=/home/topkek/dotnetboot/m612 MSYSTEM=CLANG64 WPF_BUILD=/home/topkek/wpfboot/build bash ~/wpfboot/driver/bootstrap/m612/wpf.sh check-paragraph-context-native check-paragraph-context
bash ~/wpfboot/driver/bootstrap/m612/wpf-tests/check-columns-sdk.sh ~/wpfboot/build/sdk-check-root ~/wpfboot/columns-sdk-check
M612=/home/topkek/dotnetboot/m612 MSYSTEM=CLANG64 WPF_BUILD=/home/topkek/wpfboot/build bash ~/wpfboot/driver/bootstrap/m612/wpf.sh check-paragraph-context-native inventory layout check-sdk
```

All commands pass. Native tests verify exact callback widths/gaps, per-column
paragraph/line identity and page coordinates, real section/track counts, capacity
and stale query rejection, recursive update clearing, invalid bounds before
allocation and cleanup of a failure in the later column. The original WPF
paginator renders two fixed columns with a 30-unit gap and red 2-unit rule,
creating 12 pages in Ideal and 9 in Display. Every one of 1278 source characters
belongs to exactly one original ITextView page range and maps to that page through
DynamicDocumentPaginator. Raster checks require glyph pixels in both first-page
columns, zero dark glyph pixels in the gap, and at least 100 red rule pixels
(observed 400). All pages render text; totals are 59629 dark pixels Ideal and
45806 Display. Disposed-page regeneration, compacting GC and dispatcher cleanup
pass through normal shared-framework and self-contained SDK publishes with SDK
roots/toolchain PATH removed. step_check_sdk now includes the column gate; the
single-column/bottomless/earlier WPF/WinForms regressions pass with zero consumer
warnings/errors. Evidence is archived under
 tmp/wpf8-support/validation/columns-20261013. Package integration follows.
Column balancing/span areas, hard/keep/optimal/forced-line policies, tables/
floaters/figures, incremental/interruptible algorithms, full bidi/native language
and broader runtime behavior remain required for the full WPF goal.

## 125. Installed release 13: finite multi-column text layout

The 2026-10-13 continuation packages section 124 as WindowsDesktop 8.0.0-13.
Declared/checksummed 0013-column-layout.patch applies after the twelve earlier
patches. Clean pinned sources plus all thirteen patches match the six committed
column feature files byte for byte. The private package tree rebuilds its native
library, inventory and layout, passes original ABI/context and native column/
resource/failure checks, and runs normal SDK checks including multi-column
pagination before packaging and installing. Unchanged managed/graphics/printing
outputs remain its own source builds.

Validation in the CLANG64 package workspace:

```sh
makepkg --verifysource --force
source PKGBUILD                 # with srcdir set to this workspace's src/
_wpf native-win32 inventory check-paragraph-context-native check-paragraph-context check-lineservices-line-native layout check-sdk
makepkg --repackage --force --noconfirm
pacman -U --noconfirm mingw-w64-clang-x86_64-dotnet-windowsdesktop-bootstrap-8.0.0-13-any.pkg.tar.zst
/usr/bin/python3 check-staged.py /clang64/lib/dotnet
bash "$(_driver)/wpf-tests/check-columns-sdk.sh" /clang64/lib/dotnet ~/wpfboot/installed-columns-sdk-check
bash "$(_driver)/wpf-tests/check-pagination-sdk.sh" /clang64/lib/dotnet ~/wpfboot/installed-columns-pagination-sdk-check
```

All commands pass. Staged/installed 42-managed/eight-native/SDK/legacy payload
checks pass. Installed original WPF column pages render 12 Ideal/9 Display pages,
cover every one of 1278 source characters exactly once, map content positions to
their pages, render glyphs in both first-page columns with no dark glyphs in the
gap, and render the red column rule. Original page regeneration, compacting GC
and dispatcher cleanup pass through the installed shared framework and portable
publishes with SDK roots/toolchain PATH removed. Installed single-column
pagination still renders six pages with exact character coverage in both modes.
Private normal WPF/tab/inline-object/justification/width/context/finite/bottomless/
pagination/columns/WinForms regression gates pass with zero consumer warnings/
errors. The source-bootstrap.json srcdir provenance warning remains.

Artifact: mingw-w64-clang-x86_64-dotnet-windowsdesktop-bootstrap-8.0.0-13-any.pkg.tar.zst.
SHA-256: b3e7c79a7706e4a17e009ea45e6d1303d7e84583290572823f10b9cb79eeb33a.
Recipe commit: msys2-ur 6afec39; feature commit: c951bf4.
Artifact, recipe, metadata, patch and logs are archived under
 tmp/wpf8-support/validation/columns-package-20261013.
Imported native coverage is 89/139 with 50 absent. Multi-column text can now
continue within a page and onto later pages using original geometry/callbacks.
Balancing/span areas, hard/keep/optimal/forced-line policies, tables/floaters/
figures, advanced/incremental/interruptible algorithms, full bidi/native language
and broader runtime behavior remain incomplete. The full WPF goal stays active.

## 126. Explicit paragraph page/column breaks through nested containers

The 2026-10-13 continuation implements original FSPAP BreakPageBefore and
BreakColumnBefore behavior for finite text tracks. A page break stops the whole
page and preserves empty remaining columns; a column break resumes in the next
column. Original suppression flags apply at the destination: page start
suppresses page/column breaks on the first block, column start suppresses the
already-satisfied column break. A continued paragraph does not repeat a break
that was already applied before its first chunk. Bottomless documents ignore
these finite-only boundaries.

Container callbacks retain and propagate the exact page/column stop reason and
original suppression argument through FsFormatSubtrackFinite. A no-progress
resumed container preserves its existing immutable continuation rather than
restarting its children. Page-level continuation/stop state is now independent
of trailing empty column tracks, so skipping columns for a hard page break cannot
lose the next paragraph. These improve existing exports; native coverage remains
89/139 with 50 absent.

Validation on win11:

```sh
M612=/home/topkek/dotnetboot/m612 MSYSTEM=CLANG64 WPF_BUILD=/home/topkek/wpfboot/build bash ~/wpfboot/driver/bootstrap/m612/wpf.sh native-win32 check-paragraph-context-native
bash ~/wpfboot/driver/bootstrap/m612/wpf-tests/check-hard-breaks-sdk.sh ~/wpfboot/build/sdk-check-root ~/wpfboot/hard-break-sdk-check
M612=/home/topkek/dotnetboot/m612 MSYSTEM=CLANG64 WPF_BUILD=/home/topkek/wpfboot/build bash ~/wpfboot/driver/bootstrap/m612/wpf.sh inventory layout check-sdk
```

All commands pass. Native tests verify explicit page stops despite spare column
space, empty skipped columns, same-page column resume, exact-once destination
suppression and callback/break-record cleanup. Original WPF tests cover flat
paragraphs and the same paragraphs inside a nested Section, both Ideal and
Display. First-block page/column flags do not create a blank first page; red/blue
paragraphs occupy separate columns on page 0; green/black paragraphs occupy page
1 in order. All 56 source characters map to the expected page, colored glyph
bounds verify placement and compacting GC/dispatcher cleanup pass. Shared-
framework and portable publishes run with SDK roots/toolchain PATH removed.
The raster fixture distinguishes neutral black ink from antialiased green ink.
step_check_sdk includes the hard-break gate; earlier normal WPF/finite/bottomless/
pagination/columns/WinForms regressions pass with zero consumer warnings/errors.
Evidence is archived under tmp/wpf8-support/validation/hard-breaks-20261013.
Package integration follows. Keep/optimal/forced-line policies, balancing/span
areas, tables/floaters/figures, advanced/incremental/interruptible algorithms,
full bidi/native language and broader runtime behavior remain required for the
full WPF goal.

## 127. Installed release 14: explicit paragraph page/column boundaries

The 2026-10-13 continuation packages section 126 as WindowsDesktop 8.0.0-14.
Declared/checksummed 0014-explicit-paragraph-breaks.patch applies after the
thirteen prior patches. Clean pinned sources plus all fourteen patches match
the five committed feature files byte for byte. The private package tree
rebuilds native output, inventory and layout, passes resource/break/native
checks, and runs all normal SDK gates including flat/nested paragraph boundary
placement before creating and installing the archive. Unchanged managed/
graphics/printing outputs remain its own source builds.

Validation in the CLANG64 package workspace:

```sh
makepkg --verifysource --force
source PKGBUILD                 # with srcdir set to this workspace's src/
_wpf native-win32 inventory check-paragraph-context-native check-paragraph-context check-lineservices-line-native layout check-sdk
makepkg --repackage --force --noconfirm
pacman -U --noconfirm mingw-w64-clang-x86_64-dotnet-windowsdesktop-bootstrap-8.0.0-14-any.pkg.tar.zst
/usr/bin/python3 check-staged.py /clang64/lib/dotnet
bash "$(_driver)/wpf-tests/check-hard-breaks-sdk.sh" /clang64/lib/dotnet ~/wpfboot/installed-hard-break-sdk-check
bash "$(_driver)/wpf-tests/check-columns-sdk.sh" /clang64/lib/dotnet ~/wpfboot/installed-hard-break-columns-sdk-check
```

All commands pass. Staged/installed 42-managed/eight-native/SDK/legacy payload
checks pass. Installed original WPF flat/nested Section cases render first-block
suppression and explicit column/page destinations in Ideal/Display, map all 56
characters to the correct page, and pass compacting GC/dispatcher cleanup through
shared-framework and self-contained publishes with SDK roots/toolchain PATH
removed. Installed multi-column pagination still covers all 1278 characters
exactly once across 12 Ideal/9 Display pages and passes glyph/gap/rule checks.
Private normal WPF/tab/inline-object/justification/width/context/finite/bottomless/
pagination/columns/hard-break/WinForms regressions pass with zero consumer
warnings/errors. The source-bootstrap.json srcdir provenance warning remains.

Artifact: mingw-w64-clang-x86_64-dotnet-windowsdesktop-bootstrap-8.0.0-14-any.pkg.tar.zst.
SHA-256: 1540deb4007c9a7c337488f5337cb8be0adf24f764cbef6f1b790d9296f92a47.
Recipe commit: msys2-ur 99fe34d; feature commit: 6593d4a.
Artifact, recipe, metadata, patch and logs are archived under
 tmp/wpf8-support/validation/hard-breaks-package-20261013.
Imported native coverage remains 89/139 with 50 absent: this release implements
additional behavior of existing exports. Keep/optimal/forced-line policies,
balancing/span areas, tables/floaters/figures, advanced/incremental/interruptible
algorithms, full bidi/native language and broader runtime behavior remain
incomplete. The full WPF goal stays active.

## 128. KeepTogether paragraph deferral and fresh-area progress

The 2026-10-13 continuation implements original FSTXTPROPS KeepTogether behavior
for finite text paragraphs. A fresh kept paragraph that cannot fit the remaining
area is deferred without consuming any of its characters. At a fresh page or
column, an oversized kept paragraph can still split, so the policy cannot cause
an endless sequence of empty pages. Completed/resumed chunks keep original
source ranges and line-break ownership. Nested object callbacks now propagate
the inherited fEmptyOk argument: an empty subtrack inside a container that started
mid-page does not count as a fresh page for relaxing keep constraints. Bottomless
paragraphs naturally retain all content without a finite boundary. These improve
existing exports; native coverage remains 89/139 with 50 absent.

Validation on win11:

```sh
M612=/home/topkek/dotnetboot/m612 MSYSTEM=CLANG64 WPF_BUILD=/home/topkek/wpfboot/build bash ~/wpfboot/driver/bootstrap/m612/wpf.sh native-win32 check-paragraph-context-native
bash ~/wpfboot/driver/bootstrap/m612/wpf-tests/check-keep-together-sdk.sh ~/wpfboot/build/sdk-check-root ~/wpfboot/keep-sdk-check
M612=/home/topkek/dotnetboot/m612 MSYSTEM=CLANG64 WPF_BUILD=/home/topkek/wpfboot/build bash ~/wpfboot/driver/bootstrap/m612/wpf.sh inventory layout check-sdk
```

All commands pass. Native tests verify intact deferral after preceding text,
resume from the first character, oversized keep progress across four short pages,
and exact accepted/speculative line/client/break teardown. Original WPF compares
the same document with KeepTogether false/true, in flat/nested Section form and
Ideal/Display. Without keep, the paragraph spans pages 0..1; with keep, all its
characters remain on page 1, while preceding text stays on page 0. The same
raster content is retained (3439 total dark pixels Ideal, 2644 Display), and
source/page mapping, disposed-page regeneration and compacting GC pass. An
11-line oversized kept paragraph completes in three rendered pages in both
modes. A two-column case renders preceding red text and the intact blue kept
paragraph in separate columns on a single page. Normal shared-framework and
self-contained publishes with SDK roots/toolchain PATH removed pass dispatcher
cleanup. step_check_sdk now includes the keep-together gate; prior normal WPF/
finite/bottomless/pagination/columns/hard-break/WinForms regressions pass with
zero consumer warnings/errors. Evidence is archived under
 tmp/wpf8-support/validation/keep-together-20261013. Package integration follows.
KeepWithNext chains, optimal/forced-line policies, balancing/span areas, tables/
floaters/figures, advanced/incremental/interruptible algorithms, full bidi/native
language and broader runtime behavior remain required for the full WPF goal.

## 129. Installed release 15: KeepTogether text paragraphs

The 2026-10-13 continuation packages section 128 as WindowsDesktop 8.0.0-15.
Declared/checksummed 0015-keep-together-paragraphs.patch applies after the fourteen
prior patches. Clean pinned sources plus all fifteen patches match the five
committed feature files byte for byte. The private package tree rebuilds native
output, inventory and layout, passes keep/resource/native checks, and runs all
normal SDK gates including original keep comparisons before archive creation and
installation. Unchanged managed/graphics/printing outputs remain its own source
builds.

Validation in the CLANG64 package workspace:

```sh
makepkg --verifysource --force
source PKGBUILD                 # with srcdir set to this workspace's src/
_wpf native-win32 inventory check-paragraph-context-native check-paragraph-context check-lineservices-line-native layout check-sdk
makepkg --repackage --force --noconfirm
pacman -U --noconfirm mingw-w64-clang-x86_64-dotnet-windowsdesktop-bootstrap-8.0.0-15-any.pkg.tar.zst
/usr/bin/python3 check-staged.py /clang64/lib/dotnet
bash "$(_driver)/wpf-tests/check-keep-together-sdk.sh" /clang64/lib/dotnet ~/wpfboot/installed-keep-sdk-check
bash "$(_driver)/wpf-tests/check-columns-sdk.sh" /clang64/lib/dotnet ~/wpfboot/installed-keep-columns-sdk-check
```

All commands pass. Staged/installed 42-managed/eight-native/SDK/legacy payload
checks pass. Original installed flat/nested WPF comparisons render the same
paragraph split across pages with keep disabled and intact on page 1 with keep
enabled, in Ideal/Display. Preceding source remains on page 0. Oversized kept
paragraphs complete in three rendered pages, and a kept paragraph can move
intact to the next column on one page. Source/page mappings, disposed-page
regeneration, glyph placement, compacting GC and dispatcher cleanup pass through
shared-framework and self-contained publishes with SDK roots/toolchain PATH
removed. Installed two-column pagination still renders 12 Ideal/9 Display pages
and covers all 1278 characters exactly once. Private normal WPF/tab/inline-object/
justification/width/context/finite/bottomless/pagination/columns/hard-break/keep/
WinForms regressions pass with zero consumer warnings/errors. The existing
source-bootstrap.json srcdir provenance warning remains.

Artifact: mingw-w64-clang-x86_64-dotnet-windowsdesktop-bootstrap-8.0.0-15-any.pkg.tar.zst.
SHA-256: 164ee7de1b1ced17f16974aa43273e48a59165df19438a44f4973843bd2711da.
Recipe commit: msys2-ur e5d96a1; feature commit: 8eddd09.
Artifact, recipe, metadata, patch and logs are archived under
 tmp/wpf8-support/validation/keep-together-package-20261013.
Imported native coverage remains 89/139 with 50 absent: KeepTogether is new
behavior of existing exports. KeepWithNext chains, optimal/forced-line policies,
balancing/span areas, tables/floaters/figures, advanced/incremental/interruptible
algorithms, full bidi/native language and broader runtime behavior remain
incomplete. The full WPF goal stays active.

## 130. KeepWithNext heading chains and margin-preserving rollback

The 2026-10-13 continuation implements FSPAP KeepWithNext chains. The formatter
retains the immutable source position at the beginning of each chain; if no
content of the following paragraph fits, it removes accepted chain members,
recomputes retained height/ink and resumes the chain in the next area. Earlier
source stays on the current page. Original DuplicateMcsclient/DestroyMcsclient
callbacks preserve and release the margin-collapsing state before the chain, so
rollback does not substitute the deferred group's margin state. Oversized chains
that start at a fresh area can relax the constraint for bounded progress. Explicit
page/column breaks retain precedence over incompatible keep constraints.

The original Paragraph.KeepWithNext contract also implies that its own paragraph
is unbroken. Native subtrack formatting reads original owner paragraph properties
and propagates this constraint to its builtin text child, while keeping nested
fresh-area logic from section 128. Bottomless formatting has no finite keep
boundary. These are behaviors of existing exports: coverage remains 89/139 with
50 absent; ABI records still match all 283 original field sizes/offsets.

Validation on win11:

```sh
M612=/home/topkek/dotnetboot/m612 MSYSTEM=CLANG64 WPF_BUILD=/home/topkek/wpfboot/build bash ~/wpfboot/driver/bootstrap/m612/wpf.sh native-win32 check-paragraph-context-native
bash ~/wpfboot/driver/bootstrap/m612/wpf-tests/check-keep-next-sdk.sh ~/wpfboot/build/sdk-check-root ~/wpfboot/keep-next-sdk-check
M612=/home/topkek/dotnetboot/m612 MSYSTEM=CLANG64 WPF_BUILD=/home/topkek/wpfboot/build bash ~/wpfboot/driver/bootstrap/m612/wpf.sh check-sdk
```

All final commands pass. Native tests verify accepted heading rollback, exact
preceding/chain source preservation, intact resume and accepted/speculative
callback teardown. Original WPF compares one- and three-heading chains with keep
false/true in flat/nested Section documents, Ideal/Display. Disabled headings
remain on page 0; enabled chains move with their body to page 1. Every source
character maps to the required page, rendered pages regenerate across compacting
GC, and oversized chains complete in two pages. Additional cases check nonzero
collapsed margins (with an intact following body), an unbroken multi-line heading,
and heading/body glyphs together in the second column on a single page. The
raster gate accepts colored text as well as black ink. Normal shared-framework
and self-contained publishes run with SDK roots/toolchain PATH removed and pass
dispatcher cleanup. step_check_sdk now includes the chain gate; the earlier
normal WPF/finite/bottomless/pagination/column/hard-break/KeepTogether/WinForms
regressions pass with zero consumer warnings/errors. Evidence is archived under
 tmp/wpf8-support/validation/keep-next-20261013. Package integration follows.
Optimal/forced-line policies, balancing/span areas, tables/floaters/figures,
advanced/incremental/interruptible algorithms, full bidi/native language and
broader runtime behavior remain required for the full WPF goal.

## 131. Installed release 16: KeepWithNext heading chains

The 2026-10-13 continuation packages section 130 as WindowsDesktop 8.0.0-16.
Declared/checksummed 0016-keep-with-next-chains.patch applies after the fifteen
previous patches. Clean pinned sources plus all sixteen patches match the six
committed chain feature files byte for byte. The private package tree rebuilds
native output, inventory and layout, passes original ABI/context and native
source/resource/chain checks, then runs all normal SDK gates including heading
chains before archive creation and installation. Unchanged managed/graphics/
printing outputs remain its own source builds.

Validation in the CLANG64 package workspace:

```sh
makepkg --verifysource --force
source PKGBUILD                 # with srcdir set to this workspace's src/
_wpf native-win32 inventory check-paragraph-context-native check-paragraph-context check-lineservices-line-native layout check-sdk
makepkg --repackage --force --noconfirm
pacman -U --noconfirm mingw-w64-clang-x86_64-dotnet-windowsdesktop-bootstrap-8.0.0-16-any.pkg.tar.zst
/usr/bin/python3 check-staged.py /clang64/lib/dotnet
bash "$(_driver)/wpf-tests/check-keep-next-sdk.sh" /clang64/lib/dotnet ~/wpfboot/installed-keep-next-sdk-check
bash "$(_driver)/wpf-tests/check-keep-together-sdk.sh" /clang64/lib/dotnet ~/wpfboot/installed-keep-next-together-sdk-check
```

All commands pass. Staged/installed 42-managed/eight-native/SDK/legacy payload
checks pass. Installed original one/three-heading flat/nested WPF comparisons
move enabled chains with their body while disabled headings remain on the earlier
page. Source/page mappings, rendered page regeneration and GC pass in Ideal/
Display. Nonzero collapsed-margin rollback, unbroken multi-line headings,
oversized chain progress and heading/body glyphs together in the second column
pass through shared-framework and self-contained publishes with SDK roots/
toolchain PATH removed. Dispatcher cleanup and installed KeepTogether tests
also pass. Private normal WPF/tab/inline-object/justification/width/context/
finite/bottomless/pagination/columns/hard-break/keep/keep-next/WinForms regressions
pass with zero consumer warnings/errors. The existing source-bootstrap.json
srcdir provenance warning remains.

Artifact: mingw-w64-clang-x86_64-dotnet-windowsdesktop-bootstrap-8.0.0-16-any.pkg.tar.zst.
SHA-256: 29a949f49ca9157adb2c558a8b990728a8bc2a59fea116d8862449ddd4320a2c.
Recipe commit: msys2-ur 359d823; feature commit: 817cc43.
Artifact, recipe, metadata, patch and validation logs are archived under
 tmp/wpf8-support/validation/keep-next-package-20261013.
Imported native coverage remains 89/139 with 50 absent: KeepWithNext adds
behavior to existing exports. Optimal/forced-line policies, balancing/span areas,
tables/floaters/figures, advanced/incremental/interruptible algorithms, full
bidi/native language and broader runtime behavior remain incomplete. The full
WPF goal stays active.


## 132. Forced progress for undersized finite pages

The 2026-10-13 continuation accepts one actual original client line when a
fresh finite area cannot fit any line. Accepted line ascent/descent and ink
bounds remain real; paragraph used height is bounded by the available extent.
The forced-progress flag propagates through original container callbacks,
subtracks, columns and the page result. Deferred paragraphs in already occupied
areas retain the existing no-progress behavior and original resume position.
A forced nonterminal line retains its continuation even when bounded lookahead
formatted only that line; an accepted terminal line does not manufacture an
extra break record. Zero-height accepted clients remain owned by the track.

Validation on win11:

```sh
M612=/home/topkek/dotnetboot/m612 MSYSTEM=CLANG64 WPF_BUILD=/home/topkek/wpfboot/build bash ~/wpfboot/driver/bootstrap/m612/wpf.sh native-win32 check-paragraph-context-native
bash ~/wpfboot/driver/bootstrap/m612/wpf-tests/check-forced-progress-sdk.sh ~/wpfboot/build/sdk-check-root ~/wpfboot/forced-sdk-check
M612=/home/topkek/dotnetboot/m612 MSYSTEM=CLANG64 WPF_BUILD=/home/topkek/wpfboot/build bash ~/wpfboot/driver/bootstrap/m612/wpf.sh check-paragraph-context-native inventory layout check-sdk
```

Native checks verify four-page exact source consumption in flat/nested tracks
with four-unit and zero-height areas, forced-result propagation, terminal record
absence and exact accepted/speculative callback resource cleanup. Original WPF
checks use 400x10 pages with 32-unit paragraph line height. Flat/nested documents
with keep constraints false/true finish in three pages, retain the requested
page size, cover all 61 characters exactly once and preserve content/page mapping
in Ideal/Display. A taller export bitmap observes actual oversized line ink,
including its portion below the physical page boundary: 3161 pixels Ideal and
2988 Display across the three pages. This is retained-line evidence, not a claim
that all glyph ink fits inside a ten-unit physical page. Disposed early/late page
regeneration, compacting GC and dispatcher cleanup pass through normal shared
framework and self-contained publishes with SDK roots/toolchain PATH removed.
The source SDK gate now includes forced progress alongside the previous normal
WPF/tab/inline-object/justification/width/context/finite/bottomless/pagination/
columns/hard-break/keep/keep-next/WinForms gates. Final source regression results
and inventory are archived under tmp/wpf8-support/validation/forced-progress-20261013.
Package integration follows. Imported native coverage stays 89/139 with 50 absent.
Optimal line policies, balancing/span areas, tables/floaters/figures, advanced/
incremental/interruptible algorithms, full bidi/native language and broader
runtime behavior remain required for the full WPF goal.


## 133. Installed release 17: forced finite-page progress

The 2026-10-13 continuation packages section 132 as WindowsDesktop 8.0.0-17.
Declared/checksummed 0017-forced-page-progress.patch applies after the sixteen
previous patches. Clean pinned sources plus all seventeen patches match the
five committed feature files byte for byte. The private package tree rebuilds
native output, inventory and layout, passes original ABI/context and native
source/resource/forced checks, then runs all normal SDK gates before archive
creation and installation. Unchanged managed/graphics/printing outputs remain
its own source builds.

Validation in the CLANG64 package workspace:

```sh
makepkg --verifysource --force
source PKGBUILD                 # with srcdir set to this workspace's src/
_wpf native-win32 inventory check-paragraph-context-native check-paragraph-context check-lineservices-line-native layout check-sdk
makepkg --repackage --force --noconfirm
pacman -U --noconfirm mingw-w64-clang-x86_64-dotnet-windowsdesktop-bootstrap-8.0.0-17-any.pkg.tar.zst
/usr/bin/python3 check-staged.py /clang64/lib/dotnet
bash "$(_driver)/wpf-tests/check-forced-progress-sdk.sh" /clang64/lib/dotnet ~/wpfboot/installed-forced-sdk-check
bash "$(_driver)/wpf-tests/check-pagination-sdk.sh" /clang64/lib/dotnet ~/wpfboot/installed-forced-pagination-sdk-check
```

All final commands pass. The installation wrapper initially lacked CLANG64's
MINGW_PACKAGE_PREFIX environment and stopped before any package mutation;
running it through MSYSTEM=CLANG64 bash -lc supplies the intended environment.
Staged/installed 42-managed/eight-native/SDK/legacy payload checks pass. Installed
original flat/nested WPF checks with keep constraints false/true finish in three
400x10 pages at the requested size, with all 61 characters mapped exactly once
and original oversized glyph ink retained. Ideal/Display, page regeneration,
compacting GC, normal shared-framework and self-contained publishes with SDK
roots/toolchain PATH removed, and dispatcher cleanup pass. Installed ordinary
pagination still renders six pages with all 1278 characters covered exactly
once. Private normal WPF/tab/inline-object/justification/width/context/finite/
bottomless/pagination/columns/hard-break/keep/keep-next/forced/WinForms regressions
pass with zero consumer warnings/errors. The existing source-bootstrap.json
srcdir provenance warning remains.

Artifact: mingw-w64-clang-x86_64-dotnet-windowsdesktop-bootstrap-8.0.0-17-any.pkg.tar.zst.
SHA-256: d60b37c49133ad631df09382be9f419f52e8493a0e324232709e199794eb2b9d.
Recipe commit: msys2-ur 0b4c9e4; feature commit: c32fec2.
Artifact, recipe, metadata, patch and validation logs are archived under
 tmp/wpf8-support/validation/forced-progress-package-20261013.
Imported native coverage remains 89/139 with 50 absent: forced progress adds
behavior to existing exports. Optimal policies, balancing/span areas, tables/
floaters/figures, advanced/incremental/interruptible algorithms, full bidi/native
language and broader runtime behavior remain incomplete. The full WPF goal
stays active.


## 134. Original BlockUIContainer callbacks, pagination and child layout

The 2026-10-14 continuation adds nonfloating, full-column BlockUIContainer
formatting through original WPF floater callbacks. GetFloaterHandlerInfo copies
FSFLOATERINIT into this replacement's opaque handler record; the engine obtains
it through original GetObjectHandlerInfo. Its private handler producer and
consumer are both source-owned. Callback tables/properties/query records and
function signatures come from original Pts.cs, bringing ABI field validation
from 283 to 313 entries. No Microsoft native handler implementation is imported.

ConvertMcsclient computes collapsed top space; FormatFloaterContentFinite or
Bottomless performs original UIElementIsland measurement; GetMCSClientAfterFloater
returns the original bottom-margin state. The engine retains the original
content handle, page-coordinate block bounds, used height, forced-progress and
query/update state. An occupied area can defer a whole block without advancing
source or replacing incoming margins. A fresh undersized area accepts original
forced content with bounded used height and no phantom terminal continuation.
Scoped content/break ownership releases populated outputs after callback errors,
then destroys the original paragraph client. FsQueryFloaterDetails provides real
recorded placement. Floating obstacles still need their own text-flow algorithm
and are explicitly rejected rather than formatted as nonfloating blocks.

Validation on win11:

```sh
M612=/home/topkek/dotnetboot/m612 MSYSTEM=CLANG64 WPF_BUILD=/home/topkek/wpfboot/build bash ~/wpfboot/driver/bootstrap/m612/wpf.sh native-win32 check-paragraph-context-native
MSYSTEM=CLANG64 bash -lc 'bash ~/wpfboot/driver/bootstrap/m612/wpf-tests/check-block-ui-sdk.sh ~/wpfboot/build/sdk-check-root ~/wpfboot/block-ui-sdk-check'
M612=/home/topkek/dotnetboot/m612 MSYSTEM=CLANG64 WPF_BUILD=/home/topkek/wpfboot/build bash ~/wpfboot/driver/bootstrap/m612/wpf.sh check-paragraph-context inventory layout check-sdk
```

Native checks verify original handler table use, intact deferral/resume, actual
content/bounds, forced terminal progress, foreign/stale queries, clear-update
state, populated callback failure and exact content/line/break/client cleanup.
Original WPF flat/nested documents in Ideal/Display defer a real Button inside a
red Border to page 1 after preceding text. Child size is 360x60 at (18,10), with
16560 red pixels; text/block page mapping, visual point hit, routed click events,
disposed-page regeneration and GC pass. Oversized and empty blocks finish on a
single 400x10 page at the requested size. A two-column document moves its block
intact to the second column at (208,10), width 150, and finishes on one page.
Original FlowDocumentScrollViewer child resizing from 60 to 120 updates the
rendered red area from 22020 to 44040 pixels. Shared-framework and self-contained
SDK consumers with SDK roots/toolchain PATH removed pass dispatcher cleanup.
The source SDK gate now includes the block-control test alongside previous WPF/
text/paragraph/pagination/keep/forced/WinForms regressions. Evidence is archived
under tmp/wpf8-support/validation/block-ui-20261014; package integration follows.
Imported native coverage rises to 91/139 with 48 absent. Floating obstacles,
figures, tables, balancing/span areas, optimal/advanced/incremental/interruptible
algorithms, full bidi/native language and broader runtime behavior remain
required for the full WPF goal.


## 135. Installed release 18: original block UI controls

The 2026-10-14 continuation packages section 134 as WindowsDesktop 8.0.0-18.
Declared/checksummed 0018-block-ui-containers.patch applies after the seventeen
previous patches. Clean pinned sources plus all eighteen patches match the six
committed feature files byte for byte. The private package tree rebuilds native
output, inventory and layout, passes 313 original ABI field checks, original
context/line-service regressions and native source/resource/block checks, then
runs every normal SDK gate before archive creation and installation. Unchanged
managed/graphics/printing outputs remain its own source builds.

Validation in the CLANG64 package workspace:

```sh
makepkg --verifysource --force
source PKGBUILD                 # with srcdir set to this workspace's src/
_wpf native-win32 inventory check-paragraph-context-native check-paragraph-context check-lineservices-line-native layout check-sdk
makepkg --repackage --force --noconfirm
pacman -U --noconfirm mingw-w64-clang-x86_64-dotnet-windowsdesktop-bootstrap-8.0.0-18-any.pkg.tar.zst
/usr/bin/python3 check-staged.py /clang64/lib/dotnet
bash "$(_driver)/wpf-tests/check-block-ui-sdk.sh" /clang64/lib/dotnet ~/wpfboot/installed-block-ui-sdk-check
bash "$(_driver)/wpf-tests/check-forced-progress-sdk.sh" /clang64/lib/dotnet ~/wpfboot/installed-block-ui-forced-sdk-check
```

All final commands pass. Staged/installed 42-managed/eight-native/SDK/legacy
payload checks pass. Installed original flat/nested WPF documents in Ideal/
Display defer a real button block intact to the next page, with expected
360x60 child size, (18,10) placement, 16560 red pixels, surrounding text/block
page mappings, visual point hit, routed button event, regenerated pages and GC.
Oversized/empty blocks finish on one tiny page without phantom continuation;
a block can move intact to the second column of one page. Original scrolling
controls react to child height changes from 60 to 120 with doubled red raster.
Shared-framework and self-contained publishes run with SDK roots/toolchain PATH
removed and pass dispatcher cleanup. Installed forced-text pagination remains
three requested-size pages with all 61 characters mapped exactly once. Private
normal WPF/tab/inline-object/justification/width/context/finite/bottomless/
pagination/columns/hard-break/keep/keep-next/forced/block/WinForms regressions
pass with zero consumer warnings/errors. The existing source-bootstrap.json
srcdir provenance warning remains.

Artifact: mingw-w64-clang-x86_64-dotnet-windowsdesktop-bootstrap-8.0.0-18-any.pkg.tar.zst.
SHA-256: eaa8a3186d838ac98e4cd2e4922afabf11b1b936b6ed70d3b271077814aa6c63.
Recipe commit: msys2-ur 531b752; feature commit: 7570d75.
Artifact, recipe, metadata, patch and validation logs are archived under
 tmp/wpf8-support/validation/block-ui-package-20261014.
Imported native coverage is 91/139 with 48 absent. Floating obstacles/figures,
tables, balancing/span areas, optimal/advanced/incremental/interruptible
algorithms, full bidi/native language and broader runtime behavior remain
incomplete. The full WPF goal stays active.


## 136. Source subpage engine and original table-cell subpage clients

The 2026-10-14 continuation adds finite and bottomless subpage creation,
subpage destruction, independent typed break-record destruction/cloning, simple/
complex queries, basic-column enumeration and recursive update clearing. These
are eight original imported APIs, bringing coverage to 99/139 with 40 absent.
FSSUBPAGEDETAILS and its nested union arms come from original Pts.cs; all 326
managed/native ABI field sizes/offsets match.

Subpages retain their own root tracks/columns, real text clients, actual local
geometry and continuation checkpoints. Page/subpage/subtrack record kinds are
validated independently. Original installed object 1 uses its own callback table
and break destructor, while object 0 retains subtrack ownership. Scoped callback
transactions also retain newly exported subpages, releasing whole child graphs
if the managed callback loses its output handle and returns an error. Arranged
original subpage clients own their native handle; disposal gives them ownership
first, then releases any subpage left by an unarranged client. Root callbacks
preserve actual forced ink bounds while accounting for available used height.

Structural marker lines may consume source with zero visible height. Retained
text/child records now preserve this source progress instead of losing it when
used height is zero. At a fresh undersized area, a structural-only prefix does
not exhaust the opportunity to accept an actual oversized visible line. Native
checks cover every source unit once across those page boundaries.

Validation on win11:

```sh
M612=/home/topkek/dotnetboot/m612 MSYSTEM=CLANG64 WPF_BUILD=/home/topkek/wpfboot/build bash ~/wpfboot/driver/bootstrap/m612/wpf.sh native-win32 check-paragraph-context-native
M612=/home/topkek/dotnetboot/m612 MSYSTEM=CLANG64 WPF_BUILD=/home/topkek/wpfboot/build bash ~/wpfboot/driver/bootstrap/m612/wpf.sh check-paragraph-context inventory layout check-sdk
```

Final native gates pass. They verify four finite source ranges, independent
clones after page disposal, record-kind/foreign/stale rejection, two-column
geometry/capacity, recursive update state, allocation-free invalid-column
rejection, partial line failure cleanup, bottomless actual extent, installed
object-1 nested continuations and lost-output rollback. Forced nested ink retains
its real ten-unit box while parent used height remains four units. Source SDK
regressions pass; the final native-only overhang adjustment is rebuilt and
validated again before package integration.

A normal offline SDK consumer drives original SubpageParagraph and
SubpageParaClient methods through an attached table cell, original scoped
format/arrange contexts and the original viewport/visual pipeline. Four rendered
chunks in each formatting mode cover all 699 source characters exactly once,
with 33036 Ideal/26220 Display glyph pixels. The first output continuation is
cloned and its original destroyed before resuming. Original bottomless clients
retain all 699 characters with full extents 532 Ideal/560 Display and real
viewport glyphs. Client/record disposal runs inside the original context; GC,
callback-exception checks and dispatcher cleanup pass through shared-framework
and self-contained publishes with SDK roots/toolchain PATH removed. The fixture
uses WPF's 300 native units per DIP and invokes viewport updates for original
deferred line visuals. Original cell subpages use one column; native multi-column
subpage behavior has its own callback/query gate. This exercises the original
cell subpage path directly; the table object handler and public table layout
remain required. It does not establish public floater/figure text-flow support.

Evidence is archived under tmp/wpf8-support/validation/subpages-20261014.
Finite balancing/span areas, footnotes and bottomless multi-column subpages are
still rejected explicitly; subpage updates/comparison/display transfer remain
unfinished. Floating obstacles/figures, tables, optimal/advanced/incremental/
interruptible algorithms, full bidi/native language and broader runtime behavior
remain required for the full WPF goal. Package integration follows.


## 137. Installed release 19: finite and bottomless subpages

The 2026-10-14 continuation packages section 136 as WindowsDesktop 8.0.0-19.
Declared/checksummed 0019-subpage-layout.patch applies after the eighteen previous
patches. Clean pinned sources plus all nineteen patches match the six committed
feature files byte for byte. The private package tree rebuilds native output,
inventory and layout, passes 326 original ABI field checks, context/line-service
regressions, native subpage/ownership/source-progress gates and every normal SDK
gate before archive creation and installation. Unchanged managed/graphics/
printing outputs remain its own source builds.

Validation in the CLANG64 package workspace:

```sh
makepkg --verifysource --force
source PKGBUILD                 # with srcdir set to this workspace's src/
_wpf native-win32 inventory check-paragraph-context-native check-paragraph-context check-lineservices-line-native layout check-sdk
makepkg --repackage --force --noconfirm
pacman -U --noconfirm mingw-w64-clang-x86_64-dotnet-windowsdesktop-bootstrap-8.0.0-19-any.pkg.tar.zst
/usr/bin/python3 check-staged.py /clang64/lib/dotnet
bash "$(_driver)/wpf-tests/check-subpages-sdk.sh" /clang64/lib/dotnet ~/wpfboot/installed-subpages-sdk-check
bash "$(_driver)/wpf-tests/check-block-ui-sdk.sh" /clang64/lib/dotnet ~/wpfboot/installed-subpages-block-ui-sdk-check
```

All final commands pass. Staged/installed 42-managed/eight-native/SDK/legacy
payload checks pass. Installed original table-cell subpage clients render four
finite chunks in Ideal/Display, cover all 699 source characters exactly once,
clone and destroy the first output continuation before resuming, retain 33036/
26220 glyph pixels and dispose original clients across GC. Bottomless original
clients retain all source characters, real viewport glyphs and full 532/560-unit
extents. Scoped original callbacks report no cleanup exception, and dispatcher
shutdown passes. These checks use the original cell subpage path directly;
public table/floater/figure layout still requires its object/text-flow handlers.
Normal shared-framework and self-contained publishes run with SDK roots/toolchain
PATH removed. Installed BlockUIContainer deferral, margin/child bounds, point
hit/events, tiny/empty blocks, second-column placement and child-resize regressions
pass. Private normal WPF/tab/inline-object/justification/width/context/finite/
bottomless/pagination/columns/hard-break/keep/keep-next/forced/block/subpage/
WinForms regressions pass with zero consumer warnings/errors. The existing
source-bootstrap.json srcdir provenance warning remains.

Artifact: mingw-w64-clang-x86_64-dotnet-windowsdesktop-bootstrap-8.0.0-19-any.pkg.tar.zst.
SHA-256: 910324b2e2a8a62f645c9f89eec1f7bf5590ca1cf36a244265a52d568f5792e2.
Recipe commit: msys2-ur caf9986; feature commit: b0334f5.
Artifact, recipe, metadata, patch and validation logs are archived under
 tmp/wpf8-support/validation/subpages-package-20261014.
Imported native coverage is 99/139 with 40 absent. Subpage updates/comparison/
display transfer, balancing/span areas/footnotes, floating obstacles/figures,
tables, optimal/advanced/incremental/interruptible algorithms, full bidi/native
language and broader runtime behavior remain incomplete. The full WPF goal
stays active.


## 138. Stable-identity bottomless subpage updates

The 2026-10-14 continuation implements FsUpdateBottomlessSubpage for supported
single-column LTR subpages. The formatter builds a replacement tree through
original callbacks, allocates its registry entry before changing existing state,
then swaps the root/layout data while retaining the existing subpage identity.
Superseded original line/client resources release outside the registry lock.
Failed formatting or invalid geometry leaves the previous native tree queryable;
metric/margin/interruption outputs remain reset. Foreign/stale handles, finite
subpages and a different source segment are rejected. Formatting completes fully
and reports uninterruptible completion; this does not add an interruptible or
partial-paragraph incremental algorithm.

Validation on win11:

```sh
M612=/home/topkek/dotnetboot/m612 MSYSTEM=CLANG64 WPF_BUILD=/home/topkek/wpfboot/build bash ~/wpfboot/driver/bootstrap/m612/wpf.sh native-win32 check-paragraph-context-native
MSYSTEM=CLANG64 bash -lc 'bash ~/wpfboot/driver/bootstrap/m612/wpf-tests/check-subpages-sdk.sh ~/wpfboot/build/sdk-check-root ~/wpfboot/subpages-update-sdk-check'
M612=/home/topkek/dotnetboot/m612 MSYSTEM=CLANG64 WPF_BUILD=/home/topkek/wpfboot/build bash ~/wpfboot/driver/bootstrap/m612/wpf.sh check-paragraph-context-native inventory layout check-sdk
```

Native gates verify stable identity, changed source/extent/position, invalidated
old root queries, exact superseded-resource release, partial populated callback
failure, clean failure outputs, preserved prior geometry, allocation-free invalid
geometry rejection, foreign/wrong-segment/stale rejection and exact final cleanup.
Original SubpageParagraph.UpdateBottomlessPara updates the same original
SubpageParaClient after appending 640 text characters and narrowing its width
from 400 to 300. Full height grows from 532 to 1197 in Ideal and from 560 to 1260
in Display. All 1339 source characters remain in the original content range,
the original handle stays unchanged and viewport glyph raster changes. Original
arrange/viewport/visual creation, client disposal, callback-exception checks and
compacting GC pass. Shared-framework and self-contained SDK consumers run with
SDK roots/toolchain PATH removed and pass dispatcher cleanup. Existing finite
subpage coverage remains four rendered chunks covering all 699 characters once.
This is the original cell subpage path; public table layout still requires its
table object handler.

The existing source SDK gate includes these update checks in check-subpages-sdk.sh.
Final source regressions and inventory are archived under
 tmp/wpf8-support/validation/subpage-update-20261014. Coverage rises to 100/139
with 39 absent; original ABI field checks remain 326. Subpage comparison/display
transfer, subtrack updates, balancing/span areas/footnotes, floating obstacles/
figures, tables, optimal/advanced/incremental/interruptible algorithms, full
bidi/native language and broader runtime behavior remain required for the full
WPF goal. Package integration follows.
