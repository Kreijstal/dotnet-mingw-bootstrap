# Original ILLink 10 source bootstrap

The original runtime v10.0.5 ILLink now builds and performs the original
CoreLib library rewrite. Its output executes on the source-built native CLR10
without the earlier fcall-RVA helper. This is a private bootstrap; the full
runtime/reference packs, SDK10, WindowsDesktop10 and PowerShell7.6 remain open.

Seven production projects build from unchanged C# inputs:

| Project | Original C# inputs |
| --- | ---: |
| Mono.Cecil | 137 |
| Mono.Cecil.Rocks | 12 |
| Mono.Cecil.Pdb | 36 |
| Mono.Cecil.Mdb | 9 |
| System.Collections.Immutable | 130 |
| System.Reflection.Metadata | 229 |
| ILLink | 219 |

The runtime pins Microsoft.DotNet.Cecil `0.11.5-alpha.26116.108` through
VMR `12f41dd0da1a94621e039e27aef264179c1104b5`. Its Cecil source revision is
`27abc57253a0d3010418e877fdfb35f9fa6806b9`; all 197 selected production,
project and key inputs match the exact VMR Git blobs. The source archive SHA256
is `3d82d4708f93dabd7442bcdb6f2667489fdd3ee2162d821b96d8aad62c33d019`.
Preparation removes 134 archived binary inputs. All four Cecil projects retain
their original signing identity and source files.

The tool runs privately on source-built .NET8 with the source Roslyn5 compiler
adapter from `../runtime10-coreclr`. Both supporting BCL projects explicitly
support net8 upstream; their complete evaluated net8 source sets and original
resources are retained. Generated resource accessors and private build wrappers
are bootstrap inputs. This does not establish the net10 framework/package graph.

SDK8 emits empty version metadata for project-reference runtime assets. A host
trace demonstrated that framework8 then replaced the private Metadata10 asset.
`NormalizeBootstrapDeps.cs` fills dependency-file assembly and file versions
from the actual built PE files. It changes no DLL bytes or runtime policy.
The build records the original dependency file and all 13 metadata entries.
Compiler references also exclude duplicate SDK8 Immutable/Metadata framework
assets where the private source assemblies are used. The fresh tool build has
479 warnings and zero errors, including original-source diagnostics and SDK8
reference-resolution warnings; they remain visible in the recorded log.

Start with the prepared native/CoreLib root from `../runtime10-corelib`, with
the raw source CoreLib and `cdac-bootstrap5/compiler`. Copy this directory to
`illink-workflow` outside the original runtime source tree, place the pinned
Cecil archive in the root, and run in CLANG64:

```sh
ROOT=/c/codex-dotnet10/c1.1BZ8KU
/usr/bin/python3 illink-workflow/prepare.py "$ROOT" "$ROOT/cecil-27abc572.tar.gz" "$ROOT/illink10-4"
bash illink-workflow/build.sh "$ROOT" "$ROOT/illink10-4"
/usr/bin/python3 illink-workflow/audit-source.py "$ROOT" "$ROOT/illink10-4"
/usr/bin/python3 illink-workflow/rewrite-corelib.py "$ROOT" "$ROOT/illink10-4" "$ROOT/illink10-4/corelib-rewrite2"
/usr/bin/python3 illink-workflow/validate-corelib.py "$ROOT" "$ROOT/illink10-4" "$ROOT/illink10-4/corelib-rewrite2"
```

Preparation and rewrite outputs must be new. The rewrite evaluates the original
`eng/illink.targets`, retaining the library root mode, skip/link actions,
symbol rewriting, descriptor, both shared/CoreCLR suppression files and the
original warning list. Assembly-root arguments precede descriptor arguments,
matching the original LinkTask ordering. The final rewrite has zero warnings.

Validation compares 31,189 public surface entries, including signatures,
visibility flags, constants and generic constraints. They are unchanged.
All four embedded resource payloads remain byte-identical, the rewritten PDB
loads successfully, and there are no assembly references. Original ILLink
removes unused internals: 2,957 types/43,227 methods become 2,897/42,365.
All 208 remaining InternalCall methods have zero body RVAs. The earlier helper
is not invoked. Basic JIT/generics/Unicode, compacting GC, exceptions/finally
and managed threading each execute with exit0 against the exact linked bytes.

Exact commands, hashes, initial failures and final results are recorded in
[`managed-results-20261027.json`](managed-results-20261027.json) and
[`managed-results-20261027.txt`](managed-results-20261027.txt).
No runtime10 or ILLink pacman package is installed by this workflow.
The dependency counts remain 50/53 direct, 52 transitive and 106 artifacts
including the four existing experimental packages.
