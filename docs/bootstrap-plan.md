# Source-only .NET toolchain for MSYS2: bootstrap plan (phases 1–3 report)

Goal: build Mono, then Roslyn, MSBuild, modern .NET and WindowsDesktop, and
finally Greenshot, entirely from source on MSYS2. No precompiled
.NET/Mono/Roslyn/MSBuild binary may be used at any point.

This report covers phases 1–3 only: the Guix chain, Windows feasibility, and
first builds. The detailed evidence is in:

- `docs/guix-mono-chain.md` — the Guix chain;
- `docs/graph.json` / `docs/graph.dot` — the machine-readable graph;
- `docs/msys2-mono-port.md` — the attempt log;
- `docs/roslyn-bootstrap.md`, `docs/msbuild-bootstrap.md` — what Guix
  already does for phases 4/5.

## 1. Executive summary

1. **Guix's chain is real and fully source-based**, and since 2026 it goes
   further than the 2024 blog post. It runs: `treecc` → `pnet` (cscc + ilrun,
   written in C) → `pnetlib` → **17 Mono builds** (1.2.6 … 6.12.0.206) →
   Roslyn 2.0 → 2.3 → 2.8 → 3.0 → 3.2 → 3.8 → 3.9, plus MSBuild 15.7.179.
   All of it is in `gnu/packages/dotnet.scm` (Guix master a473ec23efdd, see
   the commit table). **The first C# compiler is DotGNU's `cscc`, a C
   program**; no Mono or .NET binary is needed. Mono 1.2.6 is compiled with
   `EXTERNAL_MCS=cscc EXTERNAL_RUNTIME=ilrun`.
2. **Stage 0 is SOLVED on MSYS2 UCRT64 (x86_64).** treecc, pnet and pnetlib
   build with GCC 16.2 using 4 Guix patches and 4 new ones.
   `cscc -o h.exe h.cs && ilrun h.exe` runs. This is a working
   C# compiler and CLI runtime on 64-bit Windows built only from C sources,
   with no binary seed.
3. **Mono 1.2.6 on UCRT64 x86_64: REQUIRES PATCHING, well under way.** The
   C runtime builds. cscc compiles Mono's basic-profile `mcs.exe`,
   `mscorlib.dll` and `System.dll`. With 10 Win64/MinGW patches (on top of the
   2 from Guix) the new x86_64 JIT runs that mcs and prints `Mono C# compiler
   version 1.2.6.0`. The next blocker is a wrong vtable slot (Hashtable,
   slot 37 of 34) on the first real compile. The root cause is still open,
   most likely LLP64 again.
4. **The earliest point where the Guix chain fails on MSYS2** is therefore
   the first Mono (1.2.6). In 2007 its Windows support meant 32-bit x86
   built with Cygwin `gcc -mno-cygwin`. The Win64 JIT port had only "begun"
   (ChangeLog 2007-03/2007-10). Its stack-argument and unwind-info parts
   arrived in 2008 (svn r101545/r101861; `RtlInstallFunctionTableCallback`
   from 2.4.x on).
5. **Recommended first package:** `mingw-w64-pnet-bootstrap` (stage 0),
   written as `bootstrap/mingw-w64-pnet-bootstrap/PKGBUILD`. The result of
   running it is in §6.

## 2. Stage classification (MSYS2 UCRT64 unless noted)

| Stage | Class | Evidence |
|---|---|---|
| treecc 0.3.10 | **SOLVED** | Built and installed on win11. 1 patch (`-mno-cygwin`). msys2-mono-port.md A1 |
| pnet git 3baf947 | **SOLVED** | Built; cscc/ilrun work on x86_64. 4 Guix patches + `ucrt-timezone-macro` + `win64-findfirst-intptr`. A2 |
| pnetlib git c3c12b8 | **SOLVED** | 48 assemblies built; hello world compiles and runs. 1 patch. A3 |
| mono 1.2.6, x86_64 | **REQUIRES PATCHING** | C runtime builds; cscc-built mcs runs on the JIT (`--version` OK). 12 patches so far (2 Guix, 10 new). The next blocker is a vtable-slot mismatch in `Location.AddFile` → `Hashtable.Contains`. Further expected work: LLP64 audit, Win64 SEH/unwind info, xmm6–15 callee-saved, Win64 "stdcall" pop assumption, GC thread registration. A4 |
| mono 1.2.6, i686 (MINGW32) | **UNKNOWN** (experiment running, see §6) | This was the historically supported Windows target. MSYS2 mingw32 has no `gc`; built bdwgc 8.2.12 from source (`00-bdwgc.sh`). MSYS2's mingw32 repo has 230 packages versus 3909 in ucrt64, which is a long-term risk |
| mono 1.9.1 | **REQUIRES PATCHING** | Same configure (`-mno-cygwin`) and same Win64 state as 1.2.6 (no 2008-04 Win64 fixes in its ChangeLog). Most 1.2.6 patches should carry over. Guix needs `MONO_CREATE_IMAGE_VERSION` here |
| mono 2.4.2.3, 2.6.4 | **PROBABLY SOLVABLE** | Win64 unwind registration (`RtlInstallFunctionTableCallback`) present; eglib optional (`--with-glib`); still `-mno-cygwin` in configure (same 1-line patch) |
| mono 2.11.4, 3.0.12 | **REQUIRES PATCHING** | configure refuses a user `CC` ("mono can only be compiled with gcc-3 from the 'gcc-core' package") unless cross compiling; needs a configure patch |
| mono 3.12.1, 4.9.0 | **PROBABLY SOLVABLE** | configure treats `*-mingw*` natively (no forced CC, `with_tls=pthread`) |
| mono 5.0.1 … 6.12.0.206 | **PROBABLY SOLVABLE** | MSYS2 already builds 6.12.0.199 for ucrt64/clang64 with 5 small patches (`mingw-w64-mono`), but seeded with `make get-monolite-latest`. The Guix-specific parts (mcs-only `--with-csc=mcs`, reference assemblies rebuilt from `binary-reference-assemblies` sources, cherry-picked mcs) are platform-neutral |
| libgdiplus | **UNKNOWN** | Not needed on Windows (System.Drawing uses real GDI+) |
| SCI/SRM bootstrap, MSBuild 15.7.179, Roslyn 2.0→3.9 | **PROBABLY SOLVABLE** | Managed code compiled by direct `mcs`/`csc` command lines in Guix. Windows-only code paths (native PDB writer, `CodePagesEncodingProvider`) are already stubbed by Guix. Not tried |
| Modern Roslyn (4.x), MSBuild 16/17, .NET SDK/runtime, NuGet | **UNKNOWN** | Not investigated (phases 4–6). Guix has no dotnet package; the blog's "Future work" says the dotnet world "is an entirely different beast" |
| WindowsDesktop/WPF/WinForms, net48 targeting, Greenshot | **UNKNOWN** | Not investigated (phases 7–9) |

## 3. How the bootstrap works on MSYS2 (current scripts)

```
bootstrap/env.sh              common environment, poison PATH, MSYS2 arg-conversion exclusions
bootstrap/lib.sh              apply_patches (stamped, incremental), scan_binaries
bootstrap/00-bdwgc.sh         private bdwgc (win32 threads + DllMain), used for MINGW32
bootstrap/01-treecc.sh        stage 0a
bootstrap/02-pnet.sh          stage 0b (prep-pnet.sh = Guix snippet port)
bootstrap/03-pnetlib.sh       stage 0c (prep-pnetlib.sh = Guix snippet port)
bootstrap/stage0-all.sh       0a–0c in order
bootstrap/04-mono-1.2.6.sh    stage 1 (prep-mono.sh = Guix prepare-mono-source port)
bootstrap/run-remote.sh       Linux-side driver: rsync + run a stage on win11
bootstrap/tools/              mkpatch.sh, ln-s-rel, mono126-smoke.sh
bootstrap/mingw-w64-pnet-bootstrap/PKGBUILD   first package
patches/<stage>/NNNN-*.patch  every patch with a "Why:" header
```

Rules the scripts enforce:

- Every source tarball hash is checked; treecc and mono-1.2.6 match the Guix
  hashes, which I converted from nix-base32.
- All `*.dll`/`*.exe`/`*.so` in the Mono trees are deleted. This removes the
  4 monolite blobs of the 1.2.6 tarball.
- `scan_binaries` must return nothing before building.
- The binary-seeded host Mono is poisoned on PATH.

## 4. Binary inventory so far

| Source | Binaries found | Handling |
|---|---|---|
| treecc-0.3.10.tar.gz | none | – |
| pnet git 3baf947 | none (only bundled `libgc/`, `libffi/` *sources*) | bundled libs deleted (Guix), MSYS2 `gc`/`libffi` used |
| pnetlib git c3c12b8 | none | – |
| mono-1.2.6.tar.bz2 | `mcs/class/lib/monolite/{mcs.exe,mscorlib.dll,System.dll,System.Xml.dll}` | deleted by `prep-mono.sh`; `docs/` dropped from SUBDIRS (AgilityPack.dll) |
| later Mono (from Guix spec lists) | `roslyn-binaries`, `xunit-binaries`, `helix-binaries`, `binary-reference-assemblies` DLLs, monolite downloads | not fetched (roslyn/xunit); fetched then blob-deleted (helix); rebuilt from source (reference assemblies, ≥5.0.1); `--with-csc=mcs`; never `get-monolite-latest` |

## 5. Strategy options for getting past Mono 1.2.6 on Windows

- **A. Finish the x86_64 port of 1.2.6, then carry the patches to 1.9.1.**
  This is in progress and each fix has moved execution further. Backports
  from upstream 2008 Win64 work (Jonathan Chambers' commits) are the main
  source of fixes. From 2.4.x on, upstream's own Win64 support covers more
  of this.
- **B. Use i686 (MINGW32) for the early Monos.** This was historically
  supported. Managed outputs are architecture-neutral, so an i686
  Mono N can be the `EXTERNAL_MCS`/runtime for an x86_64 Mono N+1 once N+1
  has solid Win64 support. Since x86 Windows runs on WoW64, this handoff
  works on the same machine. The risk is that MSYS2's mingw32 environment is
  being wound down (no gc; we build bdwgc ourselves).
- **C. Shorten the chain via pnet**, as the Guix blog's "Future work"
  suggests: compile a newer `mcs` directly with cscc and run it on `ilrun`
  (already working on x86_64) against pnetlib. It would then build a later
  Mono whose Win64 JIT is mature, skipping the 2007-era JITs entirely.
  Status: UNKNOWN; it depends on how much C# 2/3 cscc handles in newer mcs
  sources.

Recommendation: keep A and B running in parallel for one more session, since
B is cheap now that the scripts are environment-aware, and evaluate C as the
long-term way to shorten the chain.

## 6. First PKGBUILD to implement

**`mingw-w64-pnet-bootstrap`** (stage 0) — `bootstrap/mingw-w64-pnet-bootstrap/PKGBUILD`

- **Sources:**
  - `https://download.savannah.gnu.org/releases/dotgnu-pnet/treecc-0.3.10.tar.gz`
    (sha256 `5e9d20a6…6efe7`, identical to Guix);
  - `git+https://git.savannah.gnu.org/git/dotgnu-pnet/pnet.git#commit=3baf94734d8dc3fdabba68a8891e67a43ed6c4bd`;
  - `git+https://git.savannah.gnu.org/git/dotgnu-pnet/pnetlib.git#commit=c3c12b8b0c65f5482d03d6a4865f7670e98baf4c`.
- **depends:** `${MINGW_PACKAGE_PREFIX}-gc`, `-libffi`, `-libwinpthread`.
- **makedepends:** `-cc`, `-autotools`, `-python`, plus `bison`, `flex`,
  `texinfo`, `git`.
- **prepare():** the Guix origin snippets (delete bundled libgc/libffi,
  generated autotools files and parsers; `-lgc`; `IL_CONFIG_UNROLL=n`), the
  patches, `autoreconf -vif`, a binary scan, and poison wrappers for any host
  Mono.
- **build():** treecc, then pnet (installed into a staging DESTDIR), then
  pnetlib with `CSCC_PLUGINS_PATH`/`CSCC_LIB_PATH` pointing at the staged
  pnet. Every configure call gets `--build=--host=${MINGW_CHOST}` and
  `CFLAGS="-O2 -g -std=gnu17 -Wno-error=…"`. Installation uses
  `LN_S=ln-s-rel`.
- **Install prefix:** `${MINGW_PREFIX}/lib/dotnet-bootstrap/pnet`, a private
  prefix, so it cannot clash with Mono's `ilasm`/`resgen`/`al`.
- **Patches (8):**
  - treecc 0001 (`-mno-cygwin`);
  - pnet 0001–0004 (Guix: newer libgc, newer texinfo, line-number info,
    off-by-one), 0005 (UCRT `timezone` macro), 0006 (Win64 `_findfirst`
    handle);
  - pnetlib 0001 (double `.exe`).
- **check():** compile a hello world with cscc and run it with ilrun.
- **Test status on win11:** see §7 / the commit log of this repository.

The next package would be `mingw-w64-mono-bootstrap-1.2.6`
(`bootstrap/04-mono-1.2.6.sh` turned into a PKGBUILD), once the JIT blocker
in §2 is fixed.

## 7. Status of the long-running experiments at the time of writing

See the bottom of this file, updated by the last commit of this session.

## 8. Later phases (stubs)

- **Phase 4 (Mono → Roslyn):** Guix already solves this, up to Roslyn 3.9
  (C# 9) on mono-bootstrap 6.12. See `roslyn-bootstrap.md`. The beyond-3.9
  steps (C# 10–12, needed by modern SDK sources) are UNKNOWN.
- **Phase 5 (MSBuild):** Guix builds MSBuild 15.7.179 with mcs. See
  `msbuild-bootstrap.md`. Modern MSBuild/SDK/NuGet cycles are UNKNOWN.
- **Phase 6 (modern .NET):** UNKNOWN; not investigated. Note that the Guix
  blog author explicitly gave up here.
- **Phases 7–9 (WindowsDesktop, net48 targeting, Greenshot):** UNKNOWN; not
  investigated.

## Open questions

1. Root cause of the Hashtable vtable-slot mismatch in x86_64 mono-1.2.6:
   LLP64 in metadata/vtable code, or a cscc/pnetlib-reference resolution
   difference that only shows on Win64?
2. Does i686 mono-1.2.6 (MINGW32 + private bdwgc) get further with fewer
   patches?
3. GC: MSYS2's `gc` is pthreads-based. Mono 1.2.6 Windows threads are
   CreateThread-based and relied on bdwgc's DllMain registration. Should the
   bootstrap always use the private win32-threads bdwgc (`00-bdwgc.sh`)?
4. Can a newer mcs be compiled by cscc directly (strategy C), and which
   version is the newest that works?
5. Is relying on MSYS2's shrinking mingw32 environment acceptable for an
   MSYS2 package, given that only the build host needs it and the artifacts
   are managed code?
