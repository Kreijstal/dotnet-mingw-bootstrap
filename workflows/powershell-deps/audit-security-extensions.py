"""Audit source signature replacement and the tool used to create its test fixtures."""
import hashlib
import io
import json
from pathlib import Path
import struct
import subprocess
import sys
import xml.etree.ElementTree as E
import zipfile

def pe(data, dll=True):
    assert data[:2] == b'MZ'
    offset = struct.unpack_from('<I', data, 0x3c)[0]
    assert data[offset:offset + 4] == b'PE\0\0'
    machine, sections = struct.unpack_from('<HH', data, offset + 4)
    optional_size, flags = struct.unpack_from('<HH', data, offset + 20)
    optional = offset + 24
    assert machine == 0x8664
    assert bool(flags & 0x2000) == dll
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


def inputs(root):
    fields={}
    for line in (root/'.SRCINFO').read_text().splitlines():
        key,sep,value=line.strip().partition(' = ')
        if sep:fields.setdefault(key,[]).append(value)
    assert len(fields['source'])==len(fields['sha256sums'])
    for source,wanted in zip(fields['source'],fields['sha256sums']):
        assert wanted!='SKIP'
        if '::' not in source:
            assert hashlib.sha256((root/source).read_bytes()).hexdigest()==wanted,source
    artifacts=list(root.glob('*.pkg.tar.zst'));assert len(artifacts)==1
    artifact=artifacts[0]
    entries=subprocess.check_output(['tar','--zstd','-tf',str(artifact)],text=True).splitlines()
    def payload(path):return subprocess.check_output(['tar','--zstd','-xOf',str(artifact),path])
    assert not any(p.startswith('msys64/') for p in entries), 'Native install prefix was not staged correctly'
    return fields,artifact,entries,payload

root=Path('mingw-w64-dotnet-security-extensions')
fields,artifact,entries,payload=inputs(root)
assert fields['pkgver']==['1.4.0'] and fields['pkgrel']==['1']
info=payload('.PKGINFO').decode()
assert 'pkgver = 1.4.0-1\n' in info
assert [line[9:] for line in info.splitlines() if line.startswith('depend = ')]==['mingw-w64-clang-x86_64-dotnet-runtime=8.0.131']
for name in ['README.md','ExpectedApi.json','source-provenance.json','signature-native.h']:
    target='clang64/share/doc/dotnet-security-extensions/'+name
    assert target in entries and payload(target)==(root/name).read_bytes()
assert payload('clang64/share/licenses/dotnet-security-extensions/LICENSE')==(root/'LICENSE').read_bytes()
api=json.loads(payload('clang64/share/doc/dotnet-security-extensions/ExpectedApi.json'))
assert len(api['api'])==29
assert api['identity']=='getfilesiginforedistwrapper, Version=10.0.0.0, Culture=neutral, PublicKeyToken=31bf3856ad364e35'
name='clang64/lib/dotnet/library-packs/microsoft.security.extensions.1.4.0.nupkg'
assert [p for p in entries if p.endswith('.nupkg')]==[name]
with zipfile.ZipFile(io.BytesIO(payload(name))) as z:
    spec=E.fromstring(z.read('Microsoft.Security.Extensions.nuspec'))
    assert spec.findtext('.//{*}id')=='Microsoft.Security.Extensions'
    assert spec.findtext('.//{*}version')=='1.4.0'
    assert not spec.findall('.//{*}dependency')
    assert spec.findtext('.//{*}license')=='MIT'
    assert z.read('README.md')==(root/'README.md').read_bytes()
    assert sorted(p for p in z.namelist() if p.endswith('.dll'))==['lib/net8.0/getfilesiginforedistwrapper.dll','runtimes/win-x64/native/msys-file-signature.dll']
    managed=z.read('lib/net8.0/getfilesiginforedistwrapper.dll')
    native_bytes=z.read('runtimes/win-x64/native/msys-file-signature.dll')
    native=pe(native_bytes)
    assert sorted(native['exports'])==['msys_signature_abi','msys_signature_free','msys_signature_query']
    assert all(p.lower() in {'kernel32.dll','wintrust.dll','crypt32.dll','ole32.dll'} or p.lower().startswith('api-ms-win-crt-') for p in native['imports'])
record=dict(recipe=str(root),artifact=artifact.name,sha256=hashlib.sha256(artifact.read_bytes()).hexdigest(),nuget_id='Microsoft.Security.Extensions',nuget_version='1.4.0',assembly_version='10.0.0.0',public_key_token='31bf3856ad364e35',published_api_declarations=29,managed_sha256=hashlib.sha256(managed).hexdigest(),native_sha256=hashlib.sha256(native_bytes).hexdigest(),native_backend=native,implementation='MSYS2 source replacement using Windows WinTrust; not the private Microsoft implementation')

root=Path('mingw-w64-osslsigncode')
fields,artifact,entries,payload=inputs(root)
assert fields['pkgver']==['2.14'] and fields['pkgrel']==['1']
info=payload('.PKGINFO').decode()
assert 'pkgver = 2.14-1\n' in info
assert sorted(line[9:] for line in info.splitlines() if line.startswith('depend = '))==['mingw-w64-clang-x86_64-openssl','mingw-w64-clang-x86_64-zlib']
assert fields['sha256sums'][0]=='0f033fd6069387d2e489fbd2187e62f624764eb8c2758ee94e3e793e5150b5c5'
assert 'clang64/share/licenses/osslsigncode/LICENSE' in entries
assert payload('clang64/share/licenses/osslsigncode/COPYING').startswith(b'                    GNU GENERAL PUBLIC LICENSE')
assert 'clang64/share/doc/osslsigncode/README.md' in entries
assert [p for p in entries if p.endswith(('.exe','.dll'))]==['clang64/bin/osslsigncode.exe']
native_bytes=payload('clang64/bin/osslsigncode.exe');native=pe(native_bytes,dll=False)
assert not any('msys' in p.lower() or 'cygwin' in p.lower() for p in native['imports'])
record['fixture_tool']=dict(recipe=str(root),artifact=artifact.name,sha256=hashlib.sha256(artifact.read_bytes()).hexdigest(),source_sha256=fields['sha256sums'][0],native=native)
if len(sys.argv)==2:Path(sys.argv[1]).write_text(json.dumps(record,indent=2)+'\n')
print('Microsoft.Security.Extensions: exact source checksums, identity baseline, licenses, sole managed/native NuGet payload and three native exports PASS')
print('osslsigncode: pinned source/checksums, native Windows executable, dependency metadata, license and install prefix PASS')
