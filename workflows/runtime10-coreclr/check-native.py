"""Windows native API checks; these do not prove managed CoreCLR10 execution."""
from pathlib import Path
import ast
import ctypes as C
from ctypes import wintypes as W
import hashlib
import json
import re
import shutil
import subprocess
import sys

root, out, probe = (Path(p).resolve() for p in sys.argv[1:])
assert not out.exists(), out
out.mkdir(parents=True)
layout = out / 'native CLR λ'
layout.mkdir()
audit = json.loads((root / 'native-audit.json').read_text())
for name, item in audit['images'].items():
    source = root / 'coreclr-build' / item['build_path']
    assert hashlib.sha256(source.read_bytes()).hexdigest() == item['sha256'], name
    shutil.copy2(source, layout / (name + '.dll'))
shutil.copy2(probe, layout / 'native-jit-probe.exe')
result = subprocess.run([str(layout / 'native-jit-probe.exe'), str(layout / 'clrjit.dll')],
                        capture_output=True, text=True, timeout=60)
(out / 'jit-probe.log').write_text(result.stdout + result.stderr)
assert result.returncode == 0, (result.returncode, result.stdout, result.stderr)
assert 'PASS original source10 JIT startup' in result.stdout, result.stdout

version = C.WinDLL('version', use_last_error=True)
version.GetFileVersionInfoSizeW.argtypes = [W.LPCWSTR, C.POINTER(W.DWORD)]
version.GetFileVersionInfoW.argtypes = [W.LPCWSTR, W.DWORD, W.DWORD, C.c_void_p]
version.VerQueryValueW.argtypes = [C.c_void_p, W.LPCWSTR, C.POINTER(C.c_void_p), C.POINTER(W.UINT)]
def file_version(path):
    import struct
    size = version.GetFileVersionInfoSizeW(str(path), None)
    assert size, path
    buffer = C.create_string_buffer(size)
    assert version.GetFileVersionInfoW(str(path), 0, size, buffer)
    value, length = C.c_void_p(), W.UINT()
    assert version.VerQueryValueW(buffer, '\\', C.byref(value), C.byref(length))
    hi, lo = struct.unpack_from('<II', C.string_at(value, length.value), 8)
    return '.'.join(map(str, [hi >> 16, hi & 65535, lo >> 16, lo & 65535]))
versions = {name: file_version(layout / (name + '.dll')) for name in audit['images']}
assert set(versions.values()) == {'10.0.526.11800'}, versions

class Descriptor(C.Structure):
    _fields_ = [('magic', C.c_uint64), ('flags', C.c_uint32), ('size', C.c_uint32),
                ('json', C.c_void_p), ('pointer_count', C.c_uint32),
                ('padding', C.c_uint32), ('pointers', C.c_void_p)]
assert C.sizeof(Descriptor) == 40
coreclr = C.WinDLL(str(layout / 'coreclr.dll'), winmode=0x1100)
descriptor = Descriptor.in_dll(coreclr, 'DotNetRuntimeContractDescriptor')
assert descriptor.magic == 0x0043414443434e44
assert descriptor.flags & 1 and not descriptor.flags & 2  # little-endian, 64-bit
assert descriptor.size > 0 and descriptor.pointer_count > 1
assert descriptor.json and descriptor.pointers
raw = C.string_at(descriptor.json, descriptor.size).rstrip(b'\0')
source = root / 'coreclr-build/vm/datadescriptor/cdac-cdac_contract_descriptor/contract-descriptor.c'
text = source.read_text()
literal = re.search(r'\.descriptor\s*=\s*(".*"),\s*\.pointer_data_count', text, re.S)[1]
expected = ast.literal_eval(literal).encode('utf-8')
assert raw == expected, 'Loaded descriptor must match the original tool-generated C literal'
assert descriptor.pointer_count == int(re.search(r'\.pointer_data_count\s*=\s*([0-9]+)', text)[1])
model = json.loads(raw)
assert model['contracts'] and model['types'] and model['globals'], model.keys()
pointers = (C.c_void_p * descriptor.pointer_count).from_address(descriptor.pointers)
assert any(pointers)
(out / 'loaded-contract-descriptor.json').write_bytes(raw + b'\n')
record = dict(native_api_exit_code=0, source_file_versions=versions,
              original_jit_ee_guid='7a8cbc56-9e19-4321-80b9-a0d2c578c945',
              jit_startup_original_host_callbacks_and_interface_guid_passed=True,
              unicode_native_layout=True, coreclr_load_and_exported_descriptor_passed=True,
              descriptor_bytes_match_original_generated_c=True,
              descriptor_json_sha256=hashlib.sha256(raw).hexdigest(),
              descriptor_bytes=len(raw), descriptor_pointer_count=descriptor.pointer_count,
              contracts=len(model['contracts']), types=len(model['types']), globals=len(model['globals']),
              managed_coreclr10_execution_verified=False, full_runtime10_built=False,
              full_sdk10_built=False, full_dependency_goal_complete=False)
(out / 'results.json').write_text(json.dumps(record, indent=2) + '\n')
print('PASS native source10 JIT startup/ABI, four PE versions and loaded original generated cDAC descriptor; managed execution still required')
