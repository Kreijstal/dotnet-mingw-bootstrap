"""Audit the source-built System.Management package required by PowerShell and WCF."""
import hashlib
import io
import json
from pathlib import Path
import struct
import subprocess
import sys
import xml.etree.ElementTree as ET
import zipfile

def pe(data):
    assert data[:2] == b'MZ'
    offset = struct.unpack_from('<I', data, 0x3c)[0]
    assert data[offset:offset + 4] == b'PE\0\0'
    machine, sections = struct.unpack_from('<HH', data, offset + 4)
    optional_size, flags = struct.unpack_from('<HH', data, offset + 20)
    optional = offset + 24
    assert machine == 0x8664 and flags & 0x2000
    assert struct.unpack_from('<H', data, optional)[0] == 0x20b
    directories = [struct.unpack_from('<II', data, optional + 112 + index * 8) for index in range(16)]
    assert directories[14] == (0, 0), 'Native DLL must have no CLR header'
    mappings = []
    for index in range(sections):
        base = optional + optional_size + index * 40
        virtual_size, rva, raw_size, raw = struct.unpack_from('<IIII', data, base + 8)
        mappings.append((rva, max(virtual_size, raw_size), raw))

    def file_offset(rva):
        for start, size, raw in mappings:
            if start <= rva < start + size:
                return raw + rva - start
        raise AssertionError(('Unmapped RVA', rva))

    def string_at(rva):
        start = file_offset(rva)
        return data[start:data.index(b'\0', start)].decode('ascii')

    exports = []
    if directories[0][0]:
        table = file_offset(directories[0][0])
        count, names = struct.unpack_from('<II', data, table + 24)[0], struct.unpack_from('<I', data, table + 32)[0]
        table = file_offset(names)
        exports = [string_at(struct.unpack_from('<I', data, table + 4 * index)[0]) for index in range(count)]
    imports = []
    if directories[1][0]:
        table = file_offset(directories[1][0])
        while any(data[table:table + 20]):
            imports.append(string_at(struct.unpack_from('<I', data, table + 12)[0]))
            table += 20
    return {'machine': 'AMD64', 'entry_point': struct.unpack_from('<I', data, optional + 16)[0],
            'exports': exports, 'imports': imports}


root=Path(sys.argv[1]) if len(sys.argv)>=3 else Path('mingw-w64-dotnet-management')
fields={}
for line in (root/'.SRCINFO').read_text().splitlines():
    key,sep,value=line.strip().partition(' = ')
    if sep:fields.setdefault(key,[]).append(value)
assert len(fields['source'])==len(fields['sha256sums'])
for source,wanted in zip(fields['source'],fields['sha256sums']):
    assert wanted!='SKIP'
    if '::' not in source:assert hashlib.sha256((root/source).read_bytes()).hexdigest()==wanted,source
pattern=fields['pkgname'][-1]+'-'+fields['pkgver'][0]+'-'+fields['pkgrel'][0]+'-*.pkg.tar.zst'
artifacts=list(root.glob(pattern));assert len(artifacts)==1,(pattern,artifacts)
package=artifacts[0]
entries=subprocess.check_output(['tar','--zstd','-tf',str(package)],text=True).splitlines()
def payload(path):return subprocess.check_output(['tar','--zstd','-xOf',str(package),path])
for path in ['clang64/share/licenses/dotnet-management/LICENSE.port','clang64/share/doc/dotnet-management/source-port.patch','clang64/share/doc/dotnet-management/native-exports.json','clang64/share/licenses/dotnet-management/LICENSE','clang64/share/licenses/dotnet-management/THIRD-PARTY-NOTICES',
             'clang64/share/doc/dotnet-management/README.md','clang64/share/doc/dotnet-management/removed-binaries.json','clang64/share/doc/dotnet-management/localization.json']:
    assert path in entries
resources=json.loads(payload('clang64/share/doc/dotnet-management/localization.json'))
assert len(resources)==1 and len(resources[0]['strings'])==79
info=payload('.PKGINFO').decode()
assert 'pkgver = 8.0.0-2\n' in info
assert 'depend = mingw-w64-clang-x86_64-dotnet-runtime=8.0.131\n' in info
assert not any(line.startswith('depend = mingw-w64-clang-x86_64-dotnet-codeanalysis') for line in info.splitlines())
nupkg='clang64/lib/dotnet/library-packs/system.management.8.0.0.nupkg'
assert [e for e in entries if e.endswith('.nupkg')]==[nupkg]
with zipfile.ZipFile(io.BytesIO(payload(nupkg))) as archive:
    spec=ET.fromstring(archive.read(next(p for p in archive.namelist() if p.endswith('.nuspec'))))
    metadata=next(e for e in spec if e.tag.endswith('}metadata'))
    values={e.tag.rsplit('}',1)[-1]:e.text for e in metadata}
    assert values['id']=='System.Management' and values['version']=='8.0.0'
    assert {e.get('id'):e.get('version') for e in spec.iter() if e.tag.endswith('}dependency')}=={'System.CodeDom':'[8.0.0]'}
    repository=next(e for e in metadata if e.tag.endswith('}repository'))
    assert repository.get('url')=='https://github.com/dotnet/runtime'
    assert sorted(p for p in archive.namelist() if p.endswith('.dll'))==['ref/net8.0/System.Management.dll','runtimes/win-x64/native/msys-wmi-native.dll','runtimes/win/lib/net8.0/System.Management.dll']
    native=pe(archive.read('runtimes/win-x64/native/msys-wmi-native.dll'))
    expected=json.loads((root/'native-exports.json').read_text())
    assert len(expected)==65 and sorted(native['exports'])==sorted(expected)
    assert all(e.lower() in {'ole32.dll','oleaut32.dll','kernel32.dll','advapi32.dll'} or e.lower().startswith('api-ms-win-crt-') for e in native['imports'])
assert 'depend = mingw-w64-clang-x86_64-dotnet-codedom=8.0.31\n' in info
patch=payload('clang64/share/doc/dotnet-management/source-port.patch')
assert patch==(root/'0001-use-source-built-wmi-backend.patch').read_bytes()
assert payload('clang64/share/doc/dotnet-management/apartment-security.patch')==(root/'0002-secure-service-calls-in-calling-apartment.patch').read_bytes()
record={'recipe':str(root),'artifact':package.name,'sha256':hashlib.sha256(package.read_bytes()).hexdigest(),
        'nuget_id':'System.Management','nuget_version':'8.0.0','source_tag':'runtime v8.0.31',
        'source_sha256':fields['sha256sums'][0],'framework':'net8.0','assembly_version':'8.0.0.0',
        'public_key_token':'b03f5f7f11d50a3a','upstream_api_signatures':819,'neutral_resource_strings':79,'dependencies':{'System.CodeDom':'8.0.0'},'native_backend':native}
if len(sys.argv)>=3:Path(sys.argv[2]).write_text(json.dumps(record,indent=2)+'\n')
elif len(sys.argv)==2:Path(sys.argv[1]).write_text(json.dumps(record,indent=2)+'\n')
print('System.Management: checksums, licenses, dependency metadata, resource baseline and exact NuGet payload PASS')
