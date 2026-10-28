"""Record the Windows PowerShell shipping/build/module dependency declarations.

This deliberately includes conditional project references, including GraphicalHost,
so a minimal console configuration cannot hide dependencies from the work list.
It records declared versions; it does not claim that an installed bootstrap
assembly is a compatible or serviced replacement.
"""
import argparse
import hashlib
import json
from pathlib import Path
import xml.etree.ElementTree as ET

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("source", type=Path)
parser.add_argument("--version", required=True)
parser.add_argument("--archive", type=Path, required=True)
args = parser.parse_args()
root = args.source.resolve()
pending = [
    "src/powershell-win-core/powershell-win-core.csproj",
    "src/Modules/PSGalleryModules.csproj",
    "src/Microsoft.PowerShell.LocalAccounts/Microsoft.PowerShell.LocalAccounts.csproj",
    "src/ResGen/ResGen.csproj",
    "src/TypeCatalogGen/TypeCatalogGen.csproj",
    "Analyzers.props",
]
seen = set()
packages = {}
projects = []
while pending:
    relative = pending.pop()
    if relative in seen:
        continue
    seen.add(relative)
    path = root / relative
    if not path.is_file():
        raise SystemExit(f"Missing declared project: {relative}")
    tree = ET.parse(path)
    projects.append(relative)
    for reference in tree.iter("PackageReference"):
        package = reference.get("Include")
        version = reference.get("Version") or reference.findtext("Version")
        if not package or not version:
            raise SystemExit(f"Unresolved package reference in {relative}")
        private = reference.get("PrivateAssets") or reference.findtext("PrivateAssets")
        kind = "build" if private and private.lower() == "all" else "runtime"
        if relative.startswith("src/Modules/"):
            kind = "module"
        if relative == "Analyzers.props":
            kind = "build-optional-analyzer"
        item = packages.setdefault(package, {"id": package, "requirements": []})
        item["requirements"].append({"version": version, "project": relative, "kind": kind})
    for reference in tree.iter("ProjectReference"):
        target = reference.get("Include").replace("\\", "/")
        if "$" in target:
            raise SystemExit(f"Unresolved project reference in {relative}: {target}")
        pending.append(str((path.parent / target).resolve().relative_to(root)))

print(json.dumps({
    "powershell_version": args.version,
    "source_url": f"https://github.com/PowerShell/PowerShell/tree/v{args.version}",
    "source_archive_sha256": hashlib.sha256(args.archive.read_bytes()).hexdigest(),
    "scope": "Windows shipping project, conditional desktop graph, bundled modules, generators and analyzers",
    "projects": sorted(projects),
    "packages": [packages[key] for key in sorted(packages)],
}, indent=2))
