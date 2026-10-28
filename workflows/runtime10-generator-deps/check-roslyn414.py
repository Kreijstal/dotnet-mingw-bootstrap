"""Verify real Roslyn4.14 language/API/resource behavior from a fresh source feed."""
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
here=Path(__file__).resolve().parent
native_root=subprocess.check_output(['cygpath','-am',str(root)],text=True).strip()
def native(path):return native_root+'/'+Path(path).resolve().relative_to(root).as_posix()
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
source=json.loads((out/'source-inputs.json').read_text())
for name,record in source['projects'].items():
    for path,expected in record['original_cs'].items():assert sha(out/'roslyn-source'/path)==expected,(name,path)
for suffix,name,language in [('common','Microsoft.CodeAnalysis','Core'),('csharp','Microsoft.CodeAnalysis.CSharp','CSharp')]:
    folder=out/'roslyn-source/src/Compilers'/language/'Portable'
    (check/('api-'+suffix+'.txt')).write_text((folder/'PublicAPI.Shipped.txt').read_text().rstrip()+'\n'+(folder/'PublicAPI.Unshipped.txt').read_text().rstrip()+'\n')
    resources=[dict(assembly=item['assembly'],resource=item['logical_name'],culture=item['culture'],strings=item['strings'])
               for item in json.loads((out/'localization.json').read_text()) if item['assembly']==name]
    (check/('localization-'+suffix+'.json')).write_text(json.dumps(resources,ensure_ascii=False,indent=2)+'\n')
tree=E.Element('Project',Sdk='Microsoft.NET.Sdk');pg=E.SubElement(tree,'PropertyGroup')
for k,v in dict(TargetFramework='net8.0',OutputType='Exe',UseAppHost='false',EnableDefaultItems='false',
                EnableNETAnalyzers='false',NuGetAudit='false',Nullable='enable',LangVersion='preview',
                CscToolPath=native(root/'cdac-bootstrap5/compiler'),CscToolExe='csc50.cmd').items():E.SubElement(pg,k).text=v
ig=E.SubElement(tree,'ItemGroup')
for file in ['Roslyn414Checks.cs','CheckApi.cs']:E.SubElement(ig,'Compile',Include=native(here/'inputs'/file))
E.SubElement(ig,'PackageReference',Include='Microsoft.CodeAnalysis.CSharp',Version='[4.14.0]')
E.ElementTree(tree).write(check/'Check.csproj',encoding='utf-8',xml_declaration=True)
env=dict(os.environ,DOTNET_CLI_HOME=native(check/'cli-home'),NUGET_PACKAGES=native(check/'cache'),DOTNET_CLI_TELEMETRY_OPTOUT='1',MSBuildEnableWorkloadResolver='false')
dotnet='/clang64/lib/dotnet/dotnet.exe'
with (check/'build.log').open('w') as log:
    subprocess.run([dotnet,'build',native(check/'Check.csproj'),'-c','Release','--configfile',native(out/'NuGet.config'),'-p:UseSharedCompilation=false','-v:minimal'],env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
binary=check/'bin/Release/net8.0';packages={}
for name in ['Microsoft.CodeAnalysis.Common','Microsoft.CodeAnalysis.CSharp']:
    package=out/'feed'/(name+'.4.14.0.nupkg')
    assert (check/'cache'/name.lower()/'4.14.0'/(name.lower()+'.4.14.0.nupkg')).read_bytes()==package.read_bytes()
    with zipfile.ZipFile(package) as z:
        for path in z.namelist():
            if path.startswith('lib/net8.0/') and path.endswith('.dll'):
                assert (binary/path.removeprefix('lib/net8.0/')).read_bytes()==z.read(path),(name,path)
    packages[name]=sha(package)
refs=subprocess.check_output(['cygpath','-am','/clang64/lib/dotnet/packs/Microsoft.NETCore.App.Ref/8.0.31/ref/net8.0'],text=True).strip()
command=[dotnet,native(binary/'Check.dll'),refs,
         *[native(check/file) for file in ['api-common.txt','api-csharp.txt','localization-common.json','localization-csharp.json']]]
with (check/'run.log').open('w') as log:
    result=subprocess.run(command,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=180)
(check/'run.exit').write_text(str(result.returncode)+'\n')
assert result.returncode==0,(result.returncode,check/'run.log')
(check/'results.json').write_text(json.dumps(dict(runtime_checks_command=command,runtime_checks_exit_code=0,
    exact_source_package_and_localized_dll_bytes_match=True,source_package_sha256=packages,
    source_workspaces414_built=False,source_roslyn44_built=False,source_targeting_generators10_complete=False,
    full_dependency_goal_complete=False),indent=2)+'\n')
print('PASS fresh source Roslyn4.14 consumer: C#13, emit/execute, compiler/analyzer/generator APIs, public API and full resources')
