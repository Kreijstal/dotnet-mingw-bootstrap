"""Check built package contents, local source hashes and NuGet dependency metadata.

Run from the repository root after building the six initial recipes and experimental MMI port. Downloaded
source archive hashes are verified by makepkg; this verifies recipe-local inputs.
"""
import hashlib
import io
import json
from pathlib import Path
import subprocess
import sys
import xml.etree.ElementTree as ET
import zipfile

expected = {
    "markdig": set(),
    "powershell-markdownrender": {"Markdig.Signed"},
    "humanizer": set(),
    "json-more": set(),
    "jsonpointer": {"Json.More.Net", "Humanizer.Core"},
    "jsonschema": {"Json.More.Net", "JsonPointer.Net"},
    "mmi": set(),
}
records = []
for name, dependencies in expected.items():
    root = Path("mingw-w64-dotnet-" + name)
    fields = {}
    for line in (root / ".SRCINFO").read_text().splitlines():
        key, separator, value = line.strip().partition(" = ")
        if separator:
            fields.setdefault(key, []).append(value)
    sources, hashes = fields["source"], fields["sha256sums"]
    assert len(sources) == len(hashes)
    for source, wanted in zip(sources, hashes):
        if "::" not in source:
            assert hashlib.sha256((root / source).read_bytes()).hexdigest() == wanted, (name, source)
    packages = list(root.glob("*.pkg.tar.zst"))
    assert len(packages) == 1, (name, "Expected exactly one built package", packages)
    package = packages[0]
    entries = subprocess.check_output(["tar", "--zstd", "-tf", str(package)], text=True).splitlines()
    license_root = f"clang64/share/licenses/dotnet-{name}/"
    assert license_root + "LICENSE" in entries, (name, "License missing")
    if name == "humanizer":
        assert license_root + "LICENSE.Bytes" in entries
        assert license_root + "LICENSE.Inflections" in entries
    if name == "json-more":
        assert license_root + "LICENSE.CC-BY-SA-4.0" in entries
        assert license_root + "NOTICE" in entries
    nupkgs = [entry for entry in entries if entry.endswith(".nupkg")]
    assert len(nupkgs) == 1, (name, nupkgs)
    blob = subprocess.check_output(["tar", "--zstd", "-xOf", str(package), nupkgs[0]])
    with zipfile.ZipFile(io.BytesIO(blob)) as archive:
        spec = ET.fromstring(archive.read(next(p for p in archive.namelist() if p.endswith(".nuspec"))))
        actual = {element.get("id") for element in spec.iter() if element.tag.endswith("}dependency")}
        assert actual == dependencies, (name, actual, dependencies)
        assert any(p.startswith("lib/net8.0/") and p.endswith(".dll") for p in archive.namelist())
        if name == "mmi":
            metadata = next(e for e in spec if e.tag.endswith("}metadata"))
            values = {e.tag.rsplit("}", 1)[-1]: e.text for e in metadata}
            assert values["id"] == "Microsoft.Management.Infrastructure"
            assert values["version"] == "2.0.0-preview.1"
            assert [p for p in archive.namelist() if p.endswith(".dll")] == ["lib/net8.0/Microsoft.Management.Infrastructure.dll"]
            assert "clang64/share/doc/dotnet-mmi/README.md" in entries
    records.append({
        "recipe": str(root), "artifact": package.name,
        "sha256": hashlib.sha256(package.read_bytes()).hexdigest(),
        "dependencies": sorted(actual),
    })
    print(name + ": local source checksums, license payload and NuGet dependencies PASS")
if len(sys.argv) == 2:
    Path(sys.argv[1]).write_text(json.dumps(records, indent=2) + "\n")
