# dotnet-mingw-bootstrap

Builds a .NET 8 SDK on Windows/MSYS2 from source, without any prebuilt
Microsoft .NET binary. The packaged results (Mono-seeded bootstrap tools,
the .NET 8 runtime/SDK/targeting packs, C++/CLI) are the `dotnet-*` recipes in
[msys2-ur](https://github.com/Kreijstal/msys2-ur).

## Two tracks

| Track | Seed | State |
|---|---|---|
| zero-seed (`bootstrap/env.sh`) | none: DotGNU pnet (C) → Mono 1.2.6 → … as in Guix | paused |
| m612 (`bootstrap/env-m612.sh`) | MSYS2's `mingw-w64-ucrt-x86_64-mono` 6.12 | self-hosting .NET 8 SDK reached |

The m612 chain is Mono 6.12 → Roslyn → MSBuild → .NET 8 runtime, libraries
and SDK (`bootstrap/m612/`). The documentation is in `docs/`:
`bootstrap-plan.md` and `guix-mono-chain.md` (goal, zero-seed track),
`msys2-mono-port.md`, `roslyn-bootstrap.md`, `msbuild-bootstrap.md` and
`phase6-dotnet.md` (Mono 6.12 to the self-hosting .NET 8 SDK).

## WPF and Windows Forms

WPF and Windows Forms moved to their own repository,
[wpf-mingw](https://github.com/Kreijstal/wpf-mingw), with their history. Its
scripts go into `bootstrap/m612/` of this checkout, where they use the shared
environment and `libraries.sh`. Commits up to the split still contain them
here, which is what the existing msys2-ur recipes pin.
