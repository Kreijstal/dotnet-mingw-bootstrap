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


def native_imports(libraries, reader):
    imports = {}
    for library in libraries:
        output = subprocess.check_output([reader, str(library.resolve())], text=True)
        for block in re.split(r"(?m)^File: ", output)[1:]:
            lines = block.splitlines()
            dll = lines[0].strip()
            if "Format: COFF-import-file-x86-64" not in lines or "Type: code" not in lines or "Name type: name" not in lines:
                continue
            if not re.fullmatch(r"[A-Za-z0-9_.-]+\.(?:dll|drv)", dll, re.IGNORECASE):
                raise ValueError("invalid native import DLL: " + dll)
            exports = [line.removeprefix("Export name: ") for line in lines if line.startswith("Export name: ")]
            if len(exports) != 1:
                raise ValueError("incomplete named COFF import: " + dll)
            entry = exports[0]
            if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", entry):
                continue # Decorated/ordinal/forwarder import forms need separate support.
            for line in lines:
                if not line.startswith("Symbol: "): continue
                symbol = line.removeprefix("Symbol: ")
                if symbol.startswith("__imp_"): continue
                binding = {"library": dll, "entry": entry}
                old = imports.get(symbol)
                if old and {k: v.lower() for k, v in old.items()} != {k: v.lower() for k, v in binding.items()}:
                    raise ValueError("ambiguous native import symbol: " + symbol)
                imports[symbol] = binding
    return imports


def main():
    if os.name == "nt":
        # Redirected Windows streams otherwise use an ANSI code page and can
        # turn a successful Unicode-path compilation into an encoding error.
        sys.stdout.reconfigure(encoding="utf-8")
        sys.stderr.reconfigure(encoding="utf-8")
    arguments = sys.argv[1:]
    if arguments and arguments[0] == "--arguments-json":
        if len(arguments) != 2:
            raise ValueError("--arguments-json requires a single JSON argument-vector file")
        arguments = json.loads(Path(arguments[1]).read_text(encoding="utf-8"))
        if not isinstance(arguments, list) or not all(isinstance(value, str) for value in arguments):
            raise ValueError("JSON arguments must be an array of strings")
    ap = argparse.ArgumentParser(description=__doc__, epilog="Use --arguments-json FILE to supply a UTF-8 JSON argument vector without shell quoting or Windows command-length limits.")
    ap.add_argument("input", type=Path)
    ap.add_argument("-o", required=True, type=Path)
    ap.add_argument("-I", action="append", default=[])
    ap.add_argument("-D", action="append", default=[])
    ap.add_argument("--std", choices=["c++14", "c++17", "c++20"], default="c++17", help="C++ language mode used by Clang (default: c++17)")
    ap.add_argument("--reference", action="append", default=[], type=Path)
    ap.add_argument("--type", action="append", default=[])
    ap.add_argument("--emit", choices=["il", "assembly"], default="assembly")
    ap.add_argument("--target", choices=["exe", "library"], default="exe")
    ap.add_argument("--assembly-name")
    ap.add_argument("--public-key", type=Path, help="strong-name public-key blob for the emitted assembly identity (public signing only)")
    ap.add_argument("--framework", choices=["windowsforms"], help="reference the source-built Windows Forms pack and select its runtime framework")
    ap.add_argument("--native-library", default="", help="DLL supplying used extern-C functions through a split native bridge")
    ap.add_argument("--native-import", action="append", default=[], type=Path, help="trusted x64 MinGW import archive supplying named native DLL bindings")
    ap.add_argument("--llvm-readobj", default="llvm-readobj", help="LLVM reader used to inspect native COFF import libraries")
    ap.add_argument("--clang", default=os.environ.get("CPPCLI_CLANG", "clang++"))
    ap.add_argument("--sdk", type=Path, default=os.environ.get("CPPCLI_SDK"))
    ap.add_argument("--ilasm", type=Path, default=os.environ.get("CPPCLI_ILASM"))
    ap.add_argument("--plugin", type=Path)
    ap.add_argument("--metadata-tool", type=Path)
    a = ap.parse_args(arguments)
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
    references = [packs[-1] / n for n in ["System.Runtime.dll", "System.Console.dll", "System.Runtime.CompilerServices.VisualC.dll", "System.Runtime.InteropServices.dll"]]
    references += [r.resolve() for r in a.reference]
    references = list(dict.fromkeys(r.resolve() for r in references))
    types = sorted(set(["System.Object", "System.String", "System.Console", "System.IntPtr", "System.UIntPtr"] + a.type))
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
    public_key = a.public_key.read_bytes().hex() if a.public_key else ""
    if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_.-]*", assembly_name):
        ap.error("invalid assembly name")
    if a.native_library and not re.fullmatch(r"[A-Za-z0-9_.-]+\.dll", a.native_library):
        ap.error("--native-library must be a simple DLL filename")
    if source == output:
        ap.error("input and output must differ")
    work = output.parent / (output.name + ".work")
    work.mkdir(parents=True, exist_ok=True)
    native_manifest = work / "native-imports.json"
    native_manifest.write_text(json.dumps({"version": 1, "imports": native_imports(a.native_import, a.llvm_readobj)}, sort_keys=True), encoding="utf-8")
    manifest, header = work / "metadata.json", work / "metadata.hpp"
    adapted, il = work / "adapted.cpp", work / "assembly.il"
    # LLVM's in-process crash recovery treats MinGW C++ exceptions raised by a
    # plugin as crashes before its local catch handler can run. Use a dedicated
    # cc1 process so unsupported input produces normal diagnostics.
    common = [a.clang, "-fno-integrated-cc1", "-std=" + a.std, "-fms-extensions", "-Werror=return-type", "-x", "c++", "-fsyntax-only", "-I", source.parent, "-I", build / "include"]
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
    friends = []
    for item in json.loads(reference_file.read_text(encoding="utf-8")):
        ref = Path(item["path"])
        candidates = [ref] if ref.is_absolute() else [Path(item["source"]).parent / ref] + [directory / ref for directory in reference_directories]
        found = next((p.resolve() for p in candidates if p.is_file()), None)
        if found is None:
            ap.error("#using assembly not found in source directory or source-built pack: " + item["path"])
        references.append(found)
        if item.get("as_friend"):
            friends.append(str(found))
    references = list({os.path.normcase(str(p.resolve())): p.resolve() for p in references}.values())
    access_file = work / "access.json"
    access_file.write_text(json.dumps({"Assembly": assembly_name, "PublicKey": public_key, "Friends": friends}), encoding="utf-8")
    metadata_arguments = work / "metadata-arguments.json"
    metadata_arguments.write_text(json.dumps([str(value) for value in
        [manifest, header, ";".join(types), "--access", access_file, *references]], ensure_ascii=False), encoding="utf-8")
    run([dotnet, metadata_tool.resolve(), "--arguments-json", metadata_arguments])
    # Never suppress Clang diagnostics to make a surrogate parse.
    action("cli-adapt", shim, ["metadata=" + str(manifest), "output=" + str(adapted)])
    action("cli-emit", adapted, ["metadata=" + str(manifest), "output=" + str(il),
                                 "evidence=" + str(work / "ast.json"),
                                 "assembly=" + assembly_name, "public-key=" + public_key, "target=" + a.target, "native-library=" + a.native_library,
                                 "native-imports=" + str(native_manifest)], True)
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
    except (OSError, ValueError, subprocess.CalledProcessError) as e:
        print("cli-clang: " + str(e), file=sys.stderr)
        sys.exit(1)
