# msys2-ur .NET workflows

Build workflows, checks and recorded results for the .NET packages in
[msys2-ur](https://github.com/Kreijstal/msys2-ur) that build on this bootstrap:
the .NET 8 runtime/targeting servicing, the .NET 10 host and runtime work, the
SDK servicing, CIM/WMI and PowerShell. They used to live, uncommitted, in
msys2-ur's `tests/` directory.

Several recipes there build from a source archive that their `make-source.py`
generates from one of these directories (for example
`mingw-w64-dotnet-runtime8-servicing` from `runtime-servicing/`). Paths in the
recorded results and READMEs that start with `../` or `mingw-w64-` refer to
the msys2-ur repository.
