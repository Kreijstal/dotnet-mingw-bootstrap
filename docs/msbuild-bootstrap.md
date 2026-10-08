# MSBuild bootstrap (phase 5): findings so far

Status: **MSBuild 17.8.49 (net472) built from source on Mono on MSYS2** (see
`phase6-dotnet.md`, `bootstrap/m612/msbuild.sh`). The facts below come from Guix
(`gnu/packages/dotnet.scm` at a473ec23efdd19074b2615ea478767a596e452e3).

## Guix `msbuild` 15.7.179 (commit 5239ec21fda51687671c6667a3bffbcd32bbc9d5, 2025-06)

- **Source:** `https://github.com/dotnet/msbuild`, tag `v15.7.179`. The
  dotnet.scm comments say "too new version: 15.9.21.664" and "too old (no
  support for mono) version: 14.0".
- **Build:** no MSBuild is needed to build it. The `configure` phase writes
  version/IVT files, and `mcs -langversion:7.2 -unsafe -d:NET472
  -d:STRONG_NAME …` is invoked directly on source lists. Resources go through
  `resgen`. Patches: `mono-msbuild-15.7.179-fix-build.patch` and
  `mono-msbuild-15.7.179-fix-resources.patch`.
- **Inputs:** `mono-bootstrap` (6.12.0 with mcs),
  `mono-system-reflection-metadata-bootstrap` and
  `mono-system-collections-immutable-bootstrap`. All three are compiled from
  the corefx sources inside the Mono 6.12 tree, using `resx2sr` from
  mono ≥ 5.1.0.
- **Install:** `lib/MSBuild.exe` plus targets. `MSBuild.exe.config` gets a
  `<runpath>` entry (Guix's mono-6.12.0-add-runpath extension) pointing at
  the SRM/SCI directories.
- **Tests:** none (they need xunit-binaries).

## Relevance for the .NET SDK cycle

MSBuild 15.7 targets .NET Framework 4.7.2 on Mono. It is useful for building
old-style projects and as a step toward newer MSBuild versions. Modern MSBuild
(17.x) requires the .NET SDK. That cycle (Roslyn ↔ MSBuild ↔ SDK ↔ NuGet ↔
reference assemblies) is UNKNOWN on MSYS2 and is the subject of phase 5/6.
