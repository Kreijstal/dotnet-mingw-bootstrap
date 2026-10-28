"""Audit the exact AsyncInterfaces 8, Newtonsoft and telemetry package payloads."""
import hashlib
import io
import json
from pathlib import Path
import subprocess
import sys
import xml.etree.ElementTree as E
import zipfile

targets=[
    ('asyncinterfaces8','Microsoft.Bcl.AsyncInterfaces','8.0.0',{}),
    ('newtonsoft-json','Newtonsoft.Json','13.0.4',{}),
    ('applicationinsights','Microsoft.ApplicationInsights','2.23.0',{'System.Diagnostics.DiagnosticSource':'[8.0.1]'})]
records=[]
for suffix,name,version,dependencies in targets:
    root=Path('mingw-w64-dotnet-'+suffix);fields={}
    for line in (root/'.SRCINFO').read_text().splitlines():
        key,sep,value=line.strip().partition(' = ')
        if sep:fields.setdefault(key,[]).append(value)
    assert len(fields['source'])==len(fields['sha256sums'])
    for source,wanted in zip(fields['source'],fields['sha256sums']):
        assert wanted!='SKIP'
        if '::' not in source:assert hashlib.sha256((root/source).read_bytes()).hexdigest()==wanted,source
    artifacts=list(root.glob('*.pkg.tar.zst'));assert len(artifacts)==1
    artifact=artifacts[0]
    entries=subprocess.check_output(['tar','--zstd','-tf',str(artifact)],text=True).splitlines()
    def payload(path):return subprocess.check_output(['tar','--zstd','-xOf',str(artifact),path])
    assert 'clang64/share/licenses/dotnet-'+suffix+'/LICENSE' in entries
    if suffix!='newtonsoft-json':assert 'clang64/share/doc/dotnet-'+suffix+'/README.md' in entries
    info=payload('.PKGINFO').decode()
    assert 'pkgver = '+version+'-1\n' in info
    expected=['mingw-w64-clang-x86_64-dotnet-runtime=8.0.131']
    if suffix=='applicationinsights':expected+=['mingw-w64-clang-x86_64-dotnet-diagnosticsource=8.0.31']
    assert sorted(line[9:] for line in info.splitlines() if line.startswith('depend = '))==sorted(expected)
    nupkg='clang64/lib/dotnet/library-packs/'+name.lower()+'.'+version+'.nupkg'
    assert [p for p in entries if p.endswith('.nupkg')]==[nupkg]
    with zipfile.ZipFile(io.BytesIO(payload(nupkg))) as archive:
        spec=E.fromstring(archive.read(next(p for p in archive.namelist() if p.endswith('.nuspec'))))
        metadata=next(e for e in spec if e.tag.endswith('}metadata'))
        values={e.tag.rsplit('}',1)[-1]:e.text for e in metadata}
        assert values['id']==name and values['version']==version
        deps={e.get('id'):e.get('version') for e in spec.iter() if e.tag.endswith('}dependency')}
        assert deps==dependencies,(name,deps)
        assert [p for p in archive.namelist() if p.endswith('.dll')]==['lib/net8.0/'+name+'.dll']
        dll_hash=hashlib.sha256(archive.read('lib/net8.0/'+name+'.dll')).hexdigest()
    if suffix=='applicationinsights':
        api=payload('clang64/share/doc/dotnet-applicationinsights/PublicAPI.txt').decode().splitlines()
        assert len({p for p in api if p and not p.startswith('#')})==1033
        assert isinstance(json.loads(payload('clang64/share/doc/dotnet-applicationinsights/removed-binaries.json')),list)
    records.append(dict(recipe=str(root),artifact=artifact.name,sha256=hashlib.sha256(artifact.read_bytes()).hexdigest(),
                        nuget_id=name,nuget_version=version,dependencies=dependencies,managed_dll_sha256=dll_hash,
                        source_sha256=fields['sha256sums'][0]))
    print(name+': source checksums, license, dependencies and exact net8.0 payload PASS')
if len(sys.argv)==2:Path(sys.argv[1]).write_text(json.dumps(records,indent=2)+'\n')
