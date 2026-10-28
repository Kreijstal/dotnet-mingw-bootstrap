"""Audit source inputs, all four NuGets, and actual compiler dependency caches."""
from pathlib import Path
import hashlib
import json
import subprocess
import sys
import tarfile
import xml.etree.ElementTree as E
import zipfile

root,out=(Path(p).resolve() for p in sys.argv[1:])
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
source=json.loads((out/'source-inputs.json').read_text())
source_roots={'runtime':root/'roslyn-deps9-1/runtime-source','roslyn':root/'roslyn414-1/roslyn-source'}
archive_paths={'runtime':root/'runtime-v9.0.0.tar.gz','roslyn':root/'roslyn-4.14.0-8edf7bcd.tar.gz'}
archive_hashes={'runtime':'32755d528ead386762fb9da054249f6151ac78ef6a92f437044838cd0ab00def','roslyn':'8cf2d365d1fad52b480f7c68b5c477cb586a935ada0eb309b0c40dacfec5fe9c'}
selected={'runtime':{},'roslyn':{}}
for name,entry in source['projects'].items():
    assert entry['production_cs_changes']==0
    wanted=selected[entry['source_repository']]
    wanted.update(entry['original_cs']);wanted.update(entry['original_resources'])
    wanted[entry['original_project']]=entry['original_project_sha256']
selected['runtime']['src/libraries/System.Composition/src/System.Composition.csproj']=source['composition_meta_original_project_sha256']
archive_checks={}
for repo,archive in archive_paths.items():
    assert sha(archive)==archive_hashes[repo]
    verified=set()
    with tarfile.open(archive) as tar:
        assert tar.pax_headers['comment']==source[repo+'_commit']
        for member in tar:
            if not member.isfile():continue
            name=member.name.partition('/')[2]
            if name not in selected[repo]:continue
            data=tar.extractfile(member).read();expected=selected[repo][name]
            assert hashlib.sha256(data).hexdigest()==expected,(repo,name)
            assert sha(source_roots[repo]/name)==expected,(repo,name)
            verified.add(name)
    assert verified==set(selected[repo])
    archive_checks[repo]=dict(archive_sha256=archive_hashes[repo],selected_original_inputs_verified=len(verified))
expected_ids={'System.Threading.Channels':'9.0.0','System.Composition':'9.0.0',
              'Microsoft.CodeAnalysis.Workspaces.Common':'4.14.0','Microsoft.CodeAnalysis.CSharp.Workspaces':'4.14.0'}
packages={}
for package in (out/'feed').glob('*.nupkg'):
    with zipfile.ZipFile(package) as z:
        spec=E.fromstring(z.read(next(p for p in z.namelist() if p.endswith('.nuspec'))))
        md=next(e for e in spec if e.tag.endswith('metadata'));values={e.tag.rsplit('}',1)[-1]:e.text for e in md}
        name=values['id'];assert name in expected_ids and values['version']==expected_ids[name]
        repo='runtime' if name.startswith('System.') else 'roslyn'
        repository=next(e for e in md if e.tag.endswith('repository'))
        assert repository.get('url')=='https://github.com/dotnet/'+repo and repository.get('commit')==source[repo+'_commit']
        dependencies={e.get('id'):e.get('version') for e in md.iter() if e.tag.endswith('dependency')}
        if name=='System.Composition':
            wanted={'System.Composition.'+n:'[9.0.0]' for n in ['AttributedModel','Convention','Hosting','Runtime','TypedParts']}
            assert dependencies==wanted and not any(p.endswith('.dll') for p in z.namelist())
        else:
            project='Microsoft.CodeAnalysis.Workspaces' if name.endswith('Workspaces.Common') else name
            assert dependencies=={n:'['+v+']' for n,v in source['projects'][project]['dependencies'].items()}
            assert any(p.startswith('lib/net8.0/') and p.endswith('.dll') for p in z.namelist())
        packages[name]=dict(version=values['version'],sha256=sha(package),dependencies=dependencies)
assert set(packages)==set(expected_ids)
feeds=[out/'feed',root/'roslyn414-1/feed',root/'roslyn-deps9-1/feed',Path('/clang64/lib/dotnet/library-packs')]
feed_files={}
for feed in feeds:
    for p in feed.glob('*.nupkg'):feed_files.setdefault(p.name.lower(),p)
compile_checks=[]
for name in source['projects']:
    assets=json.loads((out/name/'obj/project.assets.json').read_text())
    for library,entry in assets['libraries'].items():
        if entry['type']!='package':continue
        ident,version=library.split('/');filename=ident.lower()+'.'+version+'.nupkg';assert filename in feed_files,library
        locations=[]
        for folder in assets['packageFolders']:
            base=Path(subprocess.check_output(['cygpath','-u',folder],text=True).strip())/entry['path']
            if (base/filename).is_file():locations.append(base)
        assert len(locations)==1,(name,library,locations)
        cache=locations[0];expected=feed_files[filename]
        assert (cache/filename).read_bytes()==expected.read_bytes(),(name,library,'different archive')
        with zipfile.ZipFile(expected) as z:
            dlls=[p for p in z.namelist() if p.endswith('.dll')]
            for p in dlls:assert (cache/p).read_bytes()==z.read(p),(name,library,p)
        compile_checks.append(dict(project=name,dependency=library,source_nupkg_sha256=sha(expected),cached_dlls_checked=len(dlls)))
result=dict(source_archive_checks=archive_checks,nuget_packages=packages,actual_source_compile_package_references=compile_checks,
    source_production_cs_changes=0,source_channels_minimum=source['original_channels_minimum'],resolved_source_channels_version=source['resolved_source_channels_version'],
    all_source_archive_package_and_compile_dependency_checks_passed=True,full_dependency_goal_complete=False)
(out/'source-package-audit.json').write_text(json.dumps(result,indent=2)+'\n')
print('PASS pinned source archives,4 NuGets and',len(compile_checks),'actual compiler-cache source package references')
