"""Independently audit the Archive package and its source PowerShell test tool."""
from pathlib import Path
import hashlib
import io
import json
import re
import subprocess
import tarfile
import tempfile
import xml.etree.ElementTree as E
import zipfile

sha=lambda data:hashlib.sha256(data).hexdigest()
def package(path):
    process=subprocess.Popen(['zstd','-dc',str(path)],stdout=subprocess.PIPE)
    with tarfile.open(fileobj=process.stdout,mode='r|') as tar:
        result={p.name.removeprefix('./'):tar.extractfile(p).read() for p in tar if p.isfile()}
    # bsdtar may pad a small package past Python's end-of-tar marker. Consume
    # the remaining decompressed bytes before waiting for the pipe writer.
    while process.stdout.read(1024*1024):pass
    assert process.wait()==0
    return result
def archive(path):
    with tarfile.open(path) as tar:
        members=[p for p in tar if p.isfile()]
        prefix=members[0].name.split('/')[0]+'/'
        return {p.name.removeprefix(prefix):tar.extractfile(p).read() for p in members}
def inputs(root):
    info=(root/'.SRCINFO').read_text()
    sources=re.findall(r'^\s+source = (.+)$',info,re.M)
    hashes=re.findall(r'^\s+sha256sums = (.+)$',info,re.M)
    assert len(sources)==len(hashes)>0
    for name,digest in zip(sources,hashes):
        path=root/name.split('::')[0]
        assert sha(path.read_bytes())==digest,path
    return len(sources)

tool_root=Path('mingw-w64-powershell-module-test-host')
module_root=Path('mingw-w64-powershell-archive')
tool_inputs=inputs(tool_root);module_inputs=inputs(module_root)
tool_release=re.search(r'^\s+pkgrel = (\d+)$',(tool_root/'.SRCINFO').read_text(),re.M).group(1)
tool_artifact=tool_root/f'mingw-w64-clang-x86_64-powershell-module-test-host-7.4.20-{tool_release}-any.pkg.tar.zst'
module_artifact=module_root/'mingw-w64-clang-x86_64-powershell-archive-1.2.6-1-any.pkg.tar.zst'
tool=package(tool_artifact);module=package(module_artifact)
prefix='clang64/lib/dotnet/tools/powershell-module-test-host/'
doc='clang64/share/doc/powershell-module-test-host/'
payload=json.loads(tool[doc+'module-host-payload.json'])
actual={name.removeprefix(prefix) for name in tool if name.startswith(prefix) and name.endswith('.dll')}
assert actual==set(payload),(actual-set(payload),set(payload)-actual)
for name,record in payload.items():assert sha(tool[prefix+name])==record['sha256'],name
assert not any(name.endswith('.pdb') for name in tool)
entry=json.loads(tool[prefix+'source-console-entry.json'])
assert entry['source_changes']==[]
assert entry['source_sha256']==sha(archive(tool_root/'powershell-7.4.20.tar.gz')[entry['original_source']])
for name,digest in entry['outputs'].items():assert sha(tool[prefix+name])==digest,name
for name,digest in entry['references'].items():assert sha(tool[prefix+name])==digest,name
assert tool[prefix+'pwsh.exe'][:2]==b'MZ' and b'pwsh.dll' in tool[prefix+'pwsh.exe']
assert tool['clang64/share/licenses/powershell-module-test-host/Dotnet-AppHost-LICENSE'].startswith(b'The MIT License')
assert tool['clang64/bin/powershell-module-test-host']==(tool_root/'powershell-module-test-host').read_bytes()
assert tool[prefix+'powershell-module-test-host.cmd']==(tool_root/'powershell-module-test-host.cmd').read_bytes()
core=archive(tool_root/'powershell-7.4.20.tar.gz')
coverage=json.loads(tool[doc+'source-coverage.json'])
original_production=0
for name,record in coverage.items():
    for path,digest in record['source_sha256'].items():assert sha(core[path])==digest,(name,path)
    if name not in ('PSVersionInfoGenerator','ResGen','TypeCatalogGen'):
        original_production+=record['original_sources']
assert original_production==1001
assert len(coverage)==13
metadata=json.loads(tool[prefix+'source-runtime-metadata.json'])
for path,digest in metadata['manifests'].items():
    assert sha(core[path])==digest
    assert tool[prefix+Path(path).name]==core[path]
assert set(metadata['autoload_manifests'].values())==set(metadata['manifests'])
for path,original in metadata['autoload_manifests'].items():
    assert tool[prefix+path]==core[original],path
for name,digest in metadata['reference_metadata'].items():assert sha(tool[prefix+'ref/'+name])==digest
gherkin=archive(tool_root/'gherkin-5.1.0.tar.gz')
gherkin_expected={name:sha(data) for name,data in gherkin.items() if name.startswith('Gherkin/') and name.endswith('.cs')}
assert json.loads(tool[doc+'gherkin-source-inventory.json'])==gherkin_expected
pester=archive(tool_root/'pester-4.10.1.tar.gz')
inventory=json.loads(tool[prefix+'Modules/Pester/source-pester-inventory.json'])
roots=('Pester.psd1','Pester.psm1','Functions','Dependencies','en-US','lib','LICENSE')
expected={name:sha(data) for name,data in pester.items() if name.split('/')[0] in roots and Path(name).suffix.lower() not in ('.dll','.exe','.pdb','.nupkg')}
assert inventory['original_source_files']==expected
for name,digest in expected.items():assert sha(tool[prefix+'Modules/Pester/'+name])==digest,name
assert sha(tool[prefix+'Modules/Pester/lib/Gherkin/core/Gherkin.dll'])==inventory['rebuilt_gherkin_sha256']
assert sum(name.startswith(prefix+'Modules/Pester/') and name.endswith('.dll') for name in tool)==1
assert inventory['legacy_framework_binaries_included'] is False

upstream=archive(module_root/'archive-1.2.6.tar.gz')
golden=json.loads((module_root/'published-source-provenance.json').read_text())
for name,record in golden['files'].items():assert sha(upstream['Microsoft.PowerShell.Archive/'+name])==record['source_sha256']
with tempfile.TemporaryDirectory() as temp:
    path=Path(temp)/'Microsoft.PowerShell.Archive.psm1'
    path.write_text(upstream['Microsoft.PowerShell.Archive/'+path.name].decode('utf-8-sig').replace('\r\n','\n'))
    subprocess.run(['patch','--fuzz=0','-p1','-i',str((module_root/'0001-preserve-reserved-device-warning-in-fallback-data.patch').resolve())],cwd=temp,check=True,stdout=subprocess.PIPE)
    patched=path.read_bytes()
with zipfile.ZipFile(io.BytesIO(module['clang64/lib/dotnet/library-packs/microsoft.powershell.archive.1.2.6.nupkg'])) as z:
    assert not any(name.endswith(('.dll','.exe','.pdb','.p7s')) for name in z.namelist())
    tree=E.fromstring(z.read('Microsoft.PowerShell.Archive.nuspec'));ns={'n':tree.tag[1:].split('}')[0]}
    meta=tree.find('n:metadata',ns)
    assert meta.findtext('n:id',namespaces=ns)=='Microsoft.PowerShell.Archive'
    assert meta.findtext('n:version',namespaces=ns)=='1.2.6'
    assert meta.find('n:dependencies',ns) is None
    assert meta.find('n:license',ns).text=='MIT'
    assert meta.find('n:repository',ns).get('commit')==golden['source_commit']
    provenance=json.loads(z.read('source-build/provenance.json'))
    assert provenance['original_files']=={name:item['source_sha256'] for name,item in golden['files'].items()}
    for name in golden['files']:
        expected=patched if name.endswith('.psm1') else upstream['Microsoft.PowerShell.Archive/'+name]
        assert z.read(name)==expected,name
        assert module['clang64/lib/powershell/Modules/Microsoft.PowerShell.Archive/1.2.6/'+name]==expected,name
        assert b'# SIG # Begin signature block' not in expected
        assert provenance['packaged_files'][name]==sha(expected)
    assert z.read('LICENSE')==upstream['LICENSE']
assert module['clang64/share/licenses/powershell-archive/LICENSE']==upstream['LICENSE']
assert 'pkgver = 1.2.6-1' in module['.PKGINFO'].decode()
assert f'pkgver = 7.4.20-{tool_release}' in tool['.PKGINFO'].decode()
record=dict(tool=dict(artifact=tool_artifact.name,sha256=sha(tool_artifact.read_bytes()),
    pinned_inputs=tool_inputs,original_production_sources=1001,original_source_generators=3,
    child_autoload_manifest_files=len(metadata['autoload_manifests']),
    original_console_entry_sources=1,source_native_apphost=True,
    dll_assets=len(payload),source_reference_assemblies=len(metadata['reference_metadata']),
    source_pester_files=len(inventory['original_source_files']),source_gherkin_cs_files=len(gherkin_expected)),
    archive=dict(artifact=module_artifact.name,sha256=sha(module_artifact.read_bytes()),pinned_inputs=module_inputs,
    nuget_id='Microsoft.PowerShell.Archive',nuget_version='1.2.6',source_commit=golden['source_commit'],source_files=3,
    source_corrections=['fallback reserved-device warning string'],compression_extraction_algorithms_unchanged=True))
Path('tests/powershell-deps/module-packages-artifacts-20261027.json').write_text(json.dumps(record,indent=2)+'\n')
print('PASS module packages: all pinned inputs,1001 original production sources/three generators plus original console entry/source native apphost,292 DLL assets, full source Pester/Gherkin inventory, Archive source patch/NuGet/loose-file parity and licenses')
