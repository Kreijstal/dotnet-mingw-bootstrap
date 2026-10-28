"""Stage the archived public MMI C# implementation for an offline .NET 8 probe."""
from pathlib import Path
import argparse
import shutil
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("source", type=Path)
parser.add_argument("output", type=Path)
parser.add_argument("--patched", action="store_true")
parser.add_argument("--public-sign", action="store_true")
args = parser.parse_args()
source, output = args.source, args.output
source = source / "src" / "Microsoft.Management.Infrastructure"
if not (source / "Native" / "MI_PlatformSpecific.cs").is_file():
    raise SystemExit("Expected the public PowerShell/MMI source tree")
if output.exists():
    raise SystemExit(f"Refusing to overwrite {output}")
shutil.copytree(source, output, ignore=shutil.ignore_patterns("bin", "obj", "*.csproj"))
(output / "Mmi.csproj").write_text('''<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup>
    <TargetFramework>net8.0</TargetFramework>
    <AssemblyName>Microsoft.Management.Infrastructure</AssemblyName>
    <RootNamespace>Microsoft.Management.Infrastructure</RootNamespace>
    <Version>2.0.0-preview.1</Version>
    <AllowUnsafeBlocks>true</AllowUnsafeBlocks>
    <DefineConstants>$(DefineConstants);_CORECLR</DefineConstants>
    <AssemblyVersion>2.0.0.0</AssemblyVersion>
    <EnableNETAnalyzers>false</EnableNETAnalyzers>
    <NuGetAudit>false</NuGetAudit>
    <NoWarn>$(NoWarn);CS0649;SYSLIB0051;SYSLIB0003</NoWarn>
  </PropertyGroup>
</Project>
''', encoding="utf-8")
shutil.copyfile(args.source / "LICENSE", output / "LICENSE")
if args.patched:
    for patch in sorted(Path(__file__).resolve().parent.glob("000*.patch")):
        subprocess.run(["patch", "--batch", "-p1", "-i", str(patch)], cwd=output, check=True)
if args.public_sign:
    key = (args.source / "signing" / "visualstudiopublic.snk").read_bytes()
    (output / "MMI.public.snk").write_bytes(key)
    info = output / "AssemblyInfo.cs"
    text = info.read_text()
    unsigned_friend = '[assembly: InternalsVisibleTo("Microsoft.Management.Infrastructure.Tests")]'
    assert text.count(unsigned_friend) == 1
    text = text.replace(unsigned_friend,
        '[assembly: InternalsVisibleTo("Microsoft.Management.Infrastructure.Tests,PublicKey=' + key.hex() + '")]')
    info.write_text(text)
    project = output / "Mmi.csproj"
    text = project.read_text().replace("</PropertyGroup>", '''    <SignAssembly>true</SignAssembly>
    <PublicSign>true</PublicSign>
    <AssemblyOriginatorKeyFile>MMI.public.snk</AssemblyOriginatorKeyFile>
  </PropertyGroup>''', 1)
    project.write_text(text)
