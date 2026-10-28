#!/bin/bash
# The whole phase 6 chain as built on win11 (2026-10-08), in order.
# Each step only uses the outputs of earlier steps and the Mono 6.12 seed.
# usage: chain.sh [first-step-number]      (run from anywhere on the build host)
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
. "$here/../env-m612.sh"
R() { cygpath -m "$M612/roslyn-$1/csc.exe"; }
start=${1:-1}; stop=${2:-50}
step() { local n=$1; shift; [ "$n" -ge "$start" ] && [ "$n" -le "$stop" ] || return 0; log "chain step $n: $*"; "$@"; }

# 1-4: Roslyn from the seed csc 3.9 (C# 9) up to 4.8 (C# 12), seed package assemblies
step 1 bash "$here/roslyn.sh" 4.0.1 "$SEED_CSC"
step 2 bash "$here/roslyn.sh" 4.4.0 "$(R 4.0.1)"
step 3 bash "$here/roslyn.sh" 4.6.0 "$(R 4.4.0)"
step 4 bash "$here/roslyn.sh" 4.8.0 "$(R 4.6.0)"
# 5: netfx "package" assemblies from source (SCI/SRM 8, System.Memory, ...)
step 5 bash "$here/netfx-pkgs.sh" "$(R 4.8.0)" "$M612/pkgs"
export PKGDIR=$M612/pkgs
# 6-11: Roslyn on the from-source packages up to 5.0 (C# 14)
step 6 bash "$here/roslyn.sh" 4.10.0 "$(R 4.8.0)"
step 7 bash "$here/roslyn.sh" 4.11.0 "$(R 4.10.0)"
step 8 bash "$here/roslyn.sh" 4.12.0 "$(R 4.11.0)"
step 9 bash "$here/roslyn.sh" 4.13.0 "$(R 4.12.0)"
step 10 env ROSLYN_TAG=NET-SDK-10.0.100-preview.4.25258.110 bash "$here/roslyn.sh" 5.0.0-p4 "$(R 4.13.0)"
step 11 env ROSLYN_TAG=Visual-Studio-2026-Version-18.0 bash "$here/roslyn.sh" 5.0.0 "$(R 5.0.0-p4)"
# 12: the rest of the package assemblies MSBuild needs (built with C# 13)
step 12 bash "$here/netfx-pkgs.sh" "$(R 4.12.0)" "$M612/pkgs" Microsoft.Bcl.AsyncInterfaces System.Text.Encodings.Web \
  System.Text.Json System.Threading.Tasks.Dataflow System.Reflection.MetadataLoadContext System.Resources.Extensions Microsoft.IO.Redist
# 13: MSBuild 17.8.49 (net472) + Roslyn 4.12 Csc task; tests an SDK-less csproj
step 13 bash "$here/msbuild.sh" 17.8.49 "$(R 4.12.0)" 4.12.0
# 14-16: package deps for NuGet and the SDK tasks (built with C# 13)
step 14 bash "$here/netfx-pkgs.sh" "$(R 4.12.0)" "$M612/pkgs" Newtonsoft.Json Microsoft.Extensions.Primitives \
  Microsoft.Extensions.FileProviders.Abstractions Microsoft.Extensions.FileSystemGlobbing Microsoft.Extensions.DependencyModel \
  Microsoft.NET.HostModel Microsoft.Deployment.DotNet.Releases
# 15: NuGet.Client 6.8.2.3 (net472) into the MSBuild tools dir; tests PackageReference restore from a local feed
step 15 bash "$here/nuget.sh" 6.8.2.3 "$(R 4.12.0)" 17.8.49
# 16: dotnet/sdk 8.0.131 Microsoft.NET.Build.Tasks (net472) + Sdks layout; tests an SDK-style net472 project
step 16 bash "$here/sdk.sh" 8.0.131 "$(R 4.12.0)" 17.8.49
# 17: Microsoft.NETCore.App.Ref 8.0 (163 reference assemblies/facades + 20 stubs) from runtime sources,
#     registered in the SDK; tests an SDK-style net8.0 library build
step 17 bash "$here/refpack.sh" "$(R 4.12.0)"
# 18: the native host (dotnet.exe, hostfxr, hostpolicy, apphost, nethost, comhost) with CLANG64 clang/lld,
#     installed into the SDK (muxer layout + Microsoft.NETCore.App.Host.win-x64 pack); tests --info, a
#     net8.0 UseAppHost=true console build, and hostfxr -> hostpolicy against a fake framework
step 18 env MSYSTEM=CLANG64 bash "$here/corehost.sh"
# 19: the Mono flavor of the runtime (coreclr.dll = Mono VM, mono-sgen.exe) and System.IO.Compression.Native.dll
#     with CLANG64 clang/lld; tests the compression DLL and dotnet.exe -> hostfxr -> hostpolicy -> coreclr.dll,
#     which initialises and stops at the missing System.Private.CoreLib (S9)
step 19 env MSYSTEM=CLANG64 bash "$here/mono-runtime.sh"
# 20: the VB compiler (Microsoft.CodeAnalysis.VisualBasic + vbc.exe) from the Roslyn 4.8.0 tree; the seed vbc 3.9
#     compiles the VB parts once (like the seed csc did for Roslyn 4.0.1); needed for Microsoft.VisualBasic.Core
step 20 bash "$here/vbc.sh" 4.8.0
# 21: System.Private.CoreLib (Mono flavor, win-x64) from the patched runtime tree, with Roslyn 4.8 (the compiler
#     runtime 8.0.0 builds with) and the from-source EventSourceGenerator/LibraryImportGenerator
step 21 bash "$here/corelib.sh" "$(R 4.8.0)"
# 22: the 167 other Microsoft.NETCore.App implementation assemblies (net8.0-windows/net8.0, partial facades with
#     generated type forwards, shims; Microsoft.VisualBasic.Core with the from-source vbc)
step 22 bash "$here/libraries.sh" "$(R 4.8.0)"
# 23: shared/Microsoft.NETCore.App/8.0.0 (+ deps.json) in the SDK root; tests a net8.0 hello world and a smoke
#     test (file IO, LINQ, Regex, JSON, XML, Process, sockets, HttpClient, Reflection.Emit, compression, crypto, VB)
step 23 env TESTS="hello smoke" bash "$here/framework.sh"

# ---- S11: the SDK's own tools as net8.0 assemblies, on the source-built runtime (docs §6.11) ----
# 24: out-of-band runtime libraries the tools need (ConfigurationManager, MetadataLoadContext, Pkcs/Xml,
#     CodeDom, Resources.Extensions, Microsoft.Extensions.*), net8.0, against the reference pack
OOB_LIBS="System.Configuration.ConfigurationManager System.Diagnostics.EventLog System.Security.Cryptography.ProtectedData
  System.Reflection.MetadataLoadContext System.Resources.Extensions System.CodeDom System.Security.Cryptography.Pkcs
  System.Security.Cryptography.Xml System.ServiceProcess.ServiceController Microsoft.Extensions.Primitives
  Microsoft.Extensions.FileProviders.Abstractions Microsoft.Extensions.FileSystemGlobbing Microsoft.Extensions.DependencyModel
  Microsoft.Extensions.Logging Microsoft.Extensions.Logging.Abstractions Microsoft.Extensions.Logging.Console Microsoft.Extensions.Options
  Microsoft.Extensions.DependencyInjection Microsoft.Extensions.DependencyInjection.Abstractions Microsoft.Extensions.Configuration
  Microsoft.Extensions.Configuration.Abstractions Microsoft.Extensions.Configuration.Binder Microsoft.Extensions.Options.ConfigurationExtensions
  Microsoft.Extensions.Logging.Configuration"
step 24 env OOB="$OOB_LIBS" bash "$here/libraries.sh" "$(R 4.8.0)" plan gens build report
# 25: the generators the reference pack ships that S9 did not need (System.Text.Json, JSImport), then the pack
#     again with analyzers/dotnet/cs; the runtime pack for self-contained publish
step 25 bash "$here/netfx-pkgs.sh" "$(R 4.8.0)" "$M612/pkgs" System.Text.Json.SourceGeneration Microsoft.Interop.JavaScript.JSImportGenerator
step 26 bash "$here/refpack.sh" "$(R 4.8.0)" pack
# 27-28: Roslyn 4.8 (csc.dll, vbc.dll, VBCSCompiler.dll) and MSBuild 17.8 (MSBuild.dll) for net8.0
step 27 bash "$here/roslyn-net8.sh" 4.8.0
step 28 bash "$here/msbuild-net8.sh" 17.8.49
step 29 bash "$here/roslyn-net8.sh" 4.8.0 tasks
# 30-32: Newtonsoft.Json, HostModel, Releases, System.CommandLine, ApplicationInsights; NuGet 6.8; TemplateEngine
step 30 bash "$here/clideps-net8.sh"
step 31 bash "$here/nuget-net8.sh" 6.8.2.3
step 32 bash "$here/clideps-net8.sh" templating
# 33-35: SourceLink, the SDK (resolvers, CLI with dotnet.dll, Microsoft.NET.Build.Tasks), ILLink
step 33 bash "$here/sourcelink-net8.sh"
step 34 bash "$here/sdk-net8.sh"
step 35 bash "$here/illink-net8.sh"
# 36-37: dotnet-sdk-8.0.131-net8 (upstream layout) and the end-to-end proof (no Mono 6.12 involved)
step 36 bash "$here/sdk-layout-net8.sh"
step 37 bash "$here/sdk-net8-test.sh"

# ---- CoreCLR (docs §6.12): the runtime the upstream SDK ships, with CLANG64 instead of MSVC ----
# 38: coreclr.dll, clrjit.dll, mscorrc.dll from the same tree (patches 0008-0016: GNU-driver CMake, inc/mingw,
#     ETW header without mc.exe, /EHa for libc++abi, clang SEH workaround, UASM for the MASM helpers);
#     needs mingw-w64-clang-x86_64-uasm
step 38 env MSYSTEM=CLANG64 bash "$here/coreclr.sh"
# 39: System.Private.CoreLib of the CoreCLR flavor, compiled by the net8.0 csc.dll of step 36 (Roslyn 4.8 with
#     System.Reflection.Metadata 8: the seed's SRM 5 misaligns CreateSpan data, which CoreCLR rejects)
step 39 bash "$here/corelib-coreclr.sh" "$(cygpath -m "$M612/dotnet-sdk-8.0.131-net8/sdk/8.0.131/Roslyn/bincore/csc.dll")"
# 40: m612/dotnet-sdk-8.0.131-net8-coreclr: the net8.0 SDK with the CoreCLR shared framework; hello + smoke
step 40 env TESTS="hello smoke ehstress" bash "$here/framework-coreclr.sh"
# 41: the end-to-end SDK proof of step 37 on CoreCLR
step 41 env ROOT="$M612/dotnet-sdk-8.0.131-net8-coreclr" TESTDIR="$M612/test/sdk-net8-coreclr" bash "$here/sdk-net8-test.sh"
# 42: crossgen2 (ReadyToRun compiler) from source: clrjit_win_x64_x64 + jitinterface_x64 (CLANG64, step 38's tree)
#     and the managed ILCompiler.* + crossgen2.dll; R2R-compiles a hello app and runs it on step 40's root
step 42 bash "$here/crossgen2.sh"
# 43: m612/dotnet-sdk-8.0.131-net8-coreclr-r2r: a copy of step 40's root with the shared framework and the
#     SDK's managed tools R2R-compiled (one image per assembly, as upstream ships them)
step 43 bash "$here/crossgen2.sh" r2rroot r2rfx r2rsdk
# 44: the end-to-end SDK proof on the R2R root
step 44 env ROOT="$M612/dotnet-sdk-8.0.131-net8-coreclr-r2r" TESTDIR="$M612/test/sdk-net8-coreclr-r2r" bash "$here/sdk-net8-test.sh"

# ---- S12: the SDK rebuilds itself from the upstream project files (docs §6.13) ----
# 45: stage 2, built by the stage-1 CoreCLR/R2R SDK of step 43: source-build-reference-packages (ilasm), the
#     fixed targeting pack, the seed feed, then Arcade, xliff-tasks, Roslyn, MSBuild, Newtonsoft.Json, xdt,
#     NuGet.Client, command-line-api, templating, deployment-tools, sourcelink and dotnet/sdk with
#     `dotnet build` of their own project files, and m612/dotnet-sdk-8.0.131-stage2 laid out from their outputs
step 45 env STAGE=2 bash "$here/s12-stage.sh" "${DOTNET_S12_START:-1}"
# 46: the end-to-end SDK proof on the stage-2 root
step 46 env ROOT="$M612/dotnet-sdk-8.0.131-stage2" TESTDIR="$M612/test/sdk-stage2" bash "$here/sdk-net8-test.sh" \
  info new pkgref addpackage multi roslyncore generators selfcontained trimmed
# 47-49: stage 3 = the same with the stage-2 SDK, stage 4 with the stage-3 SDK; the determinism proof
#        compares stage 2 with 3 (they differ only by the compiler's own version string in the PDBs) and
#        stage 3 with 4 (bit-identical)
step 47 env STAGE=3 bash "$here/s12-stage.sh"
step 48 env STAGE=4 bash "$here/s12-stage.sh"
step 49 env V1=2 V2=3 bash "$here/s12-verify.sh"
step 50 env V1=3 V2=4 bash "$here/s12-verify.sh"
