"""Restore fresh consumers from the private source9 feed and run real operations."""
from pathlib import Path
import hashlib
import json
import os
import subprocess
import sys
import xml.etree.ElementTree as E
import zipfile

root, out, check = (Path(p).resolve() for p in sys.argv[1:])
assert not check.exists(), check
check.mkdir(parents=True)
here = Path(__file__).resolve().parent
native_root = subprocess.check_output(['cygpath','-am',str(root)],text=True).strip()
def native(path):return native_root+'/'+Path(path).resolve().relative_to(root).as_posix()
source = json.loads((out/'source-inputs.json').read_text())
for name,record in source['projects'].items():
    for path,expected in record['original_cs'].items():
        assert hashlib.sha256((out/'runtime-source'/path).read_bytes()).hexdigest()==expected,(name,path)
    for path,expected in record['resource_files_sha256'].items():
        assert hashlib.sha256((out/'runtime-source'/path).read_bytes()).hexdigest()==expected,(name,path)
tree=E.Element('Project',Sdk='Microsoft.NET.Sdk');pg=E.SubElement(tree,'PropertyGroup')
for k,v in dict(TargetFramework='net8.0',OutputType='Exe',UseAppHost='false',EnableDefaultItems='false',
                EnableNETAnalyzers='false',NuGetAudit='false',Nullable='enable').items():E.SubElement(pg,k).text=v
ig=E.SubElement(tree,'ItemGroup');E.SubElement(ig,'Compile',Include=native(here/'inputs/Runtime9Checks.cs'))
for name in source['projects']:E.SubElement(ig,'PackageReference',Include=name,Version='[9.0.0]')
E.ElementTree(tree).write(check/'Check.csproj',encoding='utf-8',xml_declaration=True)
env=dict(os.environ,DOTNET_CLI_HOME=native(check/'cli-home'),NUGET_PACKAGES=native(check/'cache'),DOTNET_CLI_TELEMETRY_OPTOUT='1',MSBuildEnableWorkloadResolver='false')
dotnet='/clang64/lib/dotnet/dotnet.exe'
with (check/'build.log').open('w') as log:
    subprocess.run([dotnet,'build',native(check/'Check.csproj'),'-c','Release','--configfile',native(out/'NuGet.config'),'-p:UseSharedCompilation=false','-v:minimal'],env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
binary=check/'bin/Release/net8.0'
packages={}
for name in source['projects']:
    package=out/'feed'/(name+'.9.0.0.nupkg')
    assert (check/'cache'/name.lower()/'9.0.0'/(name.lower()+'.9.0.0.nupkg')).read_bytes()==package.read_bytes()
    with zipfile.ZipFile(package) as z:
        for path in z.namelist():
            if path.startswith('lib/net8.0/') and path.endswith('.dll'):
                assert (binary/path.removeprefix('lib/net8.0/')).read_bytes()==z.read(path),(name,path)
    packages[name]=hashlib.sha256(package.read_bytes()).hexdigest()
with (check/'run.log').open('w') as log:
    subprocess.run([dotnet,native(binary/'Check.dll'),native(out/'localization.json')],env=env,stdout=log,stderr=subprocess.STDOUT,timeout=120,check=True)
(check/'results.json').write_text(json.dumps(dict(all_eight_source_package_and_resource_asset_bytes_match=True,
    real_native_runtime_checks_exit_code=0,source_package_sha256=packages,production_cs_changes=0,
    source_roslyn414_compiled=False,full_dependency_goal_complete=False),indent=2)+'\n')
print('PASS eight exact source9 installed-feed-style consumers, resource assets and real runtime checks')
