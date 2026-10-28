# CoreLib 10 source bootstrap

The newer original ILLink checkpoint supersedes the helper-only execution
checkpoint below: [`../runtime10-illink/README.md`](../runtime10-illink/README.md).
The complete original library rewrite now preserves the public surface and
resources, rewrites symbols, and passes all four managed probes without the
fcall-RVA helper. The commands below retain the earlier raw-build checkpoint.

The private runtime v10.0.5 bootstrap now executes managed code using the
matching original CoreLib sources. Four checks pass: runtime identity and
basic JIT/generic/Unicode work, compacting GC, exceptions/finally and managed
thread creation/interlocked/join. This is not a complete runtime or SDK package.

The original Windows CoreLib project selects 1,932 unchanged C# files with
COM, IJW and tracing enabled. MSBuild evaluates a private project copy with
only input paths rebased. Four original generator projects retain all their
source inputs (96/23/46/9 C# files), including the new runtime event-source
generator. The private generator bootstrap uses source Roslyn5 and existing
source Workspaces4.9.2 on net8.0. The declared Roslyn4.14 package closure
remains open; these private bindings do not replace its shipping packages.

CoreLib itself targets net10.0 with no framework assembly references. The
direct compiler arguments preserve its original Windows definitions and the
SDK10 implicit framework symbols. The original runtime-root task produces
363 type entries. Eight generated C# outputs include interop, event sources
and the source10.0.5 product version. All 1,421 resource values match the
original resx. Metadata has identity10.0.0.0/7cec85d7bea7798e, 2,957 types,
43,227 methods and four embedded resource payloads.

Start from the prepared native runtime root described in
`../runtime10-coreclr/README.md`, with the successful private source compiler
under `cdac-bootstrap5/compiler`. Copy this workflow to `corelib-workflow`
outside the original source tree, then run from the private build root:

```sh
/usr/bin/python3 corelib-workflow/prepare-generators.py .
bash corelib-workflow/build-generators.sh .
/usr/bin/python3 corelib-workflow/prepare-corelib.py .
/usr/bin/python3 corelib-workflow/prepare-compile.py .
bash corelib-workflow/build-corelib.sh .
bash corelib-workflow/build-corerun.sh .
/usr/bin/python3 corelib-workflow/prepare-managed-check.py . corelib10-1/managed-check2
bash corelib-workflow/build-managed-check.sh . corelib10-1/managed-check2
bash corelib-workflow/check-metadata.sh . corelib10-1/managed-check2
/usr/bin/python3 corelib-workflow/audit-build.py .
```

The corrected workflow passed a fresh managed-output rebuild, producing a
byte-identical raw CoreLib to the tested input. The raw compile records 549
warning lines and no C# production patches. The tested copy has one
InternalCall RVA cleared by the existing source bootstrap helper; all 215
InternalCalls then have no body RVA. This earlier checkpoint preceded the
original ILLink rewrite, now verified by the newer workflow linked above.

The original corerun host needs a two-file MinGW port. Its original
self-tests, AMD64 image and actual CET flag pass. Native MSYS2 Python reports
`os.name == "nt"` while using `/` separators. The runner normalizes Windows
path arguments to backslashes, as required by the binder. Private diagnostics
used to locate the initial binding failure were restored, rebuilt and checked
against all original native-port hashes before the successful managed probes.

Exact commands and evidence are in `managed-results-20261027.{json,txt}`.
The private artifacts and detailed logs remain under
`/c/codex-dotnet10/c1.1BZ8KU` on the Windows builder. No runtime10 package is
globally installed. The full managed/native framework, reference packs,
SDK10.0.201, WindowsDesktop10, PowerShell7.6 and
the previously recorded compatibility scenarios remain incomplete.

The public reference-contract graph now also builds and passes independent
metadata/source audits and a newer-API compile check:
[`../runtime10-reference/README.md`](../runtime10-reference/README.md).
The complete targeting pack, including its original generator bundle, is
still open.
