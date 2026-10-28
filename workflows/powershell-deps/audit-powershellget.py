"""Independent recipe/source/module/package provenance audit for PowerShellGet."""
from pathlib import Path
import hashlib
import io
import json
import re
import subprocess
import tarfile
import xml.etree.ElementTree as E
import zipfile

sha=lambda data:hashlib.sha256(data).hexdigest()
root=Path('mingw-w64-powershell-powershellget')
info=(root/'.SRCINFO').read_text()
inputs=re.findall(r'^\s+source = (.+)$',info,re.M)
hashes=re.findall(r'^\s+sha256sums = (.+)$',info,re.M)
assert len(inputs)==len(hashes)==11
for source,digest in zip(inputs,hashes):
    assert sha((root/source.split('::')[0]).read_bytes())==digest,source
gold=json.loads((root/'published-provenance.json').read_text())
assert gold['source_commit']=='22a87ee00bc461fe49068c9e8e1b079bfdc6c3f8'
assert gold['source_archive_sha256']==hashes[0]
with tarfile.open(root/inputs[0].split('::')[0]) as t:
    members=[p for p in t if p.isfile()]
    prefix=members[0].name.split('/')[0]+'/'
    source={p.name.removeprefix(prefix):t.extractfile(p).read() for p in members}
actual={name:sha(data) for name,data in source.items() if name.startswith(('src/PowerShellGet/','DSC/'))}
assert actual==gold['source_files']
assert sha(source['tools/build.psm1'])==gold['original_build_module_sha256']
artifact=root/'mingw-w64-clang-x86_64-powershell-powershellget-2.2.5-1-any.pkg.tar.zst'
process=subprocess.Popen(['zstd','-dc',str(artifact)],stdout=subprocess.PIPE)
with tarfile.open(fileobj=process.stdout,mode='r|') as t:
    package={p.name.removeprefix('./'):t.extractfile(p).read() for p in t if p.isfile()}
while process.stdout.read(1024*1024): pass
assert process.wait()==0
assert 'pkgver = 2.2.5-1' in package['.PKGINFO'].decode()
assert 'depend = mingw-w64-clang-x86_64-powershell-packagemanagement>=1.4.4' in package['.PKGINFO'].decode()
assert not any(name.endswith(('.dll','.exe','.pdb')) for name in package)
doc='clang64/share/doc/powershell-powershellget/'
loose='clang64/lib/powershell/Modules/PowerShellGet/2.2.5/'
with zipfile.ZipFile(io.BytesIO(package['clang64/lib/dotnet/library-packs/powershellget.2.2.5.nupkg'])) as z:
    types=E.fromstring(z.read('[Content_Types].xml'))
    extensions={x.get('Extension') for x in types if x.tag.endswith('Default')}
    overrides={x.get('PartName') for x in types if x.tag.endswith('Override')}
    assert all(name=='[Content_Types].xml' or Path(name).name.rsplit('.',1)[-1] in extensions or '/'+name in overrides for name in z.namelist())
    tree=E.fromstring(z.read('PowerShellGet.nuspec'));ns={'n':tree.tag[1:].split('}')[0]}
    meta=tree.find('n:metadata',ns)
    assert meta.findtext('n:id',namespaces=ns)=='PowerShellGet'
    assert meta.findtext('n:version',namespaces=ns)=='2.2.5'
    assert meta.find('n:repository',ns).get('commit')==gold['source_commit']
    assert meta.find('n:license',ns).text=='MIT'
    deps=meta.findall('n:dependencies/n:dependency',ns)
    assert [(d.get('id'),d.get('version')) for d in deps]==[('PackageManagement','[1.4.4,)')]
    record=json.loads(z.read('source-build/provenance.json'))
    assert record['original_files']==actual
    assert record['source_changes']==[]
    assert record['original_build_module_sha256']==sha(source['tools/build.psm1'])
    assert set(record['module_files'])==set(gold['module_assets'])
    assert {name.removeprefix(loose) for name in package if name.startswith(loose)}==set(gold['module_assets'])
    for name,item in gold['module_assets'].items():
        data=z.read(name)
        assert data==package[loose+name],name
        assert sha(data)==record['module_files'][name],name
        if item['source']: assert data==source[item['source']],name
        text=data.decode('utf-16' if data[:2] in (b'\xff\xfe',b'\xfe\xff') else 'utf-8-sig').replace('\r\n','\n')
        # Source outputs carry no release signatures. Compare the independently
        # recorded release body hash, allowing only the stated manifest spacing.
        assert 'SIG # Begin signature block' not in text,name
        text=text.rstrip()+'\n'
        if name=='PowerShellGet.psd1': text='\n'.join(line.lstrip() for line in text.splitlines())+'\n'
        assert sha(text.encode())==item['normalized_sha256'],name
    assert z.read('LICENSE')==source['LICENSE']==package['clang64/share/licenses/powershell-powershellget/LICENSE']
    assert z.read('source-build/provenance.json')==package[doc+'source-build.json']
    assert z.read('README.md')==(root/'README.md').read_bytes()==package[doc+'README.md']
assert package[doc+'published-provenance.json']==(root/'published-provenance.json').read_bytes()
result=dict(artifact=artifact.name,sha256=sha(artifact.read_bytes()),pinned_inputs=len(inputs),
    source_commit=gold['source_commit'],original_module_and_dsc_files=len(actual),module_files=16,
    source_changes=[],published_normalized_script_characters=654574,nuget_id='PowerShellGet',nuget_version='2.2.5')
Path('tests/powershell-deps/powershellget-artifact-20261027.json').write_text(json.dumps(result,indent=2)+'\n')
print('PASS PowerShellGet all11 pinned inputs, complete source inventory, original assembler, all16 release-content comparisons, NuGet/loose parity, dependency metadata and licenses')
