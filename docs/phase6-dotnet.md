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
