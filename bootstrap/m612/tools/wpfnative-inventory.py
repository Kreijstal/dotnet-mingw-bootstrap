#!/usr/bin/env python3
"""Record PresentationNative's source-visible ABI and implemented PE exports."""
import argparse
import json
from pathlib import Path
import re
import subprocess


def read_source(path):
    raw = path.read_bytes()
    if raw.startswith((b"\xff\xfe", b"\xfe\xff")):
        return raw.decode("utf-16")
    try:
        return raw.decode("utf-8-sig")
    except UnicodeDecodeError:
        return raw.decode("cp1252")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("native", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--require-complete", action="store_true")
    args = parser.parse_args()
    root = args.source / "src/Microsoft.DotNet.Wpf/src"
    entries = {}
    pattern = re.compile(r"\[DllImport\(([^\]]+)\)\]\s*(?:\[[^\]]+\]\s*)*"
                         r"(?:public|private|internal|protected|static|extern|unsafe|\s)+[^;]+;")
    for file in root.rglob("*.cs"):
        for match in pattern.finditer(read_source(file)):
            attributes = match.group(1)
            if "presentationnative" not in attributes.lower():
                continue
            name = re.search(r'EntryPoint\s*=\s*"([^\"]+)"', attributes)
            signature = match.group(0).split("extern", 1)[-1].strip()
            entry = name.group(1) if name else re.search(r"(\w+)\s*\(", signature).group(1)
            location = str(file.relative_to(root)).replace("\\", "/")
            if location.startswith("Shared/"):
                category = "Win32 and OS version helpers"
            elif "/PtsHost/" in location:
                category = "paragraph layout"
            elif "/TextFormatting/" in location or location.endswith("Classification.cs"):
                category = "text analysis, formatting and Unicode classification"
            else:
                category = "natural language services"
            item = entries.setdefault(entry, {"category": category, "sources": []})
            if location not in item["sources"]:
                item["sources"].append(location)
    result = subprocess.check_output(["objdump", "-p", str(args.native)], text=True)
    exports = set(re.findall(r"^\s+\[\s*\d+\]\s+(\w+)\s*$", result, re.M))
    exports.update(re.findall(r"^\s+\d+\s+0x[0-9a-fA-F]+\s+(\w+)\s*$", result, re.M))
    if not exports:
        raise SystemExit("Could not read native PE exports")
    missing = sorted(set(entries) - exports)
    report = {
        "source": "dotnet/wpf v8.0.0", "component": "PresentationNative_cor3.dll",
        "upstreamDelivery": "binary redistributable from Microsoft.DotNet.Wpf.DncEng",
        "scope": "C# DllImport declarations naming PresentationNative; dynamic lookups and native callers need a separate audit",
        "requiredExports": {name: dict(item, implemented=name in exports) for name, item in sorted(entries.items())},
        "providedExports": sorted(exports), "missingExports": missing,
        "status": "complete imported symbol coverage" if not missing else "partial source native port; complete text/paragraph layout unavailable",
        "otherBlockers": ["complete bidi/optimal/text/paragraph/natural-language behavior",
                          "full runtime/path coverage beyond the installed experimental WindowsDesktop package"],
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n")
    print(f"PresentationNative: {len(entries)} required, {len(set(entries) & exports)} implemented, {len(missing)} missing exports")
    if args.require_complete and missing:
        raise SystemExit("Source-only WPF rendering is incomplete; see " + str(args.output))


if __name__ == "__main__":
    main()
