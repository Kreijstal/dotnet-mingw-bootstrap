"""Independently prove full Abstractions source/CI overlay/test/native payload fidelity."""
from pathlib import Path
import hashlib,io,json,re,subprocess,tarfile,xml.etree.ElementTree as E,zipfile

root=Path('mingw-w64-dotnet-identitymodel-abstractions');name='Microsoft.IdentityModel.Abstractions'
sha=lambda b:hashlib.sha256(b).hexdigest()
srcinfo=(root/'.SRCINFO').read_text();sources=re.findall(r'^\s+source = (.+)$',srcinfo,re.M);hashes=re.findall(r'^\s+sha256sums = (.+)$',srcinfo,re.M)
assert len(sources)==len(hashes)==14
for source,digest in zip(sources,hashes):assert digest!='SKIP' and sha((root/source.split('::')[0]).read_bytes())==digest,source
artifact=root/'mingw-w64-clang-x86_64-dotnet-identitymodel-abstractions-6.35.0-1-any.pkg.tar.zst'
p=subprocess.Popen(['zstd','-dc',str(artifact)],stdout=subprocess.PIPE)
with tarfile.open(fileobj=p.stdout,mode='r|') as t:payload={m.name.removeprefix('./'):t.extractfile(m).read() for m in t if m.isfile()}
while p.stdout.read(1024*1024):pass
assert p.wait()==0
assert 'pkgver = 6.35.0-1\n' in payload['.PKGINFO'].decode()
assert re.findall(r'^depend = (.+)$',payload['.PKGINFO'].decode(),re.M)==['mingw-w64-clang-x86_64-dotnet-runtime=8.0.131']
assert 'pkgbuild_sha256sum = '+sha((root/'PKGBUILD').read_bytes())+'\n' in payload['.BUILDINFO'].decode()
nuget='clang64/lib/dotnet/library-packs/'+name+'.6.35.0.nupkg'
assert [n for n in payload if n.endswith('.nupkg')]==[nuget]
with zipfile.ZipFile(io.BytesIO(payload[nuget])) as z:
    r=json.loads(z.read('source-build/inventory.json'))
    assert r['source_commit']=='c94c7fc235501d478221e979062bd8837a55575b' and r['source_archive_sha256']==hashes[0]
    assert r['production_algorithm_changes']==[] and len(r['original_cs'])==9
    original={}
    with tarfile.open(root/sources[0].split('::')[0],mode='r|gz') as t:
        for m in t:
            if not m.isfile():continue
            path=m.name.split('/',1)[1]
            if path in r['original_cs'] or path in r['original_project_metadata'] or path in ['LICENSE.txt','build/35MSSharedLib1024.snk','test/Microsoft.IdentityModel.Abstractions.Tests/TelemetryEventDetailsTests.cs']:
                original[path]=t.extractfile(m).read()
    for path,digest in r['original_cs'].items():assert sha(original[path])==digest,path
    for path,digest in r['original_project_metadata'].items():assert sha(original[path])==digest,path
    assert r['original_signing_key_sha256']==sha(original['build/35MSSharedLib1024.snk'])
    meta=json.loads((root/'published-metadata.json').read_text())[0]
    assert original['build/35MSSharedLib1024.snk'].hex()==meta['public_key']
    ci='src/'+name+'/Properties/AssemblyInfo.cs'
    assert set(r['ci_version_metadata'])=={ci}
    updated=original[ci].decode('utf-8-sig')
    for attr,value in [('AssemblyVersion',meta['version']),('AssemblyFileVersion',meta['attributes']['AssemblyFileVersionAttribute']),('AssemblyInformationalVersion',meta['attributes']['AssemblyInformationalVersionAttribute'])]:
        old='[assembly: '+attr+'("0.0.1")]';assert updated.count(old)==1
        updated=updated.replace(old,'[assembly: '+attr+'("'+value+'")]')
    assert sha(updated.encode())==r['ci_version_metadata'][ci]
    expected=dict(r['original_cs']);expected.update(r['ci_version_metadata'])
    assert r['actual_compiler_inputs']==expected and len(r['generated_sdk_metadata'])==2
    assert sorted(r['removed_build_only_package_references'])==['Microsoft.CodeAnalysis.FxCopAnalyzers','Microsoft.SourceLink.GitHub']
    original_test=original['test/Microsoft.IdentityModel.Abstractions.Tests/TelemetryEventDetailsTests.cs']
    assert sha(original_test)==r['original_telemetry_test_sha256']
    # prepare.py uses read_text(), which preserves the source UTF8 BOM. Keep it
    # here as well so the comparison covers every original fixture byte.
    adapted=original_test.decode('utf-8').replace('using Xunit;','').replace('using Moq;','').replace('[Fact]','[OwnedFact]')
    adapted=adapted.replace('var mock = new Mock<TelemetryEventDetails>();','var mock = new OwnedTelemetry();').replace('mock.CallBase = true;','').replace('mock.Object','mock')
    assert adapted.count('[OwnedFact]')==6 and adapted.encode()==z.read('source-build/UpstreamTests.cs')
    assert sha(adapted.encode())==r['adapted_test_sha256']
    assert {n for n in z.namelist() if n.endswith('.dll')}=={'lib/net8.0/'+name+'.dll','ref/net8.0/'+name+'.dll'}
    assert sha(z.read('lib/net8.0/'+name+'.dll'))==r['implementation_sha256']
    assert sha(z.read('ref/net8.0/'+name+'.dll'))==r['reference_sha256']
    assert r['implementation_sha256']!=r['reference_sha256']
    assert sha(z.read('lib/net8.0/'+name+'.xml'))==r['documentation_sha256']
    assert z.read('LICENSE.txt')==original['LICENSE.txt']==payload['clang64/share/licenses/dotnet-identitymodel-abstractions/LICENSE']
    assert z.read('README.md')==(root/'README.md').read_bytes()==payload['clang64/share/doc/dotnet-identitymodel-abstractions/README.md']
    assert z.read('source-build/inventory.json')==payload['clang64/share/doc/dotnet-identitymodel-abstractions/source-inventory.json']
    metadata=E.fromstring(z.read(name+'.nuspec')).find('{*}metadata')
    assert metadata.findtext('{*}id')==name and metadata.findtext('{*}version')=='6.35.0'
    assert metadata.findtext('{*}license')=='MIT' and metadata.find('{*}repository').get('commit')==r['source_commit']
    assert not metadata.findall('{*}dependencies/{*}group/{*}dependency')
    assert [n.get('file') for n in metadata.findall('{*}references/{*}group/{*}reference')]==[name+'.dll']
    assert len(json.loads((root/'published-api.json').read_text())['api'])==52
record=dict(artifact=artifact.name,sha256=sha(artifact.read_bytes()),source_commit=r['source_commit'],
            original_compiler_items=9,unchanged_implementation_files=8,ci_version_metadata_only_files=1,
            public_protected_api_declarations=52,upstream_telemetry_cases=6,
            implementation_sha256=r['implementation_sha256'],reference_sha256=r['reference_sha256'],production_algorithm_changes=[])
Path('tests/powershell-deps/identitymodel-abstractions-artifact-20261027.json').write_text(json.dumps(record,indent=2)+'\n')
print('PASS Abstractions all14 pinned inputs, nine original compiler items/CI-only version overlay, six adapted upstream cases, source runtime/ref/XML/native metadata/licenses')
