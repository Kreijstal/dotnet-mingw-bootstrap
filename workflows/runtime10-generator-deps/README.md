# Source dependencies for the .NET10 generator bundle

Ten private net8 NuGet artifacts now build and pass fresh-consumer checks:
eight original .NET9 libraries plus the Roslyn4.14 Common and CSharp compiler
libraries. These are prerequisites for the original runtime10 generator bundle.
Workspaces4.14 subsequently passed the separate
[Workspaces bootstrap](../runtime10-workspaces414/README.md).
Roslyn/Workspaces4.4, the original generator TFMs and the final
targeting-pack/SDK10 packages remain open.

Runtime10 `eng/Versions.props` pins LatestVS Roslyn4.14 and the JSON generator's
Roslyn4.4 API. The published source revisions are recorded by the
[4.14 Workspaces package](https://www.nuget.org/packages/Microsoft.CodeAnalysis.CSharp.Workspaces/4.14.0)
and [4.4 Common package](https://www.nuget.org/packages/Microsoft.CodeAnalysis.Common/4.4.0).
The latter source has been acquired and pinned; it has not been built here.

| Source | Revision | Archive SHA256 |
| --- | --- | --- |
| runtime9.0.0 | `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3` | `32755d528ead386762fb9da054249f6151ac78ef6a92f437044838cd0ab00def` |
| Roslyn4.14.0 | `8edf7bcd4f1594c3d68a6a567469f41dbd33dd1b` | `8cf2d365d1fad52b480f7c68b5c477cb586a935ada0eb309b0c40dacfec5fe9c` |
| Roslyn4.4.0, acquired only | `1ce8866c9de4c2d67351ef6863699dee03b0804a` | `9edb8c430576c5f5b27032afe8f79c74508b0150e03406a7a4ca6f218e03fd11` |

The runtime9 artifacts are Collections.Immutable, Reflection.Metadata,
Composition.AttributedModel/Runtime/Hosting/Convention/TypedParts and IO.Pipelines,
all version9.0.0. Their original projects support net8. MSBuild selects their
complete original source sets; no production C# files are edited. Binary inputs
from the source archives are removed and recorded. The private feed preserves
their exact9.0.0 dependency graph and original public keys.

Runtime checks load all eight exact source-built assemblies from a fresh
consumer output and exercise immutable collections, the newer metadata type-name
parser, MEF imports/shared exports and an asynchronous Unicode pipe. All seven
original resource sets and213 values match. Fresh cache archives and every
consumer managed asset match the private source NuGets byte for byte.

Roslyn4.14 retains all1,025 Common and1,127 CSharp selected source inputs.
Its original syntax/bound-tree/error-facts tools regenerate all five compiler
source outputs, which match the upstream checked-in baselines after the
original console generator's documented whitespace cleanup. The original
source-build defines, friend keys, explicit resource names and upstream
RSEXPERIMENTAL005 build setting are preserved.

The compiler consumer checks C#13 params spans, primary constructors and
collection expressions; semantic models; deterministic PE/portable-PDB emission
and execution; diagnostics/cancellation; analyzer loading/callbacks; and
incremental source generation. All4,242 Common and6,237 CSharp upstream public
API declarations match, including experimental markers. Property-level
experimental attributes are checked independently of getter signatures.
All42 resource sets/39,956 values,13 cultures and German diagnostics pass.

The source SDK8.0.131-3/runtime8.0.31-3 and the unchanged source Roslyn5 compiler
adapter from `../runtime10-coreclr` bootstrap these projects. Compilation uses
the source9 dependency packages; an independent audit confirms all11 actual
compiler-cache package references and every cached DLL match the private source
archives. A separate local audit checks493 runtime9 and2,217 Roslyn selected
source/project/resource inputs directly against the pinned archives.

Copy this workflow to `generator-deps-workflow` in the prepared runtime10 root,
with the three source archives beside it. The runtime dependency preparation
also uses the MSBuild evaluator under `reference10-2/evaluation-tool` from the
preceding reference-contract checkpoint. Example commands in CLANG64:

```sh
ROOT=/c/codex-dotnet10/c1.1BZ8KU
/usr/bin/python3 generator-deps-workflow/prepare-runtime9.py "$ROOT" "$ROOT/roslyn-deps9-1"
bash generator-deps-workflow/build-runtime9.sh "$ROOT/roslyn-deps9-1"
/usr/bin/python3 generator-deps-workflow/check-runtime9.py "$ROOT" "$ROOT/roslyn-deps9-1" "$ROOT/roslyn-deps9-1/consumer1"
/usr/bin/python3 generator-deps-workflow/prepare-roslyn414.py "$ROOT" "$ROOT/roslyn414-1"
bash generator-deps-workflow/build-roslyn414.sh "$ROOT/roslyn414-1"
/usr/bin/python3 generator-deps-workflow/check-roslyn414.py "$ROOT" "$ROOT/roslyn414-1" "$ROOT/roslyn414-1/consumer3"
/usr/bin/python3 generator-deps-workflow/audit-compile-sources.py "$ROOT"
```

Preparation and consumer outputs must be new. `prepare-roslyn414.py --resume`
can refresh private wrappers after verifying the previous pinned source hashes.
The Roslyn build rechecks all generated sources before reusing that output.
The recorded run needed targeted corrections to the private feed directory,
upstream warning settings and API verifier; exact successful targeted build
commands and those corrections are retained in the evidence. A final clean
workflow/package rebuild remains required before formal pacman packaging.

Detailed results and hashes are in
[`source-results-20261027.json`](source-results-20261027.json) and
[`source-results-20261027.txt`](source-results-20261027.txt).
Dates in those filenames are UTC; the local Berlin date is2026-10-28.
These artifacts are private net8 prerequisites, with no runtime9 or compiler
pacman installation performed. The complete generator bundle with its required
TFMs/dependencies, clean package/license audits, installed targeting-pack gate,
framework/SDK10/WindowsDesktop10/PowerShell7.6 and prior compatibility work remain
open. The established dependency counts remain50/53 direct,52 transitive and106
artifacts including the four earlier experimental packages.
