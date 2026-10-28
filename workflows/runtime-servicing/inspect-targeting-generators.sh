#!/usr/bin/env bash
set -euo pipefail
root=$(cd "${1:?usage: inspect-targeting-generators.sh PREPARED_ROOT INSPECTOR_CS}" && pwd)
inspector=$(realpath "${2:?missing metadata inspector source}")
out="$root/targeting-generators"
mkdir -p "$out/metadata-tool"
/usr/bin/python3 - "$out" "$inspector" <<'PY'
from pathlib import Path
import subprocess,sys,xml.etree.ElementTree as E
out=Path(sys.argv[1]);native=lambda p:subprocess.check_output(['cygpath','-am',str(p)],text=True).strip()
project=E.Element('Project',Sdk='Microsoft.NET.Sdk');props=E.SubElement(project,'PropertyGroup')
for name,value in dict(TargetFramework='net8.0',OutputType='Exe',UseAppHost='false',EnableDefaultItems='false',EnableNETAnalyzers='false',NuGetAudit='false').items():E.SubElement(props,name).text=value
E.SubElement(E.SubElement(project,'ItemGroup'),'Compile',Include=native(Path(sys.argv[2])))
E.ElementTree(project).write(out/'metadata-tool/Inspect.csproj',encoding='utf-8',xml_declaration=True)
PY
export DOTNET_ROOT=$(cygpath -am /clang64/lib/dotnet) DOTNET_ROOT_X64=$(cygpath -am /clang64/lib/dotnet)
export DOTNET_CLI_TELEMETRY_OPTOUT=1 MSBuildEnableWorkloadResolver=false
export DOTNET_CLI_HOME=$(cygpath -am "$out/cli-home") NUGET_PACKAGES=$(cygpath -am "$out/cache")
/clang64/lib/dotnet/dotnet.exe build "$(cygpath -am "$out/metadata-tool/Inspect.csproj")" -c Release --configfile "$(cygpath -am "$out/NuGet.config")" -p:UseSharedCompilation=false -v:minimal
/usr/bin/python3 - "$out" <<'PY'
from pathlib import Path
import json,subprocess,sys
out=Path(sys.argv[1]);native=lambda p:subprocess.check_output(['cygpath','-am',str(p)],text=True).strip();records=json.loads((out/'source-inputs.json').read_text())
args=['/clang64/lib/dotnet/dotnet.exe',native(out/'metadata-tool/bin/Release/net8.0/Inspect.dll')]
for name,item in records.items():
 root=out/name/'bin/Release/net8.0';args.append(native(root/(item['assembly']+'.dll')))
 args.extend(native(root/culture/(item['assembly']+'.resources.dll')) for culture in item['translated_strings'])
with (out/'pe-metadata.json').open('w') as log:subprocess.run(args,stdout=log,check=True)
PY
