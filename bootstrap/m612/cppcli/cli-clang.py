#!/usr/bin/env python3
"""Token adapter -> actual Clang AST plugin -> IL -> source-built ilasm."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys


def run(command):
    subprocess.run([str(x) for x in command], check=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("input", type=Path)
    ap.add_argument("-o", required=True, type=Path)
    ap.add_argument("-I", action="append", default=[])
    ap.add_argument("-D", action="append", default=[])
    ap.add_argument("--reference", action="append", default=[], type=Path)
    ap.add_argument("--type", action="append", default=[])
    ap.add_argument("--emit", choices=["il", "assembly"], default="assembly")
    ap.add_argument("--clang", default=os.environ.get("CPPCLI_CLANG", "clang++"))
    ap.add_argument("--sdk", type=Path, default=os.environ.get("CPPCLI_SDK"))
    ap.add_argument("--ilasm", type=Path, default=os.environ.get("CPPCLI_ILASM"))
    ap.add_argument("--plugin", type=Path)
    ap.add_argument("--metadata-tool", type=Path)
    a = ap.parse_args()
    version = subprocess.check_output([a.clang, "--version"], text=True)
    if "clang version 22.1.8" not in version:
        ap.error("the frontend plugin requires Clang 22.1.8")
    build = Path(__file__).resolve().parent
    plugin = a.plugin or build / ("cli_ast.dll" if os.name == "nt" else "cli_ast.so")
    metadata_tool = a.metadata_tool or build / "metadata/metadata.dll"
    if not a.sdk:
        ap.error("--sdk or CPPCLI_SDK must identify the source-built SDK")
    sdk = a.sdk.resolve()
    dotnet = sdk / ("dotnet.exe" if os.name == "nt" else "dotnet")
    packs = sorted((sdk / "packs/Microsoft.NETCore.App.Ref").glob("*/ref/net8.0"))
    if not packs:
        ap.error("source-built SDK lacks the net8.0 reference pack")
    references = [packs[-1] / n for n in ["System.Runtime.dll", "System.Console.dll"]]
    references += [r.resolve() for r in a.reference]
    types = sorted(set(["System.Object", "System.String", "System.Console"] + a.type))
    source = a.input.resolve()
    output = a.o.resolve()
    if source == output:
        ap.error("input and output must differ")
    work = output.parent / (output.name + ".work")
    work.mkdir(parents=True, exist_ok=True)
    manifest, header = work / "metadata.json", work / "metadata.hpp"
    run([dotnet, metadata_tool.resolve(), manifest, header, ";".join(types), *references])
    adapted, il = work / "adapted.cpp", work / "assembly.il"
    # LLVM's in-process crash recovery treats MinGW C++ exceptions raised by a
    # plugin as crashes before its local catch handler can run. Use a dedicated
    # cc1 process so unsupported input produces normal diagnostics.
    common = [a.clang, "-fno-integrated-cc1", "-std=c++17", "-Werror=return-type", "-x", "c++", "-fsyntax-only", "-I", source.parent]
    for inc in a.I:
        common += ["-I", str(Path(inc).resolve())]
    for define in a.D:
        common += ["-D", define]
    def action(name, input_path, arguments, include_header=False):
        command = common + ["-Xclang", "-load", "-Xclang", plugin.resolve(),
                            "-Xclang", "-plugin", "-Xclang", name]
        for arg in arguments:
            command += ["-Xclang", "-plugin-arg-" + name, "-Xclang", arg]
        if include_header:
            command += ["-include", header]
        command += [input_path]
        run(command)
    # Never suppress Clang diagnostics to make a surrogate parse.
    action("cli-adapt", source, ["metadata=" + str(manifest), "output=" + str(adapted)])
    action("cli-emit", adapted, ["metadata=" + str(manifest), "output=" + str(il),
                                 "evidence=" + str(work / "ast.json")], True)
    if a.emit == "il":
        output.write_bytes(il.read_bytes())
    else:
        if not a.ilasm:
            ap.error("--ilasm or CPPCLI_ILASM must identify source-built ilasm")
        temporary = work / "result.dll"
        run([a.ilasm.resolve(), il, "-exe", "-quiet", "-nologo", "-output=" + str(temporary)])
        temporary.replace(output)
        output.with_suffix(".runtimeconfig.json").write_text(json.dumps({"runtimeOptions": {
            "tfm": "net8.0", "framework": {"name": "Microsoft.NETCore.App", "version": "8.0.0"}
        }}) + "\n", encoding="utf-8")
    print("cli-clang: emitted " + str(output))


if __name__ == "__main__":
    try:
        main()
    except (OSError, subprocess.CalledProcessError) as e:
        print("cli-clang: " + str(e), file=sys.stderr)
        sys.exit(1)
