# Roslyn bootstrap (phase 4): findings so far

Status: **not investigated on MSYS2 yet.** Everything below comes from
Guix's implementation (`gnu/packages/dotnet.scm` at
a473ec23efdd19074b2615ea478767a596e452e3), which already solves the
Mono→Roslyn bridge on Linux. There is no experiment on Windows.

## What Guix does (2026-07, Danny Milosavljevic)

- **No MSBuild, Arcade or NuGet.** Each Roslyn version is compiled by direct
  `csc` command lines over `find-files` source lists. Small generated shims
  stand in for the build system: an `SR` class from
  `System.Reflection.Metadata` resx (via mono's `resx2sr`),
  `InternalsVisibleTo` and version attributes, and `.resources` files from
  `resgen`. The syntax generators (`CSharpSyntaxGenerator`,
  `BoundTreeGenerator`, `CSharpErrorFactsGenerator`) are compiled and run in
  2.0–2.3; from 2.8 on, Guix uses the checked-in generated files.
- **Chain** (each one is the native-input compiler of the next):

  | Roslyn | Compiled by | Guix patch | Guix commit |
  |---|---|---|---|
  | 2.0.0 (C# 7.0) | mono-bootstrap 6.12 `mcs -langversion:7.2` | roslyn-2.0.0-bootstrap-with-mono | 0648b5020f856216e4ee9daf7bc35a89e8fc0f00 |
  | 2.3.0 (C# 7.1) | csc 2.0 | roslyn-2.3.0-default-literal-for-csc-2.0 | a0a9240d0aaaab6067fbe6464f20d3942c0657cc |
  | 2.8.2 (C# 7.2 per dotnet.scm comment) | csc 2.3 | roslyn-2.8.2-csharp-7.2-for-csc-2.3 | ce5aae4199f0cbd08b6460f568fcbcdef58d9160 |
  | 3.0.0 | csc 2.8 | roslyn-3.0.0-bootstrap-with-csc-2.8 | d71affea0c59266b8fc75377c3e6d32878a29b56 |
  | 3.2.0 | csc 3.0 | roslyn-3.2.0-bootstrap-with-csc-3.0 | a883d8d02b76983f15d1a3733b13b46c174bdd03 |
  | 3.8.0 (C# 9) | csc 3.2 | roslyn-3.8.0-bootstrap-with-csc-3.2 (1366 lines) | 1e78c108e5e3bce2f53437baf1390545c11c184b |
  | 3.9.0 | csc 3.8 | roslyn-3.9.0-bootstrap-with-csc-3.8 | 8bd02af8349e07a2b93db8451054bf4ab78f56d8 |

- **mcs workarounds in Roslyn 2.0.** mcs crashes on `is null` and on `out var`
  inside iterator methods, and does not track definite assignment through
  `?.`. Guix rewrites these with substitutions (`fix-compiler-compat` phase).
- **Mono class-library incompatibilities.** There is a `KeyValuePair` name
  clash, and `CodePagesEncodingProvider` is unavailable. A
  `ComMemoryStream` stub is needed because the Windows native-PDB writer is
  never used on Linux.
- **System.Reflection.Metadata** sources (from mono's
  `external/corefx`) are compiled *inline* into Microsoft.CodeAnalysis.
  **System.Collections.Immutable** is built separately
  (`mono-system-collections-immutable-bootstrap`).
- **mcs fix needed.** mcs-compiled mscorlib lacks the `InAttribute` modreq on
  `ref readonly` returns, which Roslyn 3.8 requires.
  `mono-6.12.0-emit-ref-readonly-return-modreq.patch` (b6fc762d4674) fixes
  mcs.
- **Result.** `mono` (c143f00ce73e) = mono-bootstrap with
  `lib/mono/4.5/csc.exe` and the `Microsoft.CodeAnalysis*.dll` files replaced
  by roslyn-3.9. This is the same version as the `roslyn-binaries` blob
  upstream Mono 6.12.0.206 uses.
- **Gap:** the VB compiler (`Microsoft.CodeAnalysis.VisualBasic.dll`) is a
  TODO, because it is written in VB.

## Open questions for MSYS2

- The `ComMemoryStream` stub disables native PDB writing. On Windows, Roslyn
  would normally use `Microsoft.DiaSymReader.Native`, which is a
  proprietary blob, so portable PDBs only.
- How far beyond 3.9 can this chain go on Mono 6.12? Modern Roslyn (4.x)
  targets netstandard2.0/net8+, and its build assumes the .NET SDK. Status:
  UNKNOWN.
