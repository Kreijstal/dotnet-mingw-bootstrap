# Source Roslyn 4.14 Workspaces prerequisites

Four private net8 NuGet artifacts build and pass fresh-consumer checks:
Microsoft.CodeAnalysis.Workspaces.Common and CSharp.Workspaces 4.14.0,
System.Threading.Channels 9.0.0, and the System.Composition 9.0.0 metapackage.
These extend the [compiler dependency bootstrap](../runtime10-generator-deps/README.md).
They are prerequisites for the .NET 10 generator work, with no pacman installation
or increase in the established PowerShell dependency counts.

The original projects support net8. MSBuild selects 1,515 Common Workspaces,
271 CSharp Workspaces and 27 Channels production C# inputs; none are edited.
Original source-build conditions retain the upstream SQLite exclusion. Channels
7.0.0 is the upstream minimum dependency. The selected complete source 9.0.0
implementation satisfies that minimum; its package, assembly and emitted
dependency metadata all retain their actual version 9 identity.

Sources reuse the pinned archives from the preceding bootstrap:

| Source | Commit | Archive SHA256 |
| --- | --- | --- |
| Roslyn 4.14 | `8edf7bcd4f1594c3d68a6a567469f41dbd33dd1b` | `8cf2d365d1fad52b480f7c68b5c477cb586a935ada0eb309b0c40dacfec5fe9c` |
| runtime 9.0.0 | `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3` | `32755d528ead386762fb9da054249f6151ac78ef6a92f437044838cd0ab00def` |

The archive audit verifies 1,886 selected Roslyn and 30 runtime inputs directly
against those archives. All four NuGets preserve source repository metadata and
explicit dependencies. All 26 actual compiler-cache package references and
every cached managed DLL match the source feeds.

A fresh consumer verifies all 1,834 Common and 56 CSharp public API declarations,
104 and 29 expanded friend attributes, and 98 resource sets containing 3,486
original strings. Runtime checks cover MEF composition, Unicode documents,
project references and semantic models, cross-project reference search/rename,
formatting, simplification, solution changes, PE emission, analyzer diagnostics,
DocumentEditor/code fixes, CodeAction application, incremental edits and
cancellation. Channels checks cover backpressure, cancellation, DropOldest,
2,000 multiwriter items and completion-error identity.

Cache verification checks every archived DLL. Consumer-output verification checks
every runtime/resource asset actually selected by NuGet, including its source
bytes. API verification follows each original baseline's nullable mode.

Copy this directory to `workspaces414-workflow` in the prepared Windows CLANG64
runtime root. It uses the preceding `roslyn-deps9-1` and `roslyn414-1` source/feed
outputs, the source SDK8.0.131-3/runtime8.0.31-3 and the unchanged source Roslyn5
compiler adapter. Preparation and consumer output directories must be new:

```sh
ROOT=/c/codex-dotnet10/c1.1BZ8KU
/usr/bin/python3 workspaces414-workflow/prepare.py "$ROOT" "$ROOT/workspaces414-1"
bash workspaces414-workflow/build.sh "$ROOT/workspaces414-1"
/usr/bin/python3 workspaces414-workflow/check.py "$ROOT" "$ROOT/workspaces414-1" "$ROOT/workspaces414-1/consumer3"
/usr/bin/python3 workspaces414-workflow/audit.py "$ROOT" "$ROOT/workspaces414-1"
```

All four commands returned zero in the recorded run. Detailed results, artifact
hashes and fixture corrections are in [the JSON record](source-results-20261027.json)
and [the build/test logs](source-results-20261027.txt). Filenames use UTC;
the local Berlin date was 2026-10-28.

The original generator bundle and required shipping TFMs, Roslyn/Workspaces 4.4
for JSON generation, clean formal package/license/install validation, and the
complete .NET 10 framework/SDK/desktop/PowerShell 7.6 closure remain open.
