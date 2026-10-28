"""Audit the actual native host pacman archive, independently of staging."""
from pathlib import Path
import hashlib
import json
import struct
import subprocess
import sys
import tarfile

archive, output = (Path(p).resolve() for p in sys.argv[1:])
assert not output.exists(), output
sha = lambda data: hashlib.sha256(data).hexdigest()
files = {}
process = subprocess.Popen(['zstd', '-dc', str(archive)], stdout=subprocess.PIPE)
try:
    with tarfile.open(fileobj=process.stdout, mode='r|') as tar:
        for member in tar:
            name = member.name.removeprefix('./')
            assert '..' not in Path(name).parts and not Path(name).is_absolute(), name
            if member.isfile():
                assert name not in files, name
                files[name] = tar.extractfile(member).read()
            else:
                assert member.isdir(), name
    process.stdout.read()  # Drain tar padding before waiting for zstd.
finally:
    process.stdout.close()
assert process.wait() == 0
native = 'clang64/lib/dotnet10-bootstrap/'
doc = 'clang64/share/doc/dotnet-host10-bootstrap/'
licenses = 'clang64/share/licenses/dotnet-host10-bootstrap/'
assert all(name.startswith((native, doc, licenses)) or
           name in {'.PKGINFO', '.BUILDINFO', '.MTREE'} for name in files)
manifest = json.loads(files[doc + 'source-payload.json'])
actual = {name.removeprefix('clang64/'): sha(data) for name, data in files.items()
          if name.startswith('clang64/') and name != doc + 'source-payload.json'}
assert actual == manifest['files_sha256']
assert manifest['native_source_version'] == '10.0.5'
assert manifest['full_runtime10_built'] is False
assert manifest['full_sdk10_built'] is False
source = json.loads(files[doc + 'source-inputs.json'])
assert source['runtime_source_commit'] == '081d220c0a773ffb7c6bea6b48727833576a65ef'
assert source['runtime_archive_sha256'] == '1692acae5e3adfd625452fbc9db4fd4e982c10e38189358ae245c74dd5514d7a'
assert len(source['original_file_sha256']) == 60754
assert len(source['removed_binary_inputs']) == 35
audit = json.loads(files[doc + 'host-audit.json'])
assert len(audit['native_images']) == 7
assert len(audit['support_assets']) == 10
assert audit['product_ijwhost_built'] is True
for name, record in audit['native_images'].items():
    data = files[native + name]
    assert sha(data) == record['sha256'], name
    assert data[:2] == b'MZ', name
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    assert data[pe:pe + 4] == b'PE\0\0', name
    assert struct.unpack_from('<H', data, pe + 4)[0] == 0x8664, name
for name, digest in audit['support_assets'].items():
    assert sha(files[native + name]) == digest, name
assert files[native + 'hostfxr.dll'] == files[native + 'host/fxr/10.0.5/hostfxr.dll']
assert {name.removeprefix(native) for name in files if name.startswith(native)} == {
    *audit['native_images'], *audit['support_assets'], 'host/fxr/10.0.5/hostfxr.dll'}
for name in ['LICENSE.TXT', 'THIRD-PARTY-NOTICES.TXT', 'Arcade-LICENSE.TXT']:
    assert files[licenses + name], name
metadata = {}
for line in files['.PKGINFO'].decode().splitlines():
    if ' = ' in line:
        key, value = line.split(' = ', 1)
        metadata.setdefault(key, []).append(value)
assert metadata['pkgname'] == ['mingw-w64-clang-x86_64-dotnet-host10-bootstrap']
assert metadata['pkgver'] == ['10.0.5-1']
assert metadata['depend'] == ['mingw-w64-clang-x86_64-cc-libs']
record = dict(archive=str(archive), archive_sha256=sha(archive.read_bytes()),
              package_metadata=metadata, distribution_files=len(actual) + 1,
              native_images=7, support_assets=10, isolated_hostfxr_layout=True,
              original_source_files=60754, removed_binary_inputs=35,
              all_payload_hashes_match=True, full_runtime10_built=False,
              full_sdk10_built=False, full_dependency_goal_complete=False)
output.write_text(json.dumps(record, indent=2) + '\n')
print('PASS native package: seven AMD64 images, ten support assets, exact payload hashes and isolated ownership')
