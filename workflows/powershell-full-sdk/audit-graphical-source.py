"""Audit original GraphicalHost C#/XAML/resource inputs and every compiled BAML."""
from pathlib import Path
import hashlib,json,os,subprocess,sys,tarfile,xml.etree.ElementTree as E
root,out=(Path(p).resolve() for p in sys.argv[1:]);out.mkdir()
source=root/'PowerShell-7.4.20';folder='src/Microsoft.Management.UI.Internal';project=folder+'/Microsoft.PowerShell.GraphicalHost.csproj'
sha=lambda data:hashlib.sha256(data).hexdigest()
archive=root/'powershell-7.4.20.tar.gz';assert sha(archive.read_bytes())=='3dac352752acd64a0502c1f002ecf65eca177d2919bea3bcefbf8856afedd9eb'
with tarfile.open(archive) as tar:
 original={p.name.removeprefix('PowerShell-7.4.20/'):tar.extractfile(p).read() for p in tar if p.isfile() and p.name.startswith('PowerShell-7.4.20/'+folder+'/')}
assert (source/project).read_bytes()==original[project]
tree=E.fromstring(original[project]);assert tree.findtext('.//RunAnalyzers')=='false'
native=lambda p:subprocess.check_output(['cygpath','-am',str(p)],text=True).strip().replace('\\','/')
prefix=native(source)+'/'
selected=[]
for line in (root/'manifests/Microsoft.PowerShell.GraphicalHost-compile.txt').read_text(encoding='utf-8-sig').splitlines():
 line=line.replace('\\','/');assert line.lower().startswith(prefix.lower());selected.append(line[len(prefix):])
wanted={p for p in original if p.endswith('.cs')};actual={p for p in selected if p in original};assert wanted==actual,(wanted-actual,actual-wanted)
pages={p for p in original if p.endswith('.xaml')}
resources={folder+'/'+p.get('Include').replace('\\','/') for p in tree.findall('.//Resource')}
for p in wanted|pages|resources:assert (source/p).read_bytes()==original[p],p
here=Path(__file__).resolve().parent
(out/'Program.cs').write_bytes((here/'InspectGraphicalAssets.cs').read_bytes())
(out/'Inspect.csproj').write_text('<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><UseAppHost>false</UseAppHost><EnableDefaultItems>false</EnableDefaultItems><NuGetAudit>false</NuGetAudit></PropertyGroup><ItemGroup><Compile Include="Program.cs"/></ItemGroup></Project>')
(out/'NuGet.config').write_text('<configuration><packageSources><clear/></packageSources></configuration>')
env=dict(os.environ,DOTNET_ROOT=native(Path('/clang64/lib/dotnet')),DOTNET_ROOT_X64=native(Path('/clang64/lib/dotnet')),DOTNET_CLI_HOME=native(out/'cli-home'),NUGET_PACKAGES=native(out/'cache'),DOTNET_CLI_TELEMETRY_OPTOUT='1',DOTNET_NOLOGO='1',MSBuildEnableWorkloadResolver='false')
dotnet='/clang64/lib/dotnet/dotnet.exe'
subprocess.run([dotnet,'build',native(out/'Inspect.csproj'),'-c','Release','--configfile',native(out/'NuGet.config'),'-p:UseSharedCompilation=false','-v:minimal'],env=env,check=True)
dll=source/folder/'bin/Release/net8.0/Microsoft.PowerShell.GraphicalHost.dll'
assets=json.loads(subprocess.check_output([dotnet,native(out/'bin/Release/net8.0/Inspect.dll'),native(dll)],env=env,text=True))
expected_baml={p[len(folder)+1:-5].lower()+'.baml' for p in pages}
assert {p for p in assets if p.endswith('.baml')}==expected_baml
expected_resources={p[len(folder)+1:].lower():p for p in resources}
assert set(assets)==expected_baml|set(expected_resources),(set(assets)-expected_baml-set(expected_resources))
for name,p in expected_resources.items():assert assets[name]['sha256']==sha(original[p]),name
for name in expected_baml:assert assets[name]['length']>16,name
report=dict(original_source_cs=len(wanted),original_xaml_pages=len(pages),compiled_baml_entries=len(expected_baml),explicit_original_resource_assets=len(resources),original_project_sha256=sha(original[project]),original_upstream_run_analyzers_false_retained=True,all_original_cs_xaml_resource_sha256={p:sha(original[p]) for p in sorted(wanted|pages|resources)},compiler_generated_paths=[p for p in selected if p not in original],compiled_assets=assets,assembly_sha256=sha(dll.read_bytes()),full_desktop_compatibility_complete=False)
(root/'graphicalhost-source-audit.json').write_text(json.dumps(report,indent=2)+'\n')
print('PASS original GraphicalHost:',len(wanted),'C# inputs,',len(pages),'XAML/compiled BAML entries and',len(resources),'byte-exact original resources; upstream project/analyzer setting retained')
