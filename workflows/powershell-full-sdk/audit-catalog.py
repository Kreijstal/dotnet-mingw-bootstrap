"""Verify the original full SDK catalog's reference closure and regeneration."""
from pathlib import Path
import hashlib,json,os,subprocess,sys,tarfile,zipfile

root=Path(sys.argv[1]).resolve();source=root/'PowerShell-7.4.20';sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
archive=root/'powershell-7.4.20.tar.gz';assert sha(archive)=='3dac352752acd64a0502c1f002ecf65eca177d2919bea3bcefbf8856afedd9eb'
originals=['src/Microsoft.PowerShell.SDK/Microsoft.PowerShell.SDK.csproj','src/TypeCatalogGen/TypeCatalogGen.cs','src/TypeCatalogGen/TypeCatalogGen.csproj']
with tarfile.open(archive) as tar:
 for name in originals:assert (source/name).read_bytes()==tar.extractfile('PowerShell-7.4.20/'+name).read(),name
native=lambda p:subprocess.check_output(['cygpath','-am',str(p)],text=True).strip().replace('\\','/')
paths=[line.strip().replace('\\','/') for line in (root/'sdk-references.inc').read_text(encoding='utf-8-sig').replace('\n','').split(';') if line.strip()]
captured=[line.split('|') for line in (root/'sdk-references.inc.metadata.txt').read_text(encoding='utf-8-sig').splitlines()]
expected=[fields[2].replace('\\','/') for fields in captured if fields[1]!='Microsoft.Management.Infrastructure']
assert paths==expected,(paths,expected)
assert len(paths)>=200 and not any(p.endswith('Microsoft.Management.Infrastructure.dll') for p in paths)
for name in ['System.Data.SqlClient','System.Management','System.ServiceModel.Http','System.ServiceModel.NetTcp','System.IO.Packaging','System.Drawing.Common','System.Speech']:
 assert any(Path(p).name==name+'.dll' for p in paths),name
cache=root/'empty-packages';cache_prefix=native(cache)+'/'
framework=Path('/clang64/lib/dotnet/packs/Microsoft.NETCore.App.Ref/8.0.31/ref/net8.0');framework_prefix=native(framework)+'/'
feed=Path('/clang64/lib/dotnet/library-packs');owners={};assets={}
packages=sorted(cache.rglob('*.nupkg'))
package_owners=subprocess.check_output(['pacman','-Qqo',*[str(feed/package.name) for package in packages]],text=True).splitlines()
assert len(package_owners)==len(packages)
framework_paths=[framework/path[len(framework_prefix):] for path in paths if path.lower().startswith(framework_prefix.lower())]
framework_owners=subprocess.check_output(['pacman','-Qqo',*[str(path) for path in framework_paths]],text=True).splitlines()
assert len(framework_paths)==len(framework_owners)
framework_owner_by_path=dict(zip(framework_paths,framework_owners))
for package,owner in zip(packages,package_owners):
 installed=feed/package.name;assert installed.is_file() and sha(installed)==sha(package),package
 owners[package.name]=dict(sha256=sha(package),native_owner=owner)
 with zipfile.ZipFile(package) as z:
  for name in z.namelist():
   if name.endswith('.dll'):assets.setdefault(hashlib.sha256(z.read(name)).hexdigest(),[]).append(dict(package=package.name,asset=name))
records={}
for path in paths:
 if path.lower().startswith(cache_prefix.lower()):
  p=cache/path[len(cache_prefix):];digest=sha(p);assert digest in assets,p;origin=dict(source_nuget_assets=assets[digest])
 else:
  assert path.lower().startswith(framework_prefix.lower()),path
  p=framework/path[len(framework_prefix):];digest=sha(p);origin=dict(source_targeting_native_owner=framework_owner_by_path[p])
 records[path]=dict(sha256=digest,**origin)
target=source/'src/System.Management.Automation/CoreCLR/CorePsTypeCatalog.cs';regenerated=root/'independent-sdk-catalog.cs'
env=dict(os.environ,DOTNET_ROOT=native(Path('/clang64/lib/dotnet')),DOTNET_ROOT_X64=native(Path('/clang64/lib/dotnet')),DOTNET_CLI_TELEMETRY_OPTOUT='1')
subprocess.run(['/clang64/lib/dotnet/dotnet.exe',native(source/'src/TypeCatalogGen/bin/Release/net8.0/TypeCatalogGen.dll'),native(regenerated),native(root/'sdk-references.inc')],env=env,check=True)
assert target.read_bytes()==regenerated.read_bytes()
text=target.read_text()
required=['System.Data.SqlClient.SqlConnection','System.Management.ManagementObject','System.ServiceModel.BasicHttpBinding','System.ServiceModel.NetTcpBinding','System.IO.Packaging.Package','System.Speech.Synthesis.SpeechSynthesizer']
for name in required:assert name in text,name
report=dict(original_source_hashes={name:sha(source/name) for name in originals},original_sdk_reference_count=len(paths),reference_sha256_and_owners=records,restored_source_archive_owners=owners,full_catalog_sha256=sha(target),independent_original_generator_output_matches=True,mmi_excluded_as_upstream=True,required_dependency_catalog_types=required,full_shipping_powershell_complete=False)
(root/'full-sdk-catalog-audit.json').write_text(json.dumps(report,indent=2)+'\n')
print('PASS original full SDK catalog:',len(paths),'source-owned references; all required dependency types, original MMI exclusion and exact independent regeneration')
