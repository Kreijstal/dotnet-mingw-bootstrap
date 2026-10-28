# Serviced .NET8 runtime source build

This workflow advances the runtime-servicing requirement for the PowerShell graph.
The bootstrap bundle8.0.131 originally executed8.0.0 runtime sources. Its8.0.31
reference/pack directory aliases do not supply later servicing fixes. A separate
source8.0.31-2 runtime is now installed, but a subsequent forwarding regression
found bad compile-only stub keys and an omitted zero-reference-version policy.
Candidate3 repairs those helpers with the unchanged original Arcade postprocessor;
fresh complete package/install verification and broader compatibility remain open.

The isolated8.0.31 build now contains all168 assemblies in the original framework
manifest and six native framework DLLs. All183 reference/stub outputs and167
library compiler inventories pass the source audit. The standalone runtime check
passes actual version/source identity, every framework load, exceptions/filters,
GC/tasks, dynamic code, VB, Unicode regex/globalization/JSON/XML, managed/native
compression, RSA/AES-GCM, owned TLS/HTTP and file watching. Original PowerShell
CIM cmdlets and the source module host also pass local CIM/WMI, Add-Type,
Authenticode, ACL reads and serialization. Both the new and installed older muxer
can host the versioned runtime in isolated layouts. These are execution results;
package build/install and broader compatibility validation remain open.

A fresh source NuGet consumer exposed a further metadata defect: its older
private JSON implementation won over the new framework because the bootstrap's
simple8.0.31.0 file version sorted below8.0.1024.46610. The exact original
`CalculateAssemblyAndFileVersions` task from the runtime's declared Arcade commit
444c2ef3c414558e2e409ee346f1faebb19687eb now calculates8.0.3126.42000.
The reproducible build number20260820.0 uses the pinned runtime archive's UTC
commit date; it does not impersonate an official Microsoft build number. Product
and assembly identities retain their original values. The exact same restored
consumer now chooses the serviced JSON bytes. The first candidate package build
was stopped for this confirmed defect; the corrected candidate requires a fresh
clean build and all checks. Full earlier execution passes do not prove binding
correctness for consumers carrying private framework libraries.

Runtime8.0.31 source is pinned to commit
`1219a42122cf5190c7f512850557e38c422430dc`, archive SHA256
`b32a56a29cc0248bed219e1c7ef026bf8f006d997f5d6346382e63a22ecd9070`.
The bootstrap helper sources come from commit
`3cbf21078968e2290a54ab093392b4dfeb1c6f1f`, archive SHA256
`75bea2026b90c7f4d5f88279c4ff25ca07b4a6eb0e435df90a41c6f485f773c6`.
The trusted installed source-built SDK and source NuGet feed compile build tools.
No Microsoft runtime/SDK download or vendor implementation is used.

All17 original native-port patches are retained and replayed with zero fuzz. The
Mono CMake patch is rebased to preserve a newer upstream `/DEBUGTYPE` setting within
its MSVC branch. It is retained for the shared source tree; this workflow builds
CoreCLR. Existing MinGW port exclusions, including out-of-process debugging/DAC,
remain explicit limitations. CoreCLR/native host/JIT/resource DLLs build with the
CLANG64 toolchain and UASM. The original EHa/SEH shim test includes200,000 round trips.

The complete original interop/source-generation, LibraryImport, COM-interface,
CoreLib-event and Regex generator code is rebuilt, including code fixers, against
the source Roslyn4.9.2 libraries. Wrapper projects retain original project settings,
source selections and resource strings. They target the.NET8 build-tool host.
The Microsoft.CodeAnalysis meta-package maps to its actual source CSharp/Common
closure. Original explicit analyzers remain; compiler/reference/analyzer inventories
are captured. All original production C# inputs remain unchanged.

CoreLib builds from1,803 selected original sources with matching generated events,
source-generated interop and resource metadata. Its runtime-root descriptor comes
from the original8.0.31 task/header files. That single task is compiled into a private
build-only assembly, not packaged as an incomplete ILLink.Tasks replacement. The
original FCall-RVA metadata postprocessor is rebuilt from pinned helper source.
Build-helper overlays route paths to those tools and preserve the exact changes.

Reference/framework helpers select the8.0.31 source tree and CoreCLR project flavor.
Every framework compile failure is fatal. The installed source VB compiler builds
the original VB core library. Compile-only reference stubs used by the original
bootstrap are never runtime implementation payloads. Two helper defects are fixed:
the dependency plan retains source/shim kinds after processing linker metadata,
and shim reference projects include their checked-in type-forward sources.
Broader API/resource/native-closure audits, packaging and installed PowerShell/SDK
validation remain required before servicing can be marked complete. All broader
PowerShell dependency/native/SDK requirements remain active.

Run in MSYS2 CLANG64 with new output paths:

```bash
/usr/bin/python3 prepare.py PINNED_RUNTIME_ARCHIVE PINNED_BOOTSTRAP_ARCHIVE NEW_ROOT
/usr/bin/python3 prepare-version.py NEW_ROOT
/usr/bin/python3 prepare-file-version.py NEW_ROOT
bash build-native.sh NEW_ROOT 2
/usr/bin/python3 prepare-managed.py NEW_ROOT
bash build-generators.sh NEW_ROOT
/usr/bin/python3 prepare-corelib.py NEW_ROOT
bash build-corelib.sh NEW_ROOT
/usr/bin/python3 prepare-framework.py NEW_ROOT
bash build-framework.sh NEW_ROOT refs
bash build-framework.sh NEW_ROOT libraries
bash build-compression.sh NEW_ROOT 2
/usr/bin/python3 audit-build.py NEW_ROOT
/usr/bin/python3 audit-framework.py NEW_ROOT
/usr/bin/python3 stage-runtime.py NEW_ROOT NEW_RUNTIME_LAYOUT
bash check-runtime.sh NEW_ROOT NEW_RUNTIME_LAYOUT NEW_CHECK_OUTPUT
bash check-powershell.sh NEW_RUNTIME_LAYOUT
bash check-json-binding.sh NEW_RUNTIME_LAYOUT NEW_JSON_CONSUMER
```

`build-all.sh` runs the build/audit/layout stages from the two pinned archives.
The final two checks remain explicit. The native compression helper invokes only
its preparation and native-library steps, without compiling the Mono runtime.
The TLS fixture uses an owned PFX import because Windows Schannel rejects the
original ephemeral private key; that failure was independently reproduced on the
installed8.0.0 runtime. It installs no certificate into a trust store.

Build logs and inventories stay under the new root. The workflow owns only that
root and does not change policy, trust stores, registry, services or persistent PATH.
`audit-build.py` independently replays all17 patches, checks unchanged source hashes,
actual generator compiler/reference inputs,1,803 original CoreLib inputs/generated
metadata, and native/CoreLib output hashes. Passing it proves those stages; it does
not imply full runtime completion.
`audit-framework.py` also checks the complete upstream manifest, original compiler
input hashes, copied shim identities and independently reconstructed helper edits.
