"""Read real Windows PE fixed file versions without executing the target files."""
from pathlib import Path
import argparse
import ctypes
from ctypes import wintypes
import hashlib
import json

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('root', type=Path)
parser.add_argument('--stage', choices=['native', 'all'], default='all')
parser.add_argument('--output', type=Path)
args = parser.parse_args()
root = args.root.resolve()
expected = tuple(map(int, (root / 'file-version.txt').read_text().strip().split('.')))
assert expected == (8, 0, 3126, 42000)
api = ctypes.WinDLL('version', use_last_error=True)
api.GetFileVersionInfoSizeW.argtypes = [wintypes.LPCWSTR, ctypes.POINTER(wintypes.DWORD)]
api.GetFileVersionInfoSizeW.restype = wintypes.DWORD
api.GetFileVersionInfoW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, wintypes.LPVOID]
api.GetFileVersionInfoW.restype = wintypes.BOOL
api.VerQueryValueW.argtypes = [wintypes.LPCVOID, wintypes.LPCWSTR, ctypes.POINTER(ctypes.c_void_p), ctypes.POINTER(wintypes.UINT)]
api.VerQueryValueW.restype = wintypes.BOOL

def version(path):
    ignored = wintypes.DWORD()
    size = api.GetFileVersionInfoSizeW(str(path), ctypes.byref(ignored))
    if not size:
        raise ctypes.WinError(ctypes.get_last_error())
    data = ctypes.create_string_buffer(size)
    if not api.GetFileVersionInfoW(str(path), 0, size, data):
        raise ctypes.WinError(ctypes.get_last_error())
    pointer, length = ctypes.c_void_p(), wintypes.UINT()
    if not api.VerQueryValueW(data, '\\', ctypes.byref(pointer), ctypes.byref(length)):
        raise ctypes.WinError(ctypes.get_last_error())
    assert length.value >= 52, path
    fixed = ctypes.cast(pointer, ctypes.POINTER(wintypes.DWORD * 13)).contents
    assert fixed[0] == 0xFEEF04BD, path
    major_minor, build_revision = fixed[2], fixed[3]
    return major_minor >> 16, major_minor & 65535, build_revision >> 16, build_revision & 65535

paths = [root / 'm/corehost-8.0.31/corehost' / name for name in ['dotnet.exe', 'hostfxr.dll', 'hostpolicy.dll', 'apphost.exe', 'comhost.dll', 'nethost.dll']]
paths += [root / 'm/coreclr-8.0.31' / name for name in ['coreclr.dll', 'clrjit.dll', 'mscorrc.dll', 'clretwrc.dll']]
paths.append(root / 'm/netcoreapp-8.0.31-coreclr/lib/System.Private.CoreLib.dll')
if args.stage == 'all':
    manifest = json.loads((root / 'framework-audit.json').read_text())
    paths += [root / 'm/netcoreapp-8.0.31-coreclr/lib' / (name + '.dll') for name in manifest['required_framework_assemblies'] if name != 'System.Private.CoreLib']
    paths.append(root / 'm/mono-8.0.31/libs/System.IO.Compression.Native.dll')
record = {}
for path in paths:
    assert path.is_file(), path
    if path.name == 'apphost.exe':
        # The upstream SDK stamps this template with the application's resources.
        # Assert the actual source choice and its absence of version resources;
        # all runtime DLLs and the dotnet muxer below still require exact versions.
        cmake = root / 'm/src/runtime-8.0.31-native/src/native/corehost/apphost/standalone/CMakeLists.txt'
        assert 'set(SKIP_VERSIONING 1)' in cmake.read_text()
        try:
            version(path)
        except OSError as error:
            assert error.winerror == 1813, error
        else:
            raise AssertionError('Unexpected version resources on the upstream apphost template')
        record[path.relative_to(root).as_posix()] = dict(file_version=None, reason='Original apphost standalone CMake sets SKIP_VERSIONING=1; SDK copies application resources', sha256=hashlib.sha256(path.read_bytes()).hexdigest())
        continue
    try:
        actual = version(path)
    except OSError as error:
        raise RuntimeError('Cannot read fixed file-version metadata: ' + str(path)) from error
    assert actual == expected, (path, actual, expected)
    record[path.relative_to(root).as_posix()] = dict(file_version='.'.join(map(str, actual)), sha256=hashlib.sha256(path.read_bytes()).hexdigest())
if args.output:
    args.output.write_text(json.dumps(record, indent=2) + '\n')
versioned = sum(item['file_version'] is not None for item in record.values())
print('PASS actual PE fixed file versions8.0.3126.42000 for', versioned, 'built native/managed outputs and upstream unversioned apphost template; metadata read only')
