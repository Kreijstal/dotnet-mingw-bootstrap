"""Independently audit full PSResourceGet source, module and private package closure."""
from pathlib import Path
import hashlib,io,json,re,subprocess,tarfile,xml.etree.ElementTree as E,zipfile

root=Path('mingw-w64-powershell-psresourceget');sha=lambda b:hashlib.sha256(b).hexdigest()
def read_artifact(path):
    p=subprocess.Popen(['zstd','-dc',str(path)],stdout=subprocess.PIPE)
    with tarfile.open(fileobj=p.stdout,mode='r|') as t:
        payload={m.name.removeprefix('./'):t.extractfile(m).read() for m in t if m.isfile()}
    while p.stdout.read(1024*1024):pass
    assert p.wait()==0
    return payload
info=(root/'.SRCINFO').read_text();sources=re.findall(r'^\s+source = (.+)$',info,re.M);hashes=re.findall(r'^\s+sha256sums = (.+)$',info,re.M)
assert len(sources)==len(hashes)==18
for source,digest in zip(sources,hashes):
    with (root/source.split('::')[0]).open('rb') as f:assert digest!='SKIP' and hashlib.file_digest(f,'sha256').hexdigest()==digest,source
artifact=root/'mingw-w64-clang-x86_64-powershell-psresourceget-1.1.1-1-any.pkg.tar.zst';payload=read_artifact(artifact)
assert 'pkgver = 1.1.1-1\n' in payload['.PKGINFO'].decode()
assert set(re.findall(r'^depend = (.+)$',payload['.PKGINFO'].decode(),re.M))==set(re.findall(r'^\s+depends = (.+)$',info,re.M))
assert 'pkgbuild_sha256sum = '+sha((root/'PKGBUILD').read_bytes())+'\n' in payload['.BUILDINFO'].decode()
package='clang64/lib/dotnet/library-packs/microsoft.powershell.psresourceget.1.1.1.nupkg';loose='clang64/lib/powershell/Modules/Microsoft.PowerShell.PSResourceGet/1.1.1/'
assert [p for p in payload if p.endswith('.nupkg')]==[package]
with tarfile.open(root/sources[0].split('::')[0],'r|gz') as t:
    original={m.name.split('/',1)[1]:t.extractfile(m).read() for m in t if m.isfile()}
frozen=json.loads((root/'source-inputs.json').read_text())
for path,digest in frozen['original_src_inputs'].items():assert sha(original[path])==digest,path
mapping={
 'Azure.Core':('azure-core','azure-core'),'Azure.Identity':('azure-identity','azure-identity'),
 'Microsoft.Extensions.Primitives':('extensions6','extensions-primitives6'),
 'Microsoft.Extensions.FileProviders.Abstractions':('extensions6','fileproviders-abstractions6'),
 'Microsoft.Extensions.FileSystemGlobbing':('extensions6','filesystem-globbing6'),
 'Microsoft.Identity.Client':('msal','msal'),'Microsoft.Identity.Client.Extensions.Msal':('msal','msal-extensions'),
 'Microsoft.IdentityModel.Abstractions':('identitymodel-abstractions','identitymodel-abstractions'),
 'Newtonsoft.Json':('newtonsoft-json','newtonsoft-json'),'System.ClientModel':('clientmodel','clientmodel'),
 'System.Memory.Data':('memory-data','memory-data'),'System.Text.Json':('system-text-json8','system-text-json8'),
 'System.Formats.Asn1':('crypto','formats-asn1'),'System.Security.Cryptography.Pkcs':('crypto','cryptography-pkcs'),
 'System.Security.Cryptography.ProtectedData':('windows-libraries','protecteddata')}
for name,lib in json.loads(Path('mingw-w64-dotnet-nuget-client/libraries.json').read_text()).items():mapping[name]=('nuget-client',lib['suffix'].removeprefix('dotnet-'))
cache={};owners={}
def dependency(name,version):
    if name not in cache:
        recipe,suffix=mapping[name];matches=list(Path('mingw-w64-dotnet-'+recipe).glob('mingw-w64-clang-x86_64-dotnet-'+suffix+'-[0-9]*.pkg.tar.zst'));assert len(matches)==1,(name,matches)
        native=read_artifact(matches[0]);paths=[p for p in native if p.lower().endswith('/'+name.lower()+'.'+version+'.nupkg')];assert len(paths)==1,(name,paths)
        cache[name]=(native,native[paths[0]])
        owners[name]=dict(artifact=str(matches[0]),sha256=sha(matches[0].read_bytes()),nuget_sha256=sha(native[paths[0]]))
    return cache[name]
with zipfile.ZipFile(io.BytesIO(payload[package])) as z:
    r=json.loads(z.read('source-inventory.json'));private=json.loads(z.read('source-dependencies.json'));files=json.loads(z.read('module-files.json'))
    assert r['source_commit']==frozen['source_commit'] and r['source_archive_sha256']==hashes[0]
    assert len(r['original_cs'])==56 and r['original_cs']==r['actual_original_compile_items'] and len(r['generated_sdk_metadata'])==2
    assert set(r['original_cs'])=={p for p in original if p.startswith('src/code/') and p.endswith('.cs')}
    for mapping_name in ['original_cs','original_module_assets','original_project_metadata']:
        for path,digest in r[mapping_name].items():assert sha(original[path])==digest,path
    for path,digest in r['original_module_assets'].items():assert sha(z.read(path.removeprefix('src/')))==digest
    change,=r['production_cs_changes'];assert change['path']=='src/code/ContainerRegistryServerAPICalls.cs'
    data=original[change['path']];before=b'new SortedDictionary<SemanticVersion, string>';after=b'new SortedDictionary<NuGet.Versioning.SemanticVersion, string>'
    assert data.count(before)==1;overlay=data.replace(before,after)
    assert sha(data)==change['original_sha256'] and sha(overlay)==change['prepared_sha256']
    assert z.read('source-build/'+change['overlay'])==overlay
    assert r['actual_overlay_compile_items']=={change['path']:sha(overlay)}
    for path,digest in r['prepared_project_metadata'].items():assert sha(z.read('source-build/'+path))==digest
    expected_dependencies={k:'['+v+']' for k,v in frozen['declared_packages'].items() if k not in ['PowerShellStandard.Library','System.Net.Http','Microsoft.CSharp']}
    assert r['source_dependencies']==expected_dependencies
    assert len(private)==26 and {p[:-4] for p in private}==set(mapping)
    assert private['System.Security.Cryptography.Pkcs.dll']['asset']=='runtimes/win/lib/net8.0/System.Security.Cryptography.Pkcs.dll'
    for filename,record in private.items():
        name,version=record['package'].rsplit('/',1);native,nupkg=dependency(name,version)
        assert sha(nupkg)==record['nuget_sha256']
        with zipfile.ZipFile(io.BytesIO(nupkg)) as source:
            assert z.read('dependencies/'+filename)==source.read(record['asset'])
            assert sha(z.read('dependencies/'+filename))==record['sha256']
            for legal in record['licenses']:assert z.read('licenses/'+name.lower()+'/'+version+'/'+legal)==source.read(legal)
        for legal in record['native_license_assets']:
            assert z.read(legal['target'])==native[legal['source'].lstrip('/')]
            assert sha(z.read(legal['target']))==legal['sha256']
        assert record['licenses'] or record['native_license_assets']
    for reference in r['actual_references']:
        assert reference['origin'] in ['source-cache','source-framework','source-engine']
        if reference['origin']=='source-cache':
            lower,version,path=reference['path'].split('/',2);name=next(n for n in mapping if n.lower()==lower)
            with zipfile.ZipFile(io.BytesIO(dependency(name,version)[1])) as source:assert sha(source.read(path))==reference['sha256']
    name='Microsoft.PowerShell.PSResourceGet'
    assert {p for p in z.namelist() if p.endswith('.dll')}=={name+'.dll','source-build/ref/net8.0/'+name+'.dll'}|{'dependencies/'+p for p in private}
    assert sha(z.read(name+'.dll'))==r['implementation_sha256']
    assert sha(z.read('source-build/ref/net8.0/'+name+'.dll'))==r['reference_sha256']
    assert sha(z.read('source-build/docs/'+name+'.xml'))==r['documentation_sha256']
    assert r['implementation_sha256']!=r['reference_sha256']
    for path,digest in files.items():assert sha(z.read(path))==digest,path
    assert {p.removeprefix(loose) for p in payload if p.startswith(loose)}==set(files)|{'module-files.json'}
    for path in set(files)|{'module-files.json'}:assert payload[loose+path]==z.read(path),path
    for path in ['LICENSE','Notice.txt']:assert z.read(path)==original[path]==payload['clang64/share/licenses/powershell-psresourceget/'+path]
    assert z.read('README.md')==(root/'README.md').read_bytes()==payload['clang64/share/doc/powershell-psresourceget/README.md']
    for path in ['source-inventory.json','source-dependencies.json','module-files.json']:assert z.read(path)==payload['clang64/share/doc/powershell-psresourceget/'+path]
    meta=E.fromstring(z.read(name+'.nuspec')).find('{*}metadata')
    assert meta.findtext('{*}id')==name and meta.findtext('{*}version')=='1.1.1'
    assert meta.findtext('{*}license')=='MIT' and not meta.findall('.//{*}dependency')
    assert meta.find('{*}repository').get('commit')==r['source_commit']
    assert len(json.loads((root/'published-api.json').read_text())['api'])==460
record=dict(artifact=artifact.name,sha256=sha(artifact.read_bytes()),nuget_sha256=sha(payload[package]),source_commit=r['source_commit'],original_compile_items=56,unchanged_original_compile_items=55,private_source_dependencies=26,public_protected_api_declarations=460,implementation_sha256=r['implementation_sha256'],reference_sha256=r['reference_sha256'],production_cs_changes=r['production_cs_changes'],private_dependency_owners=owners)
Path('tests/powershell-deps/psresourceget-artifact-20261027.json').write_text(json.dumps(record,indent=2)+'\n')
print('PASS PSResourceGet all18 pinned inputs, complete original56-input/module/compiler provenance, exact type qualification, 26 independent source package payloads/licenses including Windows RID PKCS, native/NuGet/loose module/ref/XML identities and bytes')
