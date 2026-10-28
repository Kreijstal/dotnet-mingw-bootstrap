"""Audit the experimental full Windows SqlClient source package without claiming parity."""
from pathlib import Path
import hashlib,io,json,posixpath,re,subprocess,tarfile,tempfile,xml.etree.ElementTree as E,zipfile
root=Path('mingw-w64-dotnet-sqlclient');sha=lambda b:hashlib.sha256(b).hexdigest()
srcinfo=(root/'.SRCINFO').read_text();sources=re.findall(r'^\s+source = (.+)$',srcinfo,re.M);hashes=re.findall(r'^\s+sha256sums = (.+)$',srcinfo,re.M)
assert len(sources)==len(hashes)==23
for source,digest in zip(sources,hashes):
    with (root/source.split('::')[0]).open('rb') as f:assert digest!='SKIP' and hashlib.file_digest(f,'sha256').hexdigest()==digest,source
artifact=root/'mingw-w64-clang-x86_64-dotnet-sqlclient-4.9.1-1-any.pkg.tar.zst'
p=subprocess.Popen(['zstd','-dc',str(artifact)],stdout=subprocess.PIPE)
with tarfile.open(fileobj=p.stdout,mode='r|') as t:payload={m.name.removeprefix('./'):t.extractfile(m).read() for m in t if m.isfile()}
while p.stdout.read(1024*1024):pass
assert p.wait()==0
assert 'pkgver = 4.9.1-1\n' in payload['.PKGINFO'].decode()
assert 'Experimental' in payload['.PKGINFO'].decode()
assert re.findall(r'^depend = (.+)$',payload['.PKGINFO'].decode(),re.M)==['mingw-w64-clang-x86_64-dotnet-runtime=8.0.131']
assert 'pkgbuild_sha256sum = '+sha((root/'PKGBUILD').read_bytes())+'\n' in payload['.BUILDINFO'].decode()
package='clang64/lib/dotnet/library-packs/system.data.sqlclient.4.9.1.nupkg'
assert [p for p in payload if p.endswith('.nupkg')]==[package]
with tarfile.open(root/sources[0].split('::')[0],'r|gz') as t:original={m.name.split('/',1)[1]:t.extractfile(m).read() for m in t if m.isfile()}
base='src/System.Data.SqlClient/src/';project=E.fromstring(original[base+'System.Data.SqlClient.csproj'])
conditions={'':lambda w:True,"'$(TargetPlatformIdentifier)' == 'windows'":lambda w:w,"'$(TargetPlatformIdentifier)' != 'windows'":lambda w:not w,"'$(IsFunctionalAssembly)' == 'true'":lambda w:w,"'$(TargetFrameworkIdentifier)' == '.NETFramework'":lambda w:False,"'$(TargetFrameworkIdentifier)' != '.NETFramework' and '$(TargetPlatformIdentifier)' == ''":lambda w:not w,"'$(TargetFrameworkIdentifier)' == '.NETCoreApp'":lambda w:True}
with zipfile.ZipFile(io.BytesIO(payload[package])) as z:
    r=json.loads(z.read('source-build/source-inventory.json'));port=json.loads(z.read('source-build/managed-sni-port.json'));expected=json.loads(z.read('source-build/expected-assets.json'))
    assert r['source_commit']=='84b081710c66cc560b77d4de38cee493c3a38e9d' and r['source_sha256']==hashes[0]
    assert port['release_and_debug_use_managed_sni'] and not port['complete_runtime_compatibility_verified']
    assert expected['experimental'] and not expected['full_runtime_compatibility_verified']
    changes={p['original_path']:p for p in r['production_overlays']};assert len(changes)==4 and r['production_overlays']==port['overlays']
    assert {p['patch_name'] for p in changes.values()}=={'0001-use-managed-sni-on-windows.patch','0003-pass-complete-spns-to-windows-sspi.patch','0004-accept-short-custom-named-pipe-paths.patch','0005-honor-inclusive-smp-send-window.patch'}
    for path,change in changes.items():
        assert sha(original[path])==change['original_sha256']
        patch=(root/change['patch_name']).resolve();assert sha(patch.read_bytes())==change['patch_sha256']
        assert z.read('source-build/patches/'+patch.name)==patch.read_bytes()
        with tempfile.TemporaryDirectory() as tmp:
            staged=Path(tmp)/Path(path).name;staged.write_text(original[path].decode('utf-8-sig').replace('\r\n','\n'))
            subprocess.run(['patch','--fuzz=0','-p1','-i',str(patch)],cwd=tmp,check=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
            assert staged.read_bytes()==z.read('source-build/overlays/'+staged.name)
            assert sha(staged.read_bytes())==change['overlay_sha256']
    for role,windows in [('implementation',True),('contract',False)]:
        selected=set()
        for group in project.findall('ItemGroup'):
            assert group.get('Condition','') in conditions
            if not conditions[group.get('Condition','')](windows):continue
            for item in group.findall('Compile'):
                assert not item.get('Condition')
                if item.get('Remove'):assert item.get('Remove')=='@(Compile)';selected.clear();continue
                path=item.get('Include').replace('\\','/')
                selected.add(posixpath.normpath('src/Common/'+path.removeprefix('$(CommonPath)') if path.startswith('$(CommonPath)') else base+path))
        selected.add('src/Common/src/System/SR.cs')
        assert selected==set(r[role]['compiled_sha256']) and len(selected)==(242 if windows else 3)
        for path,digest in r[role]['compiled_sha256'].items():
            assert digest==(changes[path]['overlay_sha256'] if windows and path in changes else sha(original[path])),path
        assert len(r[role]['generated_sources'])==(4 if windows else 3)
        for path,digest in r[role]['prepared_project_metadata'].items():assert sha(z.read('source-build/'+role+'/'+path))==digest
        assert r[role]['framework_references'] and all(p.startswith('ref/net8.0/') for p in r[role]['framework_references'])
        resources={base+'Resources/Strings.resx'}|({base+'Resources/System.Data.SqlClient.SqlMetaData.xml'} if windows else set())
        assert set(r[role]['resource_inputs'])==resources
        for path,digest in r[role]['resource_inputs'].items():assert sha(original[path])==digest
    assert base+'System/Data/SqlClient/SqlFileStream.Windows.cs' in r['implementation']['compiled_sha256']
    for path,digest in r['original_build_metadata'].items():assert sha(original[path])==digest
    for item in r['removed_binary_inputs']:assert sha(original[item['path'].replace('\\','/')])==item['sha256']
    dlls={'lib/net8.0/System.Data.SqlClient.dll','ref/net8.0/System.Data.SqlClient.dll','runtimes/win/lib/net8.0/System.Data.SqlClient.dll'}
    assert {p for p in z.namelist() if p.endswith('.dll')}==dlls
    assert sha(z.read('runtimes/win/lib/net8.0/System.Data.SqlClient.dll'))==expected['implementation_sha256']==r['outputs']['implementation']['bin/Release/net8.0-windows/System.Data.SqlClient.dll']
    assert sha(z.read('lib/net8.0/System.Data.SqlClient.dll'))==expected['contract_sha256']==r['outputs']['contract']['bin/Release/net8.0/System.Data.SqlClient.dll']
    assert sha(z.read('ref/net8.0/System.Data.SqlClient.dll'))==expected['reference_sha256']==r['outputs']['contract']['obj/Release/net8.0/ref/System.Data.SqlClient.dll']
    assert len({expected[k] for k in ['implementation_sha256','contract_sha256','reference_sha256']})==3
    resource_strings={v.get('name'):v.findtext('value') for v in E.fromstring(original[base+'Resources/Strings.resx']).iter('data')}
    assert expected['resource_strings']==resource_strings and len(resource_strings)==407
    assert expected['metadata_resource_sha256']==sha(original[base+'Resources/System.Data.SqlClient.SqlMetaData.xml'])
    assert z.read('LICENSE')==original['LICENSE']==payload['clang64/share/licenses/dotnet-sqlclient/LICENSE']
    assert z.read('PACKAGE.md')==original[base+'PACKAGE.md']
    assert z.read('README.md')==(root/'README.md').read_bytes()==payload['clang64/share/doc/dotnet-sqlclient/README.md']
    assert z.read('source-build/source-inventory.json')==payload['clang64/share/doc/dotnet-sqlclient/source-inventory.json']
    assert z.read('source-build/managed-sni-port.json')==payload['clang64/share/doc/dotnet-sqlclient/managed-sni-port.json']
    meta=E.fromstring(z.read('System.Data.SqlClient.nuspec')).find('{*}metadata')
    assert meta.findtext('{*}id')=='System.Data.SqlClient' and meta.findtext('{*}version')=='4.9.1'
    assert meta.findtext('{*}license')=='MIT' and not meta.findall('.//{*}dependency')
    assert meta.find('{*}repository').get('commit')==r['source_commit']
    full=json.loads(payload['clang64/share/doc/dotnet-sqlclient/source-sni-audit.json'])
    assert full['implementation']==r['implementation'] and full['contract']==r['contract']
    assert full['tested_implementation_sha256']==expected['implementation_sha256']
    utilities=full['upstream_test_utilities'];assert sum(v['original_selected_sources'] for v in utilities.values())==142
    inventory=json.loads(payload['clang64/share/doc/dotnet-sqlclient/tds-source-inventory.json'])
    for mapping in inventory.values():
        for path,digest in mapping.items():assert sha(original[path])==digest
record=dict(artifact=artifact.name,sha256=sha(artifact.read_bytes()),nuget_sha256=sha(payload[package]),experimental=True,full_runtime_compatibility_verified=False,verified_direct_dependency_count_increment=0,source_commit=r['source_commit'],source_sha256=r['source_sha256'],windows_original_compile_items=242,contract_original_compile_items=3,production_source_overlays=4,upstream_tds_fixture_inputs=142,resource_strings=407,implementation_sha256=expected['implementation_sha256'],contract_sha256=expected['contract_sha256'],reference_sha256=expected['reference_sha256'],production_overlays=r['production_overlays'])
Path('tests/powershell-deps/sqlclient-package-artifact-20261027.json').write_text(json.dumps(record,indent=2)+'\n')
print('PASS experimental SqlClient all23 pinned inputs, complete242/3 source/compiler/resource inventories, four exact patches, reference/generic/Windows assets,142 original test inputs, native/NuGet metadata/license and tested bytes; full runtime parity remains open')
