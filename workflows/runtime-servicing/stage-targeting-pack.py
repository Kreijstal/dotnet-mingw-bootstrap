"""Stage a complete audited source targeting pack in a new private directory."""
from pathlib import Path
import argparse,hashlib,json,shutil,xml.etree.ElementTree as E,zipfile

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('framework_root',type=Path);parser.add_argument('generator_root',type=Path);parser.add_argument('destination',type=Path)
args=parser.parse_args();frame=args.framework_root.resolve();root=args.generator_root.resolve();out=args.destination.resolve();assert not out.exists(),out
refaudit=json.loads((frame/'reference-audit.json').read_text());audit=json.loads((root/'targeting-generators/build-audit.json').read_text());records=json.loads((root/'targeting-generators/source-inputs.json').read_text())
assert refaudit['runtime_source_commit']=='1219a42122cf5190c7f512850557e38c422430dc'
assert len(audit)==8 and sum(len(item['source_and_satellite_output_sha256']) for item in audit.values())==99
file_version=(frame/'file-version.txt').read_text().strip();assert file_version=='8.0.3126.42000'
sha=lambda data:hashlib.sha256(data).hexdigest()
generated={digest:relative for item in audit.values() for relative,digest in item['source_and_satellite_output_sha256'].items()}
owners={}
# These caches were restored exclusively from the installed source-built feed.
# Independently require byte-identical archives in that feed before accepting an owner.
feed=Path('/clang64/lib/dotnet/library-packs')
for archive in sorted((root/'targeting-generators/cache').rglob('*.nupkg')):
 installed=feed/archive.name;assert installed.is_file(),archive
 archive_sha=sha(installed.read_bytes());assert sha(archive.read_bytes())==archive_sha,archive
 with zipfile.ZipFile(archive) as z:
  for name in z.namelist():
   if name.endswith('.dll'):owners.setdefault(sha(z.read(name)),[]).append(dict(package=archive.name,asset=name,source_feed_archive_sha256=archive_sha))
proof={};refs=out/'ref/net8.0';analyzers=out/'analyzers/dotnet/cs';data=out/'data'
refs.mkdir(parents=True);analyzers.mkdir(parents=True);data.mkdir()
def copy(path,relative,origin):
 target=out/relative;target.parent.mkdir(parents=True,exist_ok=True);digest=sha(path.read_bytes())
 if target.exists():assert sha(target.read_bytes())==digest,(relative,path)
 else:shutil.copy2(path,target)
 proof[relative]=dict(sha256=digest,origin=origin)
manifest=E.Element('FileList',TargetFrameworkIdentifier='.NETCoreApp',TargetFrameworkVersion='8.0',FrameworkName='Microsoft.NETCore.App',Name='.NET Runtime')
platform=[]
for name,item in refaudit['references'].items():
 if item['kind']=='stub':continue
 source=frame/'m/build/refpack-8.0.31/out'/(name+'.dll');assert sha(source.read_bytes())==item['sha256'],name
 relative='ref/net8.0/'+source.name;copy(source,relative,dict(kind='audited-original-reference',reference_audit_sha256=sha((frame/'reference-audit.json').read_bytes())))
 n,version,token=(source.with_suffix('.meta')).read_text().split();assert n==name
 E.SubElement(manifest,'File',Type='Managed',Path=relative,AssemblyName=name,PublicKeyToken=token,AssemblyVersion=version,FileVersion=file_version)
 platform.append(source.name+'|Microsoft.NETCore.App.Ref|'+version+'|'+file_version)
assert len(platform)==163,len(platform)
for label,item in records.items():
 folder=root/'targeting-generators'/label/'bin/Release/net8.0'
 base='analyzers/dotnet/roslyn'+label[4:]+'/cs/' if label in ['Json3.11','Json4.0'] else 'analyzers/dotnet/cs/'
 for file in sorted(folder.rglob('*.dll')):
  relative=file.relative_to(folder).as_posix();digest=sha(file.read_bytes())
  if digest in generated:origin=dict(kind='audited-original-generator-or-satellite',audit_output=generated[digest])
  else:
   assert digest in owners,('Not a source generator or source NuGet dependency',file,digest)
   origin=dict(kind='installed-source-package-dependency',owners=owners[digest])
  copy(file,base+relative,origin)
 if label not in ['Json3.11','Json4.0']:
  E.SubElement(manifest,'File',Type='Analyzer',Language='cs',Path=base+item['assembly']+'.dll',AssemblyName=item['assembly'],PublicKeyToken='cc7b13ffcd2ddd51',AssemblyVersion='8.0.0.0',FileVersion=file_version)
E.indent(manifest);E.ElementTree(manifest).write(data/'FrameworkList.xml',encoding='utf-8',xml_declaration=True)
(data/'PlatformManifest.txt').write_text('\n'.join(sorted(platform))+'\n')
overrides=frame/'m/src/runtime-8.0.31-native/src/installer/pkg/sfx/Microsoft.NETCore.App/PackageOverrides.txt'
assert overrides.is_file() and overrides.stat().st_size>0
shutil.copy2(overrides,data/'PackageOverrides.txt')
report=dict(runtime_source_commit=refaudit['runtime_source_commit'],reference_assemblies=163,generator_projects=8,audited_generator_outputs=99,file_version=file_version,files=proof,framework_list_sha256=sha((data/'FrameworkList.xml').read_bytes()),platform_manifest_sha256=sha((data/'PlatformManifest.txt').read_bytes()),original_package_overrides_sha256=sha(overrides.read_bytes()),supported_generator_host='source .NET8/Roslyn4.9.2',installed_or_packaged=False,complete=False)
(out/'source-payload.json').write_text(json.dumps(report,indent=2)+'\n')
print('PASS staged163 source references,99 audited generator outputs and byte-verified source package dependency closure:',len(proof),'DLL assets; normal SDK selection and packaging checks remain required')
