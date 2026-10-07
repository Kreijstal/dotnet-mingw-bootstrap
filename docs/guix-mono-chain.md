# The GNU Guix Mono bootstrap chain

Everything here was read from the Guix source itself, not from summaries.

- **Guix checkout:** `https://codeberg.org/guix/guix.git` at master
  `a473ec23efdd19074b2615ea478767a596e452e3` (2026-10-07).
- **Definitions:** `gnu/packages/dotnet.scm`. There is no `mono.scm`. The
  old `gnu/packages/mono.scm`, with the binary-seeded mono 4.4.1 added in
  763b3d50b6249b43fceda51445bbeb1f5f5fd7d0 (2016-07-13), was deleted by
  bd638a8ad6140db87851a86faae89e736e148f3d (2022-09-01, "gnu: Delete mono
  and dependent packages").
- **Patches:** `gnu/packages/patches/{pnet,mono,corefx,roslyn}-*.patch`
  (34 files; list below).
- **Blog post:** "Adding a fully-bootstrapped Mono", unmush, 2024-12-29,
  https://guix.gnu.org/en/blog/2024/adding-a-fully-bootstrapped-mono/.
  The series is authored 2024-11-26/27 and committed 2024-12-22.
- **Later additions:** Danny Milosavljevic added the GCC 14 fixes and
  reproducibility fixes in 2025, MSBuild in 2025-06, and Roslyn 2.0→3.9 plus a
  "mono with a csc" in 2026-07 (commit dates below). Guix today therefore goes
  all the way from C to Roslyn 3.9 and MSBuild 15.7.

## How the very first C# compiler is bootstrapped

There is **no binary seed and no C#-compiler-free Mono stage**. The first
C# compiler is **DotGNU Portable.NET's `cscc`, written in C**. Its
runtime, `ilrun` (the "CVM" interpreter, optionally with libjit), is also C.

1. `treecc` 0.3.10 (C, GPL). This is DotGNU's AST/"tree compiler-compiler".
   It is built from the release tarball
   `mirror://savannah/dotgnu-pnet/treecc-0.3.10.tar.gz`, Guix sha256
   (nix-base32) `1rzgnspg2xccdq3qsx0vi3j28h4qkrzbrjnhzvnny34fjfk217ay` =
   hex `5e9d20a6938e0c6fedfed0cabc7e9e984024e4881b748d076e8c75f1aeb6efe7`.
   I verified this hash against the tarball.
2. `pnet-git` at commit `3baf94734d8dc3fdabba68a8891e67a43ed6c4bd`
   (2011-06-15) of `https://git.savannah.gnu.org/git/dotgnu-pnet/pnet.git`.
   Dependencies are GCC, bison, flex, treecc, libgc (system), libffi, and
   optionally libjit. The Guix snippet deletes the bundled `libffi/` and
   `libgc/`, all pregenerated autotools output, and every
   `*_grammar.[ch]`/`*_scanner.[ch]`, so the parsers are regenerated from
   `.y`/`.l`/`.tc`. It also links `-lgc` instead of the bundled `libgc.a` and
   sets `IL_CONFIG_UNROLL=n` in `profiles/full`. Patches:
   `pnet-newer-libgc-fix`, `pnet-newer-texinfo-fix`,
   `pnet-fix-line-number-info`, `pnet-fix-off-by-one`. The blog explains why
   git is used rather than the 0.8.0 release: 0.8.0 cannot do `+=`/`-=` on
   pointers in unsafe code, among other gaps.
3. `pnetlib-git` at commit `c3c12b8b0c65f5482d03d6a4865f7670e98baf4c`
   (2011-07-21). This is DotGNU's class library (C#), compiled by `cscc` and
   run on `ilrun`.
4. `mono-1.2.6`, built from the release tarball
   `http://download.mono-project.com/sources/mono/mono-1.2.6.tar.bz2` (sha256
   hex `24cc4f39…c3f560f`; verified). It uses
   `make EXTERNAL_MCS=<pnet>/bin/cscc EXTERNAL_RUNTIME=<pnet>/bin/ilrun`.
   pnet's cscc compiles Mono's `mcs` and core class libraries (basic
   profile); those then run on Mono's own C runtime, built by GCC, to build
   the rest of Mono 1.2.6.

The tarball ships `mcs/class/lib/monolite/{mcs.exe,mscorlib.dll,System.dll,System.Xml.dll}`.
These are exactly the binary seed that upstream used. Guix deletes them, with
every other `*.dll`/`*.exe`/`*.so`, in `prepare-mono-source-0`.

`mono-1.2.6-bootstrap.patch`, from Guix commit 7d96fa640a6b, makes Mono 1.2.6
compilable by cscc and runnable on ilrun. It touches mcs's `codegen.cs` and
`typemanager.cs`, plus corlib's `Object.cs`, `MulticastDelegate.cs`, and
Remoting `MethodCall`/`MethodResponse`/`ReturnMessage`. It rewrites anonymous
delegates and C# 2 constructs in `LocalFileEventLog.cs`, `InotifyWatcher.cs`,
`ServicePoint.cs` and `BaseMachine.cs`, comments out a `[return: MarshalAs]`
in `ICollectData.cs`, and changes C code (`class.c`, `object.c`,
`processes.c`, `metadata/Makefile.am`, `runtime/Makefile.am`).

`System.Object` is not marked `[Serializable]`, because that creates a
circular class reference under pnet. For this reason Guix sets
`#:tests? #f` (dotnet.scm comment).

## The version chain

The arrow means "native-input used as the build compiler and runtime".
Commit = the Guix commit that added the package. All Mono sources after
1.2.6 are git tags or commits of `https://gitlab.winehq.org/mono/mono.git`.
GitHub `mono/mono` has the same objects; I checked tags in a clone of it.

| # | Guix package (version) | Source | Built with | GC | Guix patches | Added in |
|---|---|---|---|---|---|---|
| 0a | treecc 0.3.10 | tarball | GCC | – | – | f2098a776afff1deb23c5cbcd2c2d80278b9a208 |
| 0b | pnet-git 0.8.0-0.3baf947 | git 3baf9473 | GCC + treecc + bison/flex | system libgc | pnet-newer-libgc-fix, pnet-newer-texinfo-fix, pnet-fix-line-number-info, pnet-fix-off-by-one | 1b1e9917ad043ac279bd829cd8ac6a842667f648 |
| 0c | pnetlib-git 0.8.0-0.c3c12b8 | git c3c12b8b | pnet cscc/ilrun | – | – (snippet only) | d65ac39c9130284692a0c856b2fc71241a2f8913 |
| 1 | mono 1.2.6 | tarball | **pnet cscc + ilrun** (EXTERNAL_MCS/RUNTIME) | boehm (system, `--with-gc=boehm`) | mono-1.2.6-bootstrap, mono-1.2.6-callsite-bound | 7d96fa640a6b71af444493f745a93837a7ea3e70 (callsite-bound: da0de42721d8262fdc1932688352f405b5d96415) |
| 2 | mono 1.9.1 | tag mono-1.9.1.1 | mono 1.2.6 | boehm | mono-1.9.1-fixes, mono-1.9.1-add-MONO_CREATE_IMAGE_VERSION, mono-1.9.1-reproducibility | 893bd4a8ef39f33ba5c2cf74a750370ee43c9735 |
| 3 | mono 2.4.2.3 | tag mono-2-4-2-3 | mono 1.9.1 | boehm | mono-2.4.2.3-reproducibility, -fixes, -fix-parallel-builds | 436648994eaed5092393da4d6bdab6f0958dc7f2 |
| 4 | mono 2.6.4 | tag mono-2.6.4 | mono 2.4.2.3 | boehm | mono-2.4.2.3-reproducibility, mono-2.6.4-fixes | ff9771114de0856c803cf777f0fe6338cae9acc3 |
| 5 | mono 2.11.4 | tag mono-2.11.4 + 4 submodules | mono 2.6.4 | boehm | mono-2.11.4-fixes | 60314dbf81638b86b3a4ad4872f9e938f8a957cd |
| 6 | mono 3.0.12 | tag mono-3.0.12 + 7 submodules | mono 2.11.4 | boehm | – | 6e8c6cfafa0e8b9937098ce136a67e4ca4a2155a |
| 7 | mono 3.12.1 | tag mono-3.12.1 + 8 submodules | mono 3.0.12 | boehm | – | 5989da223e65ce2fcea1c5f46ad5256208beb0e4 |
| 8 | mono 4.9.0 | commit 5a3736606e6243d2c84d4df2cf35c284214b8cc4 + 12 submodules | mono 3.12.1 | **sgen** (`--with-sgen=yes --disable-boehm --with-csc=mcs`) | mono-4.9.0-fix-runtimemetadataversion | 011759c1cd2f52d6543d30561bc6815693918032 |
| 9 | mono 5.0.1 | tag mono-5.0.1.1 + 17 submodules | mono 4.9.0 | sgen | – | 5f65625a709f0a74bb852756971675f9032a8f43 |
| 10 | mono 5.1.0 | commit 6fafd08b507c56f11a2eb6570703a39e5bdc0a81 | mono 5.0.1 | sgen | – | 84fcfb9f4f7a69dc0ecd5871e5e36d978ecedce0 |
| 11 | mono 5.2.0.224 | tag | mono 5.1.0 | sgen | – | 959ab3571861989e5462125a4d4c81056f9d9582 |
| 12 | mono 5.4.0.212 | tag | mono 5.2.0 | sgen | mono-5.4.0-patches + corefx-mono-5.4.0-patches (applied to the corefx submodule) | e87bd7473ad41049240ebe354f30e310363cb4db |
| 13 | mono-pre-5.8.0 (5.4.0.201-0.d0f51b4) | commit d0f51b4e834042cfa593748ada942033b458cc40 | mono 5.4.0 | sgen | mono-5.4.0-patches + corefx-mono-pre-5.8.0-patches | b0ef15d7f41ace2d15cbef64b85d92fa20d154d3 |
| 14 | mono 5.8.0.129 | tag | mono-pre-5.8.0 | sgen | mono-5.8.0-patches | 27d7d879b79230143c81b114d98fd977325ea36b |
| 15 | mono-pre-5.10.0 (5.8.0.129-0.3e9d7d6) | commit 3e9d7d6e9cf8dc33eb29c497c350a1cd7df3a057 | mono 5.8.0 | sgen | **mono-mcs-patches-from-5.10.0** (90 file diffs) | bdfdd356bfd61a2fcdc869ab2792646ea190a80d |
| 16 | mono 5.10.0.179 | tag | mono-pre-5.10.0 | sgen | **mono-5.10.0-later-mcs-changes** (97 file diffs: mcs/mcs commits up to 6.12) | 8e8653fcfb6081f1e96d7cf876faeeda8bd52e30 |
| 17 | mono 6.12.0.206 (`mono-bootstrap`; was public `mono`) | tag + 19 submodules | **mono 5.10.0** (mcs patched to the 6.12 compiler) | sgen | mono-6.12.0-fix-ConditionParser, -add-runpath, -fix-AssemblyResolver, -emit-ref-readonly-return-modreq (2026) | 84df0c4f396bcde7b0d0f5a6bb7455efa4a2f643 (renamed ba2e09c171a3; modreq patch b6fc762d4674) |

That is 17 Mono packages (1.2.6 … 6.12.0), as the blog says, plus 3 DotGNU
packages. The chain continues past Mono, as of Guix master 2026-10:

| Guix package | Built with | Patch | Added in |
|---|---|---|---|
| libgdiplus 6.2 | GCC | – | a46816dcd155e0544e745deb7256362574fe3154 (updated 54cc2887938812be9f571b1a3ed27cd96dde3353) |
| mono-system-collections-immutable-bootstrap | mono-bootstrap `mcs -langversion:7.2` on `external/corefx/src/System.Collections.Immutable/src` | – | e365fc68f3f48167dcb1b63d6acbc1d857d1ed15 |
| mono-system-reflection-metadata-bootstrap | mcs on corefx `System.Reflection.Metadata/src` | – | 15a43c743102b5026eccb82be94af30de799f79f |
| msbuild 15.7.179 (tag v15.7.179, github dotnet/msbuild) | mcs, invoked directly on file lists (no MSBuild needed); `-langversion:7.2 -d:NET472` | mono-msbuild-15.7.179-fix-build, -fix-resources | 5239ec21fda51687671c6667a3bffbcd32bbc9d5 |
| roslyn 2.0.0 (tag version-2.0.0) | **mcs** (`mcs -langversion:7.2` wrapper named csc), with SRM sources compiled inline | roslyn-2.0.0-bootstrap-with-mono | 0648b5020f856216e4ee9daf7bc35a89e8fc0f00 |
| roslyn 2.3.0 | csc 2.0 | roslyn-2.3.0-default-literal-for-csc-2.0 | a0a9240d0aaaab6067fbe6464f20d3942c0657cc |
| roslyn 2.8.2 | csc 2.3 | roslyn-2.8.2-csharp-7.2-for-csc-2.3 | ce5aae4199f0cbd08b6460f568fcbcdef58d9160 |
| roslyn 3.0.0 | csc 2.8 | roslyn-3.0.0-bootstrap-with-csc-2.8 | d71affea0c59266b8fc75377c3e6d32878a29b56 |
| roslyn 3.2.0 | csc 3.0 | roslyn-3.2.0-bootstrap-with-csc-3.0 | a883d8d02b76983f15d1a3733b13b46c174bdd03 |
| roslyn 3.8.0 (C# 9) | csc 3.2 | roslyn-3.8.0-bootstrap-with-csc-3.2 | 1e78c108e5e3bce2f53437baf1390545c11c184b |
| roslyn 3.9.0 (= version in mono's roslyn-binaries for 6.12.0.206) | csc 3.8 | roslyn-3.9.0-bootstrap-with-csc-3.8 | 8bd02af8349e07a2b93db8451054bf4ab78f56d8 |
| `mono` (6.12.0.206 + csc) | trivial build: symlink tree of mono-bootstrap, replaces `lib/mono/4.5/{csc.exe,Microsoft.CodeAnalysis*.dll,System.Collections.Immutable.dll}` with roslyn-3.9's | – | c143f00ce73ee0603f50f70aa0846c7e1f381ed0 |

The VB compiler is still missing: dotnet.scm has
`TODO: Microsoft.CodeAnalysis.VisualBasic.dll`, because it is written in VB.
Roslyn's own build system (MSBuild + NuGet + Arcade) is not used anywhere.
Each Roslyn assembly is compiled with one direct `csc` command line plus
generated `SR`/IVT/version shims, and `resgen` for `.resx`.

## Why each stage exists

- **pnet/pnetlib:** the only C# compiler and runtime written in C. They are
  the source-only root.
- **1.2.6:** the oldest Mono that cscc (plus one patch) can compile.
- **1.9.1:** the blog says the 3rd or 4th later version always failed with
  missing methods. Mono writes into new assemblies the runtime version of the
  assembly it is running, and that version traces back through 1.2.6 to pnet,
  which emits v2.0. The fix is
  `mono-1.9.1-add-MONO_CREATE_IMAGE_VERSION.patch`, a `getenv` in
  `create_dynamic_mono_image`, with phase `set-create-image-version`
  setting `MONO_CREATE_IMAGE_VERSION=v1.1.4322`.
- **2.4.2.3, 2.6.4, 2.11.4, 3.0.12, 3.12.1:** steps forward in C# language
  level and class library. Each tagged release can build the next. Starting
  with 2.11.4 the submodules are listed by hand, because their `git://` URLs
  are dead. 2.11.4 is the first version with git submodules.
- **4.9.0** (a commit, not a release): this is where external Boehm stopped
  being supported ("External Boehm is no longer supported"), so Guix switches
  to sgen and sets `--with-csc=mcs`.
  `mono-4.9.0-fix-runtimemetadataversion.patch` makes mcs accept
  `/runtimemetadataversion`, which "saves us an extra intermediate step".
- **5.0.1 … 5.8.0, with the pre- commits:** corefx sources arrive, and Mono
  uses C# features before mcs implements them. The `pre-` packages are
  commits that need fewer patches than a tagged release
  (`mono-5.4.0-patches`, `corefx-mono-5.4.0-patches`,
  `corefx-mono-pre-5.8.0-patches`, `mono-5.8.0-patches`). Releases
  5.4.0.135 and older are not self-hosting (`CS0738` on `SqlTransaction`),
  hence 5.4.0.212.
- **pre-5.10.0, 5.10.0:** these use the cherry-pick trick.
  `mono-mcs-patches-from-5.10.0.patch` backports mcs/mcs commits from 5.10
  into a 5.8 snapshot. `mono-5.10.0-later-mcs-changes.patch` backports every
  later mcs/mcs commit up to 6.12 into 5.10.0, giving "essentially the 6.12.0
  compiler" running on 5.10. That compiler then jumps straight to 6.12.0.
  The blog: "the dependencies on new features were all coming from corefx,
  not from Mono's compiler."
- **6.12.0:** the final Mono. It carries the xbuild ConditionParser fix, the
  `MONO_GAC_PREFIX` fix (`mono-6.12.0-fix-AssemblyResolver.patch`: xbuild
  ignored `MONO_GAC_PREFIX` and only looked in `<prefix>/lib/mono/gac`), the
  `<runpath>` assembly-config extension, and, added later, an mcs fix that
  emits the `InAttribute` modreq for `ref readonly` returns, so that
  mcs-built mscorlib works for Roslyn 3.8.

## Compiler patches cherry-picked between versions

- `mono-mcs-patches-from-5.10.0.patch` (pre-5.10.0): 4218 lines, 90 files,
  almost all of them `mcs/mcs/*.cs` plus matching `mcs/tests`.
- `mono-5.10.0-later-mcs-changes.patch` (5.10.0): 4601 lines, 97 files.
  This is every later commit touching `mcs/mcs`, up to 6.12.0.
- `mono-6.12.0-emit-ref-readonly-return-modreq.patch` (mono-bootstrap,
  2026). It is a new mcs feature, not a cherry-pick: Roslyn 3.8 needs
  correct `modreq(InAttribute)` on `ref readonly` returns in the
  mcs-compiled mscorlib.

The blog suggests (but Guix does not implement) using the same trick earlier
in the chain, or porting mcs to pnet, to shorten the chain.

## Bundled binaries and how they are eliminated

`prepare-mono-source-0`, used by every Mono stage, does the following:

- deletes `configure`, `libgc/`, and every file matching
  `\.(dll|exe|DLL|EXE|so)$` anywhere in the tree;
- drops `docs` from `SUBDIRS`, because `docs/AgilityPack.dll` is a blob;
- runs after the `external/*` submodules have been copied in, so blobs in
  submodules are deleted as well.

`prepare-mono-source` adds source fixes for modern GCC/glibc to
`prepare-mono-source-0`. It is used for 1.2.6 … 4.9.0. From 5.0.1 on, only
`-0` is used.

Upstream blob submodules and how Guix handles them:

| Submodule | First seen | Guix handling |
|---|---|---|
| `mcs/class/lib/monolite/*` (tarball) | 1.2.6 | deleted |
| `binary-reference-assemblies` | 4.9.0 | **4.9.0:** not fetched (commented out). `use-old-mono-libraries` rewrites `mcs/class/reference-assemblies/Makefile` to copy the *previous Mono's* `lib/mono/<ver>` instead. **≥5.0.1:** fetched (it now contains sources), DLLs deleted by the snippet, and rebuilt from source in phase `build-reference-assemblies` with the freshly built `mcs` (5.0–5.8: after `build`, using `mcs/class/lib/build/mcs.exe`; 5.10/6.12: before `build`, with the input Mono's `mcs`). It needs extra `*_REFS` added by trial and error, plus `-delaysign+`. |
| `roslyn-binaries` | added upstream 2016-11-28; 5.0.1+ | never fetched (commented out in every spec list). `--with-csc=mcs`, and phase `disable-roslyn-install` makes `mcs/packages/Makefile` `install-local` a no-op. Later replaced by Guix's own roslyn-3.9 in `mono`. |
| `xunit-binaries` | 5.1.0+ | never fetched. Tests are disabled (`#:tests? #f`, inherited by all stages including mono-bootstrap; mono-bootstrap's `check` would run only `mono/mini` and `mono/tests`, like Debian). |
| `helix-binaries` | 6.12.0 | fetched, but its blobs are deleted by the snippet and `mono-helix-client` is removed from `mcs/tools/Makefile` (`disable-helix-client`). |
| `bdwgc` (Unity fork), `llvm-project` | 6.12.0 | not fetched (commented out). |
| `boringssl`, `corefx`, `corert`, `cecil`, `linker`, `ikvm`, `rx`, `Newtonsoft.Json`, `nuget-buildtasks`, `nunit-lite`, `api-doc-tools`, `api-snapshot`, `aspnetwebstack`, `bockbuild`, `ikdasm`, `Lucene.Net(.Light)`, `entityframework`, `buildtools`, `illinker-test-assets` | various | fetched from `https://github.com/mono/<name>` at the pinned commit (an explicit list per version, because recursive clones of `git://` URLs fail). Any blobs inside them are deleted by the snippet. |

## xbuild, MONO_GAC_PREFIX and runpath (6.12.0)

- `mono-6.12.0-fix-AssemblyResolver.patch` touches
  `Microsoft.Build.Tasks/AssemblyResolver.cs`, `corlib/System/Environment.cs`,
  `metadata/assembly.[ch]` and icall tables. It adds an icall exposing the
  runtime's GAC search list, so that xbuild's resolver honours
  `MONO_GAC_PREFIX`, not just `<prefix>/lib/mono/gac`.
- `mono-6.12.0-fix-ConditionParser.patch` fixes a bug in the xbuild condition
  parser (`Microsoft.Build.BuildEngine/ConditionParser.cs`).
- `mono-6.12.0-add-runpath.patch` adds a `<runpath path="..."/>` element to
  `*.exe.config`/`*.dll.config`, which works like an ELF RUNPATH for assembly
  probing. Guix's msbuild uses it in `MSBuild.exe.config`.
- Phase `configure-external-libs` writes `<dllmap>` config files for
  `System.Data` (unixodbc), `System.Drawing` (libgdiplus) and
  `System.Windows.Forms` (libX11). This is Linux-specific.

## Build-system details that recur

- `#:parallel-build? #f` from 1.2.6 onward ("fails nondeterministically").
  mono-bootstrap strips it.
- `NO_SIGN_ASSEMBLY=yes`, delay-signing, deletion of `.mdb` files,
  `SOURCE_DATE_EPOCH=315532800`, and the `disable-mono-mini-timestamps`
  phase are all there for reproducibility.
- The CFLAGS escape hatches for GCC 14 (`-Wno-error=implicit-function-declaration`,
  `incompatible-pointer-types`, `implicit-int`, `return-mismatch`,
  `int-conversion`) were added on 2025-07-20 by commits 60d8e77f38a8 (1.2.6),
  f47993f0c702 (1.9.1), 0f9ddc4c75bb (2.4.2.3), 29182f4babd9 (3.12.1),
  7314d9c18487 (4.9.0), c9f1d2315f99 (6.12.0) and 5dc7e7a75dbc (pnet).
- `TZ=""` from 3.12.1 on. 4.9.0 adds `tzdata-for-tests`, and the
  `patch-sgen-linking` phase adds `libmonosgen-static.la` to
  `tools/monograph`.
- From 5.0.1: `SKIP_AOT=1`, and `cmake-minimal` as a native-input (for
  boringssl).
- From 5.1.0: `resx2sr` is built and installed with a wrapper (needed later
  for MSBuild and SRM/SCI). 5.10.0 adds python.

## Guix patch inventory (file → adding commit)

```
corefx-mono-5.4.0-patches.patch                      e87bd7473a
corefx-mono-pre-5.8.0-patches.patch                  b0ef15d7f4
mono-1.2.6-bootstrap.patch                           7d96fa640a
mono-1.2.6-callsite-bound.patch                      da0de42721 (wine-mono/mono PR 10; 2008 upstream fix)
mono-1.9.1-add-MONO_CREATE_IMAGE_VERSION.patch       893bd4a8ef
mono-1.9.1-fixes.patch                               893bd4a8ef
mono-1.9.1-reproducibility.patch                     4f10384b54 (later 69d8d749e1)
mono-2.4.2.3-fixes.patch                             436648994e
mono-2.4.2.3-fix-parallel-builds.patch               fd256bb786
mono-2.4.2.3-reproducibility.patch                   fd256bb786 (later f0b8657c42)
mono-2.6.4-fixes.patch                               ff9771114d
mono-2.11.4-fixes.patch                              60314dbf81
mono-4.9.0-fix-runtimemetadataversion.patch          011759c1cd
mono-5.4.0-patches.patch                             e87bd7473a
mono-5.8.0-patches.patch                             27d7d879b7
mono-mcs-patches-from-5.10.0.patch                   bdfdd356bf
mono-5.10.0-later-mcs-changes.patch                  8e8653fcfb
mono-6.12.0-add-runpath.patch                        84df0c4f39 (later d0aaef326b)
mono-6.12.0-fix-AssemblyResolver.patch               84df0c4f39
mono-6.12.0-fix-ConditionParser.patch                84df0c4f39
mono-6.12.0-emit-ref-readonly-return-modreq.patch    b6fc762d46
mono-msbuild-15.7.179-fix-build.patch                5239ec21fd
mono-msbuild-15.7.179-fix-resources.patch            5239ec21fd
pnet-fix-line-number-info.patch                      1b1e9917ad
pnet-fix-off-by-one.patch                            1b1e9917ad
pnet-newer-libgc-fix.patch                           1b1e9917ad
pnet-newer-texinfo-fix.patch                         1b1e9917ad
roslyn-2.0.0-bootstrap-with-mono.patch               0648b5020f
roslyn-2.3.0-default-literal-for-csc-2.0.patch       a0a9240d0a
roslyn-2.8.2-csharp-7.2-for-csc-2.3.patch            ce5aae4199
roslyn-3.0.0-bootstrap-with-csc-2.8.patch            d71affea0c
roslyn-3.2.0-bootstrap-with-csc-3.0.patch            a883d8d02b
roslyn-3.8.0-bootstrap-with-csc-3.2.patch            1e78c108e5
roslyn-3.9.0-bootstrap-with-csc-3.8.patch            8bd02af834
```

The machine-readable graph is in `docs/graph.json`, with a Graphviz version
in `docs/graph.dot`.

## Dependency graph (text)

```
gcc, bison, flex, autotools, libgc, libffi, glib, zlib   (source-built distro packages)
 └─ treecc 0.3.10 ─┐
                   └─ pnet-git (cscc + ilrun, C) ── pnetlib-git (C#, by cscc)
                        └─ mono 1.2.6  [EXTERNAL_MCS=cscc, EXTERNAL_RUNTIME=ilrun, boehm]
                            └─ 1.9.1 [MONO_CREATE_IMAGE_VERSION=v1.1.4322]
                                └─ 2.4.2.3 └─ 2.6.4 └─ 2.11.4 (+submodules) └─ 3.0.12 └─ 3.12.1
                                    └─ 4.9.0 [sgen, --with-csc=mcs, ref-asm = prev mono libs]
                                        └─ 5.0.1 [ref-asm rebuilt from source] └─ 5.1.0 (resx2sr)
                                            └─ 5.2.0 └─ 5.4.0.212 └─ pre-5.8.0 └─ 5.8.0
                                                └─ pre-5.10.0 (+mcs from 5.10) └─ 5.10.0 (+mcs up to 6.12)
                                                    └─ mono-bootstrap 6.12.0.206
                                                         ├─ SCI-bootstrap ── SRM-bootstrap
                                                         ├─ msbuild 15.7.179 (mcs; needs SCI, SRM)
                                                         └─ roslyn 2.0 (mcs) ─ 2.3 ─ 2.8.2 ─ 3.0 ─ 3.2 ─ 3.8 ─ 3.9
                                                              └─ mono (6.12 + roslyn-3.9 csc)
```
