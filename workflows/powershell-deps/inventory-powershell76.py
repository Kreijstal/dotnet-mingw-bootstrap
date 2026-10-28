"""Inventory declared Windows shipping dependencies without evaluating a vendor SDK."""
from pathlib import Path
import hashlib
import json
import sys
import tarfile
import xml.etree.ElementTree as E

archive,output=(Path(p).resolve() for p in sys.argv[1:])
digest=hashlib.sha256(archive.read_bytes()).hexdigest()
assert digest=='9970c95bea498a6d30587c2df4885e22f6dbbdf974d57e644046e088434a117d'
with tarfile.open(archive) as tar:
    original={p.name.removeprefix('PowerShell-7.6.0/'):tar.extractfile(p).read()
              for p in tar if p.isfile() and p.name.startswith('PowerShell-7.6.0/')}

def normalized(path):
    parts=[]
    for item in path.replace('\\','/').split('/'):
        if item=='..': parts.pop()
        elif item not in {'','.'}: parts.append(item)
    return '/'.join(parts)

roots=['src/powershell-win-core/powershell-win-core.csproj',
       'src/Microsoft.PowerShell.LocalAccounts/Microsoft.PowerShell.LocalAccounts.csproj',
       'src/ResGen/ResGen.csproj','src/TypeCatalogGen/TypeCatalogGen.csproj',
       'src/Modules/PSGalleryModules.csproj']
queue=list(roots);seen=set();packages={};edges=[];imports=[];properties={}
while queue:
    path=queue.pop()
    if path in seen: continue
    seen.add(path);tree=E.fromstring(original[path])
    parents={child:parent for parent in tree.iter() for child in parent}
    for node in tree.iter():
        tag=node.tag.rsplit('}',1)[-1]
        conditions=[];ancestor=node
        while ancestor is not None:
            if ancestor.get('Condition'): conditions.append(ancestor.get('Condition'))
            ancestor=parents.get(ancestor)
        if tag in {'TargetFramework','LangVersion','RuntimeFrameworkVersion'}:
            properties.setdefault(tag,[]).append(dict(project=path,value=node.text,conditions=conditions))
        if tag=='PackageReference':
            name=node.get('Include') or node.get('Update')
            version=node.get('Version') or next((p.text for p in node if p.tag.rsplit('}',1)[-1]=='Version'),None)
            assert name and version,(path,E.tostring(node))
            packages.setdefault(name,[]).append(dict(version=version,project=path,conditions=conditions,
                private_assets=node.get('PrivateAssets'),kind='build-analyzer' if path=='Analyzers.props' else 'declared-package'))
        if tag in {'ProjectReference','Import'}:
            value=node.get('Include') if tag=='ProjectReference' else node.get('Project')
            if not value: continue
            resolved=normalized(str(Path(path).parent/value)) if '$' not in value else None
            record=dict(project=path,declared_path=value,resolved_path=resolved,conditions=conditions,sdk=node.get('Sdk'))
            if resolved in original:
                queue.append(resolved)
                (edges if tag=='ProjectReference' else imports).append(record)
            else:
                assert tag=='Import',(path,value)
                imports.append(record)
baseline=json.loads((Path(__file__).parent/'requirements-7.4.20.json').read_text())
old={p['id']:{v['version'] for v in p['requirements']} for p in baseline['packages']}
lock=json.loads((Path(__file__).parents[2]/'mingw-w64-powershell/source-dependency-lock.json').read_text())
known_archives=set(lock['source_nuget_archives'])
records=[]
for name,requirements in sorted(packages.items()):
    versions=sorted({p['version'] for p in requirements})
    records.append(dict(id=name,requirements=requirements,declared_versions=versions,
        baseline_7_4_20_versions=sorted(old.get(name,set())),
        same_declared_version_as_baseline=bool(name in old and set(versions)==old[name]),
        all_literal_archives_in_7_4_source_lock=all((name+'.'+version+'.nupkg').lower() in known_archives for version in versions),
        source_package_and_runtime_verification_complete=False))
result=dict(powershell_version='7.6.0',source_url='https://github.com/PowerShell/PowerShell/tree/v7.6.0',
    source_archive_sha256=digest,source_tag_object='3a9c841177966fd94eedc0c8cc8fb3204e7ce67c',
    source_commit='767990ba06f8579d69f99eec46057541374aa892',
    sdk=json.loads(original['global.json'])['sdk'],scope='declared Windows console and conditional desktop graph, LocalAccounts, generators, bundled modules and analyzers; includes forced non-Windows native package references',
    roots=roots,projects=sorted(seen),project_reference_edges=edges,imports=imports,framework_and_language_properties=properties,
    original_project_sha256={p:hashlib.sha256(original[p]).hexdigest() for p in sorted(seen)},packages=records,
    declared_package_ids=len(records),same_version_ids=sum(p['same_declared_version_as_baseline'] for p in records),
    new_ids=sorted(set(packages)-set(old)),removed_ids=sorted(set(old)-set(packages)),
    exact_archive_matches_in_7_4_source_lock=sum(p['all_literal_archives_in_7_4_source_lock'] for p in records),
    restored_transitive_closure_verified=False,source_sdk10_built=False,source_runtime10_built=False,
    full_dependency_goal_complete=False)
output.write_text(json.dumps(result,indent=2)+'\n')
print('PASS pinned source inventory:',len(seen),'project/props inputs,',len(records),'declared IDs,',result['same_version_ids'],'unchanged versions,',len(result['new_ids']),'new IDs; transitive closure and .NET10 builds remain unverified')
