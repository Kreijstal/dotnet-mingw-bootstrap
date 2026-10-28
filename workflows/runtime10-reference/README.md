# .NET 10 reference-contract bootstrap

All public reference contracts declared by runtime v10.0.5 now compile from
source: 167 public assemblies and 19 private facade-stub assemblies. Independent
metadata and source/archive checks pass. This is a private contract bootstrap;
the complete targeting pack, managed framework, SDK10 and PowerShell7.6 remain
unfinished.

The original `NetCoreAppLibrary.props` declares 172 runtime assemblies, of which
five are private implementations excluded from the public reference graph.
Actual MSBuild evaluates all 186 selected contract/stub projects. Private
project copies retain their source conditions and items, with paths rebased and
bootstrap imports supplied outside the original source tree. An independent
comparison verifies all 286 copies against the pinned inputs.

All 203 selected production C# files and 286 project/property files match the
pristine source tree and pinned runtime archive. The archive SHA256 is
`1692acae5e3adfd625452fbc9db4fd4e982c10e38189358ae245c74dd5514d7a`.
There are no production C# edits. The 19 stub source files come from the original
`CreateCompileSourceForForwardedTypes` target. These stubs are private compiler
inputs and provide no runtime implementation of their forwarded types.

The direct source Roslyn5 compile uses `nostdlib`, `refonly`, and the original
project-dependency closure. It uses no binary .NET10 targeting pack. Metadata
checks verify every assembly name, version, public key, file version, reference
marker and dependency identity. The original pinned GenFacades task applies
the source-declared zero-reference-version policy to 22 facades. Original
Arcade version calculation handles VisualBasic.Core's major15 override:
assembly15.0.0.0, file15.0.526.11800. Other assembly versions retain their
original project overrides. The build records 44 warning lines (CS8500/CS8617)
and zero compiler errors.

The consumer uses .NET10 `Enumerable.Sequence<int>` and `AsyncEnumerable.Range`,
`System.Threading.Lock`, and a C#14 field-backed property. It compiles with all
167 source reference assemblies. The same compiler and source fail with the
installed net8 reference pack, including the expected missing Sequence and
AsyncEnumerable diagnostics. This checks compilation; the consumer is not
executed before the matching managed framework is built.

Start from the prepared runtime/CoreLib root described in
`../runtime10-corelib`, with `cdac-bootstrap5/compiler` and the original Arcade
version task under `version-build`. Source SDK8.0.131-3/runtime8.0.31-3 run the
private build tools; SDK10 is not used or claimed. Copy this workflow to
`reference-workflow` outside the original runtime tree, then run in CLANG64:

```sh
ROOT=/c/codex-dotnet10/c1.1BZ8KU
/usr/bin/python3 reference-workflow/prepare.py "$ROOT" "$ROOT/reference10-2"
bash reference-workflow/evaluate.sh "$ROOT" "$ROOT/reference10-2"
/usr/bin/python3 reference-workflow/build.py "$ROOT" "$ROOT/reference10-2"
/usr/bin/python3 reference-workflow/validate.py "$ROOT" "$ROOT/reference10-2"
```

Preparation requires a new output root; validation creates fresh audit and
consumer subdirectories. The build verifies input
fingerprints and output hashes before reusing any previously compiled project.
The original source date determines reproducible source file versions; these
are not official Microsoft binary build numbers.

The local independent archive/project audit used:

```sh
python3 tests/runtime10-reference/audit-pristine.py \
 /tmp/runtime10-reference1/evidence \
 /tmp/dotnet10-sources/runtime-10.0.5 \
 /tmp/dotnet10-sources/runtime-v10.0.5.tar.gz
```

The evidence includes the original source hashes, actual evaluated inputs,
private project copies, all 186 output DLLs, per-project compiler logs and
metadata, and both consumer responses. See
[`reference-results-20261027.json`](reference-results-20261027.json) and
[`reference-results-20261027.txt`](reference-results-20261027.txt).

The remaining targeting-pack work is the complete original source-generator
bundle and its declared dependencies, final package metadata/layout, clean
package/archive audits, and an installed-pack SDK consumer gate. No runtime10
or targeting10 pacman package is installed by this workflow. The full managed
and native framework, SDK10.0.201, WindowsDesktop10, PowerShell7.6 and the
previously recorded compatibility work remain open. Dependency counts stay
50/53 direct, 52 transitive and 106 artifacts including four experimental.

The first declared generator prerequisites now also build and run: original
Roslyn4.14 compiler libraries and eight source9 dependency libraries. Their
API/resource, actual compiler-input and fresh-consumer gates pass; see
[`../runtime10-generator-deps/README.md`](../runtime10-generator-deps/README.md).
Workspaces4.14/4.4 and the complete original generator bundle remain open.
