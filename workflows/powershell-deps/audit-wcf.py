"""Audit the seven source-built WCF package payloads and API audit data."""
import hashlib
import io
import json
from pathlib import Path
import subprocess
import sys
import xml.etree.ElementTree as ET
import zipfile

root=Path('mingw-w64-dotnet-wcf')
fields={}
for line in (root/'.SRCINFO').read_text().splitlines():
    key,sep,value=line.strip().partition(' = ')
    if sep:fields.setdefault(key,[]).append(value)
assert len(fields['source'])==len(fields['sha256sums'])
for source,wanted in zip(fields['source'],fields['sha256sums']):
    assert wanted!='SKIP'
    if '::' not in source:assert hashlib.sha256((root/source).read_bytes()).hexdigest()==wanted,source
published=json.loads(Path('tests/powershell-deps/wcf-published-metadata-20261026.json').read_text())
legacy=json.loads((root/'legacy-forwards.json').read_text())
shim=next(e for e in published if e['assembly']=='System.ServiceModel' and e['path'].startswith('lib/'))
assert legacy['published_sha256']==shim['sha256'] and shim['method_definitions']==0
assert legacy['forwards']==[{k:e[k] for k in ['type','target']} for e in shim['forwards']]
assert len(legacy['forwards'])==274
assert sum('+' not in e['type'] for e in legacy['forwards'])==270
audit=json.loads((root/'upstream-api-mismatches.json').read_text())
assert len(audit['members'])==3
for entry in audit['members']:
    assert entry['published_reference_contains_member'] and not entry['published_implementation_contains_member']
    reference=next(e for e in published if e['sha256']==entry['reference_sha256'])
    implementation=next(e for e in published if e['sha256']==entry['implementation_sha256'])
    assert reference['assembly']==entry['assembly'] and implementation['assembly']=='System.Private.ServiceModel'
    signature=entry['signature']
    type_name=signature.split(' ')[0]
    method_name=('.ctor' if ' ctor(' in signature else signature.split(' ')[3].split('`')[0])
    reference_type=next(e for e in reference['types'] if e['name']==type_name)
    implementation_type=next(e for e in implementation['types'] if e['name']==type_name)
    if method_name=='.ctor':
        assert any(e['name']==method_name and e['parameters']==0 and e['access']=='Family' for e in reference_type['methods'])
        assert not any(e['name']==method_name and e['parameters']==0 for e in implementation_type['methods'])
    else:
        assert any(e['name']==method_name for e in reference_type['methods'])
        assert not any(e['name']==method_name for e in implementation_type['methods'])

parts={'private-servicemodel':'System.Private.ServiceModel','web-services-description':'System.Web.Services.Description',
       **{'servicemodel-'+n.lower():'System.ServiceModel.'+n for n in ['Primitives','Duplex','Security','Http','NetTcp']}}
artifacts=sorted(root.glob('*.pkg.tar.zst'));assert len(artifacts)==7
records=[]
for suffix,name in parts.items():
    package=next(p for p in artifacts if p.name.startswith('mingw-w64-clang-x86_64-dotnet-'+suffix+'-4.10.3-'))
    entries=subprocess.check_output(['tar','--zstd','-tf',str(package)],text=True).splitlines()
    def payload(path):return subprocess.check_output(['tar','--zstd','-xOf',str(package),path])
    doc='clang64/share/doc/dotnet-'+suffix+'/'
    for leaf in ['README.md','removed-binaries.json','localization.json','manifest.json','legacy-forwards.json','upstream-api-mismatches.json']:
        assert doc+leaf in entries
    for leaf in ['LICENSE','THIRD-PARTY-NOTICES']:assert 'clang64/share/licenses/dotnet-'+suffix+'/'+leaf in entries
    assert json.loads(payload(doc+'removed-binaries.json'))==[]
    resources=json.loads(payload(doc+'localization.json'))
    assert len(resources)==26 and sum(len(e['strings']) for e in resources)==12896
    manifest=json.loads(payload(doc+'manifest.json'));assert len(manifest)==7
    library=next(e for e in manifest if e['id']==name)
    nupkg='clang64/lib/dotnet/library-packs/'+name.lower()+'.4.10.3.nupkg'
    assert [e for e in entries if e.endswith('.nupkg')]==[nupkg]
    info=payload('.PKGINFO').decode()
    assert 'pkgver = 4.10.3-1\n' in info
    dependencies=['runtime=8.0.131']
    if suffix=='private-servicemodel':dependencies+=['objectpool=8.0.31','cryptography-xml=8.0.31']
    elif suffix=='servicemodel-primitives':dependencies+=['private-servicemodel=4.10.3','syndication8=8.0.31']
    elif suffix.startswith('servicemodel-'):dependencies+=['private-servicemodel=4.10.3','servicemodel-primitives=4.10.3']
    assert sorted(e[9:] for e in info.splitlines() if e.startswith('depend = '))==sorted('mingw-w64-clang-x86_64-dotnet-'+d for d in dependencies)
    with zipfile.ZipFile(io.BytesIO(payload(nupkg))) as archive:
        spec=ET.fromstring(archive.read(next(p for p in archive.namelist() if p.endswith('.nuspec'))))
        metadata=next(e for e in spec if e.tag.endswith('}metadata'))
        values={e.tag.rsplit('}',1)[-1]:e.text for e in metadata}
        assert values['id']==name and values['version']=='4.10.3'
        actual={e.get('id'):e.get('version') for e in spec.iter() if e.tag.endswith('}dependency')}
        assert actual=={k:'['+v+']' for k,v in library['dependencies'].items()}
        repository=next(e for e in metadata if e.tag.endswith('}repository'))
        assert repository.get('url')=='https://github.com/dotnet/wcf' and repository.get('commit')==audit['source_commit']
        assets=[p for p in archive.namelist() if p.endswith('.dll') or p.endswith('/_._')]
        assert sorted(assets)==sorted(library['assets'])
        assert 'ref/net8.0/'+name+'.dll' in assets if name!='System.Private.ServiceModel' else 'ref/net8.0/_._' in assets
    records.append({'recipe':str(root),'artifact':package.name,'sha256':hashlib.sha256(package.read_bytes()).hexdigest(),
                    'nuget_id':name,'nuget_version':'4.10.3','dependencies':library['dependencies'],'assets':library['assets'],
                    'source_commit':audit['source_commit'],'source_sha256':fields['sha256sums'][0]})
    print(name+': checksums, licenses, dependencies, exact reference/runtime assets and source metadata PASS')
if len(sys.argv)==2:Path(sys.argv[1]).write_text(json.dumps(records,indent=2)+'\n')
