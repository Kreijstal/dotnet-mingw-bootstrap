#!/usr/bin/env python3
"""Token adapter -> actual Clang AST plugin -> IL -> source-built ilasm."""
import argparse
import json
import os
import re
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
    ap.add_argument("--target", choices=["exe", "library"], default="exe")
    ap.add_argument("--assembly-name")
    ap.add_argument("--framework", choices=["windowsforms"], help="reference the source-built Windows Forms pack and select its runtime framework")
    ap.add_argument("--native-library", default="", help="DLL supplying used extern-C functions through a split native bridge")
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
    runtime_frameworks = [{"name": "Microsoft.NETCore.App", "version": "8.0.0"}]
    reference_directories = [packs[-1]]
    if a.framework == "windowsforms":
        desktop = sorted((sdk / "packs/Microsoft.WindowsDesktop.App.Ref").glob("*/ref/net8.0"))
        if not desktop:
            ap.error("--framework windowsforms requires the source-built WinForms targeting pack")
        desktop = desktop[-1]
        desktop_version = desktop.parents[1].name
        references += sorted(desktop.glob("*.dll"))
        references += [packs[-1] / n for n in ["System.ComponentModel.Primitives.dll", "System.Drawing.Primitives.dll"]]
        types = sorted(set(types + ["System.Windows.Forms.Form", "System.Windows.Forms.ContainerControl",
            "System.Windows.Forms.ScrollableControl", "System.Windows.Forms.Control", "System.Windows.Forms.Application",
            "System.ComponentModel.Component", "System.MarshalByRefObject"]))
        runtime_frameworks.append({"name": "Microsoft.WindowsDesktop.App", "version": desktop_version})
        reference_directories.append(desktop)
    source = a.input.resolve()
    output = a.o.resolve()
    assembly_name = a.assembly_name or output.stem
    if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_.-]*", assembly_name):
        ap.error("invalid assembly name")
    if a.native_library and not re.fullmatch(r"[A-Za-z0-9_.-]+\.dll", a.native_library):
        ap.error("--native-library must be a simple DLL filename")
    if source == output:
        ap.error("input and output must differ")
    work = output.parent / (output.name + ".work")
    work.mkdir(parents=True, exist_ok=True)
    manifest, header = work / "metadata.json", work / "metadata.hpp"
    adapted, il = work / "adapted.cpp", work / "assembly.il"
    # LLVM's in-process crash recovery treats MinGW C++ exceptions raised by a
    # plugin as crashes before its local catch handler can run. Use a dedicated
    # cc1 process so unsupported input produces normal diagnostics.
    common = [a.clang, "-fno-integrated-cc1", "-std=c++17", "-fms-extensions", "-Werror=return-type", "-x", "c++", "-fsyntax-only", "-I", source.parent]
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
    shim, reference_file = work / "shim.cpp", work / "references.json"
    action("cli-shim", source, ["output=" + str(shim)])
    action("cli-references", shim, ["output=" + str(reference_file)])
    for item in json.loads(reference_file.read_text(encoding="utf-8")):
        ref = Path(item["path"])
        candidates = [ref] if ref.is_absolute() else [Path(item["source"]).parent / ref] + [directory / ref for directory in reference_directories]
        found = next((p.resolve() for p in candidates if p.is_file()), None)
        if found is None:
            ap.error("#using assembly not found in source directory or source-built pack: " + item["path"])
        references.append(found)
    references = list({os.path.normcase(str(p.resolve())): p.resolve() for p in references}.values())
    run([dotnet, metadata_tool.resolve(), manifest, header, ";".join(types), *references])
    # Never suppress Clang diagnostics to make a surrogate parse.
    action("cli-adapt", shim, ["metadata=" + str(manifest), "output=" + str(adapted)])
    action("cli-emit", adapted, ["metadata=" + str(manifest), "output=" + str(il),
                                 "evidence=" + str(work / "ast.json"),
                                 "assembly=" + assembly_name, "target=" + a.target, "native-library=" + a.native_library], True)
    if a.emit == "il":
        output.write_bytes(il.read_bytes())
    else:
        if not a.ilasm:
            ap.error("--ilasm or CPPCLI_ILASM must identify source-built ilasm")
        temporary = work / "result.dll"
        run([a.ilasm.resolve(), il, "-exe" if a.target == "exe" else "-dll", "-quiet", "-nologo", "-output=" + str(temporary)])
        temporary.replace(output)
        framework_config = {"framework": runtime_frameworks[0]} if len(runtime_frameworks) == 1 else {"frameworks": runtime_frameworks}
        output.with_suffix(".runtimeconfig.json").write_text(json.dumps({"runtimeOptions": {
            "tfm": "net8.0", **framework_config
        }}) + "\n", encoding="utf-8")
    print("cli-clang: emitted " + str(output))


if __name__ == "__main__":
    try:
        main()
    except (OSError, subprocess.CalledProcessError) as e:
        print("cli-clang: " + str(e), file=sys.stderr)
        sys.exit(1)
