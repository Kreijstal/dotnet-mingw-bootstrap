# CoreCLR 10 source port

This is a private bootstrap port of runtime v10.0.5, commit
`081d220c0a773ffb7c6bea6b48727833576a65ef`. It follows the separately
packaged native host component in `../runtime10-bootstrap`.

The 64-file patch applies without fuzz and builds CoreCLR, RyuJIT and the
two native resource DLLs with CLANG64/UASM. It adapts compiler/header/ABI
differences, new TLS symbol names and CET instruction encodings. It keeps
the upstream vxsort implementation and the secure system-directory delay
load hook. Explicit UASM include dependencies avoid stale assembly objects.
No original production source files are removed.

The original cDAC generator builds from eleven runtime C# files and 74
System.CommandLine C# files. The latter match the exact git blobs in the
runtime's pinned VMR dependency. The private generator targets net8.0 so
our existing source toolchain can run it before SDK10 exists. The generated
descriptor replaces the initial native bootstrap stub.
The source mappings are in the
[pinned VMR manifest](https://raw.githubusercontent.com/dotnet/dotnet/12f41dd0da1a94621e039e27aef264179c1104b5/src/source-manifest.json).

The existing source Roslyn5 compiler was bootstrapped on Mono. Its desktop
command-line client reparses Windows' OS command line and mistakes the
dotnet host arguments for compiler input when hosted on CoreCLR. The private
`CompilerHost.cs` adapter calls its original public `Program.Run` and
original analyzer loader with managed arguments. Compiler assembly bytes
remain unchanged. This host compiles and executes a C#14 field-backed
property probe, then compiles the unchanged original generator sources.
This is bootstrap tooling, not a finished SDK10 compiler distribution.
The upstream
[BuildClient implementation](https://raw.githubusercontent.com/dotnet/dotnet/12f41dd0da1a94621e039e27aef264179c1104b5/src/roslyn/src/Compilers/Shared/BuildClient.cs)
shows the desktop argument parsing and local compiler/analyzer-loader path.

Start with a fresh private directory on the Windows builder, containing
`runtime-v10.0.5.tar.gz`, the pinned command-line-api source archive, and
copies of this workflow and the native-host workflow. Run:

```sh
/usr/bin/python3 HOST_WORKFLOW/prepare-host.py runtime-v10.0.5.tar.gz .
/usr/bin/python3 HOST_WORKFLOW/prepare-version.py .
/usr/bin/python3 HOST_WORKFLOW/generate-host-resources.py .
/usr/bin/python3 WORKFLOW/prepare-port.py .
/usr/bin/python3 WORKFLOW/prepare-cdac.py . COMMAND_LINE_ARCHIVE
bash WORKFLOW/build-cdac.sh .
bash WORKFLOW/build-coreclr.sh . 2
bash WORKFLOW/check-eha.sh . NEW_EHA_OUTPUT
/usr/bin/python3 WORKFLOW/audit-native.py .
bash WORKFLOW/build-native-probe.sh . NEW_PROBE_OUTPUT
/clang64/bin/python.exe "$(cygpath -am WORKFLOW/check-native.py)" \
  "$(cygpath -am .)" "$(cygpath -am NEW_NATIVE_OUTPUT)" \
  "$(cygpath -am NEW_PROBE_OUTPUT/native-jit-probe.exe)"
```

Hashes and exact executed commands/results are recorded in
`port-source-inputs.json` and `native-results-20261027.{json,txt}`. The
native check verifies four source-derived PE versions, Unicode-path loading,
original JIT startup/host callbacks and the JIT/EE identifier. The loaded
CoreCLR cDAC JSON must equal the original tool-generated C literal byte for
byte, with populated contracts, types, globals and pointer data. The EHa
check inspects all eleven printed fixture results as well as its exit code,
including 200,000 stress round trips.

The native checkpoint above did not execute managed code. Matching CoreLib10
now passes four private managed probes; see
`../runtime10-corelib/managed-results-20261027.txt`. The full managed/native
framework, reference assemblies, SDK10,
WindowsDesktop and PowerShell7.6 integration remain open. The existing
GNU/Windows EHa shim has known limitations for foreign unwinds and hardware
faults outside call sites. DAC/out-of-process debugging and actual ETW
provider registration remain open; the original EventPipe code is retained
but has not yet been executed on this runtime. NativeAOT, ReadyToRun and
the broader runtime test matrix are also incomplete. No CoreCLR10 runtime
package is installed by this workflow.

The original ILLink library pipeline now also passes against this native CLR10:
[`../runtime10-illink/README.md`](../runtime10-illink/README.md). Its linked
CoreLib retains all checked public surface entries and resources and passes the
four managed probes without the earlier fcall-RVA helper.
