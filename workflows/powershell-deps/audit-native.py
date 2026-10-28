"""Audit the source-built PowerShell native and manifest-compiler artifacts."""
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


records = []
for folder in ['mingw-w64-dotnet-event-manifest-compiler', 'mingw-w64-powershell-native']:
    root = Path(folder)
    fields = {}
    for line in (root / '.SRCINFO').read_text().splitlines():
        key, sep, value = line.strip().partition(' = ')
        if sep:
            fields.setdefault(key, []).append(value)
    assert len(fields['source']) == len(fields['sha256sums'])
    for source, wanted in zip(fields['source'], fields['sha256sums']):
        assert wanted != 'SKIP'
        if '::' not in source:
            assert hashlib.sha256((root / source).read_bytes()).hexdigest() == wanted, source
    artifacts = list(root.glob('*.pkg.tar.zst'))
    assert len(artifacts) == 1
    artifact = artifacts[0]
    entries = subprocess.check_output(['tar', '--zstd', '-tf', str(artifact)], text=True).splitlines()

    def payload(path):
        return subprocess.check_output(['tar', '--zstd', '-xOf', str(artifact), path])

    suffix = folder.removeprefix('mingw-w64-')
    assert 'clang64/share/licenses/' + suffix + '/LICENSE' in entries
    assert 'clang64/share/doc/' + suffix + '/README.md' in entries
    record = {'recipe': folder, 'artifact': artifact.name,
              'sha256': hashlib.sha256(artifact.read_bytes()).hexdigest(),
              'source_checksums': dict(zip(fields['source'], fields['sha256sums']))}
    info = payload('.PKGINFO').decode()
    assert 'pkgver = ' + fields['pkgver'][0] + '-' + fields['pkgrel'][0] + '\n' in info
    if suffix == 'powershell-native':
        nupkg = 'clang64/lib/dotnet/library-packs/microsoft.powershell.native.700.0.0.nupkg'
        assert [e for e in entries if e.endswith('.nupkg')] == [nupkg]
        with zipfile.ZipFile(io.BytesIO(payload(nupkg))) as archive:
            spec = ET.fromstring(archive.read(next(p for p in archive.namelist() if p.endswith('.nuspec'))))
            metadata = next(e for e in spec if e.tag.endswith('}metadata'))
            values = {e.tag.rsplit('}', 1)[-1]: e.text for e in metadata}
            assert values['id'] == 'Microsoft.PowerShell.Native' and values['version'] == '700.0.0'
            assert not [e for e in spec.iter() if e.tag.endswith('}dependency')]
            repository = next(e for e in metadata if e.tag.endswith('}repository'))
            assert repository.get('commit') == '2c55e8fc288e31ce72974ad783151b06049591c4'
            expected = ['runtimes/win-x64/native/PowerShell.Core.Instrumentation.dll',
                        'runtimes/win-x64/native/pwrshplugin.dll']
            assert sorted(p for p in archive.namelist() if p.endswith('.dll')) == expected
            native = {Path(p).name: pe(archive.read(p)) for p in expected}
            resources = native['PowerShell.Core.Instrumentation.dll']
            assert resources['entry_point'] == 0 and resources['imports'] == [] and resources['exports'] == []
            plugin = native['pwrshplugin.dll']
            expected_exports = {'GetCLRVersionForPSVersion', 'WSManPluginStartup', 'WSManPluginShutdown',
                                'WSManPluginShell', 'WSManPluginReleaseShellContext', 'WSManPluginCommand',
                                'WSManPluginReleaseCommandContext', 'WSManPluginSend', 'WSManPluginSignal',
                                'WSManPluginReceive', 'WSManPluginConnect'}
            assert set(plugin['exports']) == expected_exports
            assert 'libc++.dll' in plugin['imports'] and 'WsmSvc.DLL' in plugin['imports']
            allowed = {'wsmsvc.dll', 'advapi32.dll', 'ole32.dll', 'user32.dll', 'libc++.dll', 'kernel32.dll'}
            assert all(p.lower() in allowed or p.lower().startswith('api-ms-win-crt-') for p in plugin['imports'])
            record.update(nuget_id=values['id'], nuget_version=values['version'], native=native)
    else:
        assembly = 'clang64/lib/event-manifest-compiler/event-manifest-compiler.dll'
        assert [e for e in entries if e.endswith('.dll')] == [assembly]
        dependencies = json.loads(payload('clang64/lib/event-manifest-compiler/event-manifest-compiler.deps.json'))
        assert len(dependencies['libraries']) == 1
        assert next(iter(dependencies['libraries'].values()))['type'] == 'project'
        assert 'depend = mingw-w64-clang-x86_64-dotnet-runtime=8.0.131\n' in info
        assert payload('clang64/bin/event-manifest-compiler').startswith(b'#!/usr/bin/env bash')
    records.append(record)
    print(folder + ': checksums, license, package metadata and exact payload PASS')

if len(sys.argv) == 2:
    Path(sys.argv[1]).write_text(json.dumps(records, indent=2) + '\n')
