"""Inspect the actual pacman artifact independently of its staging directory."""
from pathlib import Path
import hashlib
import json
import subprocess
import struct
import sys
import tarfile

archive, output = (Path(p).resolve() for p in sys.argv[1:])
assert not output.exists()
sha = lambda data: hashlib.sha256(data).hexdigest()
files = {}
process = subprocess.Popen(['zstd', '-dc', str(archive)], stdout=subprocess.PIPE)
try:
    with tarfile.open(fileobj=process.stdout, mode='r|') as tar:
        for member in tar:
            if member.isfile():
                files[member.name.removeprefix('./')] = tar.extractfile(member).read()
            else:
                assert member.isdir(), ('Unexpected package link/type', member.name)
    process.stdout.read()  # Drain tar padding before waiting for the producer.
finally:
    process.stdout.close()
assert process.wait() == 0
assert all(name.startswith('clang64/') or name in {'.PKGINFO','.BUILDINFO','.MTREE'} for name in files)
doc = 'clang64/share/doc/powershell/'
manifest = json.loads(files[doc + 'source-package.json'])
actual = {name.removeprefix('clang64/'): sha(data) for name, data in files.items()
          if name.startswith('clang64/') and name != doc + 'source-package.json'}
assert actual == manifest['files_sha256']
payload = json.loads(files[doc + 'application-source-payload.json'])
application = {name.removeprefix('clang64/lib/powershell/7/'): sha(data)
               for name, data in files.items() if name.startswith('clang64/lib/powershell/7/')}
assert application == payload['all_staged_files_sha256']
assert payload['original_project_dlls'] == 14
assert payload['source_reference_count'] == 163
assert len(payload['gallery_modules']) == 6
license_closure = json.loads(files[doc + 'license-closure.json'])
lock = json.loads(files[doc + 'source-dependency-lock.json'])
for key, value in lock.items():
    assert license_closure[key] == value, key
for name, digest in license_closure['copied_license_files'].items():
    assert sha(files['clang64/share/licenses/powershell/' + name]) == digest, name
assert sha(files[doc + 'launcher.c']) == manifest['source_launcher_c_sha256']
assert sha(files['clang64/bin/pwsh.exe']) == manifest['native_launcher_sha256']
native_images={}
for name in ['clang64/bin/pwsh.exe','clang64/lib/powershell/7/pwsh.exe']:
    data=files[name]
    assert data[:2]==b'MZ',name
    offset=struct.unpack_from('<I',data,0x3c)[0]
    assert data[offset:offset+4]==b'PE\0\0',name
    machine=struct.unpack_from('<H',data,offset+4)[0]
    subsystem=struct.unpack_from('<H',data,offset+24+68)[0]
    assert machine==0x8664 and subsystem==3,(name,machine,subsystem)
    native_images[name]=dict(machine='AMD64',subsystem='Windows CUI',sha256=sha(data))
native = json.loads(files[doc + 'native-console-source.json'])
assert sha(files['clang64/lib/powershell/7/pwsh.exe']) == native['native_executable_sha256']
assert native['exact_original_sdk_task_replay_matches'] is True
graphical = json.loads(files[doc + 'graphicalhost-source-audit.json'])
assert graphical['original_source_cs'] == 165
assert graphical['compiled_baml_entries'] == 16
assert graphical['explicit_original_resource_assets'] == 12
catalog = json.loads(files[doc + 'full-sdk-catalog-audit.json'])
assert catalog['original_sdk_reference_count'] == 213
assert catalog['independent_original_generator_output_matches'] is True
metadata = {}
for line in files['.PKGINFO'].decode().splitlines():
    if ' = ' in line:
        key, value = line.split(' = ', 1)
        metadata.setdefault(key, []).append(value)
assert metadata['pkgname'] == ['mingw-w64-clang-x86_64-powershell']
assert metadata['pkgver'] == ['7.4.20-2']
assert set(metadata['depend']) == {
    'mingw-w64-clang-x86_64-dotnet-runtime8-servicing=8.0.31-3',
    'mingw-w64-clang-x86_64-dotnet-windowsdesktop-bootstrap=8.0.0-64',
    'mingw-w64-clang-x86_64-cc-libs'}
assert manifest['full_dependency_goal_complete'] is False
record = dict(archive=str(archive), archive_sha256=sha(archive.read_bytes()),
              package_metadata=metadata, distribution_files=len(actual) + 1,
              native_entry_images=native_images,
              application_files=len(application), source_binary_assets=len(payload['binary_assets']),
              original_project_dlls=14, source_references=163, gallery_modules=payload['gallery_modules'],
              source_nuget_archives=len(lock['source_nuget_archives']),
              pinned_build_packages=len(lock['exact_build_package_versions']),
              bundled_license_owners=len(license_closure['bundled_source_native_owners']),
              copied_license_files=len(license_closure['copied_license_files']) + 1,
              all_distribution_application_license_hashes_match=True,
              full_dependency_goal_complete=False)
output.write_text(json.dumps(record, indent=2) + '\n')
print('PASS actual native package:', len(application), 'application files,',
      record['copied_license_files'], 'licenses; exact source binary, catalog, GUI, launcher and dependency provenance')
