"""Audit the four source10 native outputs without claiming managed execution."""
from pathlib import Path
import hashlib
import json
import re
import subprocess
import sys

root = Path(sys.argv[1]).resolve()
paths = dict(coreclr='dlls/mscoree/coreclr/coreclr.dll', clrjit='jit/clrjit.dll',
             mscorrc='dlls/mscorrc/mscorrc.dll', clretwrc='dlls/clretwrc/clretwrc.dll')
images = {}
for name, relative in paths.items():
    path = root / 'coreclr-build' / relative
    data = subprocess.check_output(['/clang64/bin/llvm-readobj', '--file-headers',
                                    '--coff-imports', '--coff-exports', str(path)], text=True)
    assert 'IMAGE_FILE_MACHINE_AMD64' in data, name
    imports = re.findall(r'(?m)^(?:Delay)?Import \{\r?\n\s+Name: ([^\r\n]+)', data)
    delayed = re.findall(r'(?m)^DelayImport \{\r?\n\s+Name: ([^\r\n]+)', data)
    assert imports, name
    assert not any(re.search('msvcp|vcruntime|libc[+][+]|libunwind', item, re.I) for item in imports), (name, imports)
    exports = re.findall(r'Export \{[^}]*?Name: ([^\r\n]+)', data)
    if name == 'coreclr':
        assert {'coreclr_initialize', 'coreclr_shutdown', 'coreclr_create_delegate',
                'coreclr_execute_assembly', 'DotNetRuntimeContractDescriptor'} <= set(exports)
        assert {item.lower() for item in delayed} == {
            'version.dll', 'api-ms-win-core-winrt-l1-1-0.dll'}, delayed
    if name == 'clrjit':
        assert {'jitStartup', 'getJit'} <= set(exports)
    images[name] = dict(build_path=relative, sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                        imports=imports, delay_imports=delayed, exports=exports)
descriptor = root / 'coreclr-build/vm/datadescriptor/cdac-cdac_contract_descriptor/contract-descriptor.c'
text = descriptor.read_text()
assert '%%jsonDescriptor%%' not in text
assert 'STUB_DESCRIPTOR' not in text
assert int(re.search(r'\.pointer_data_count\s*=\s*([0-9]+)', text)[1]) > 1, 'Expected a generated pointer table'
record = dict(runtime_source_version='10.0.5', images=images,
              original_cdac_contract_descriptor_generated=True,
              generated_cdac_source_sha256=hashlib.sha256(descriptor.read_bytes()).hexdigest(),
              managed_execution_verified=False, full_runtime10_built=False,
              full_sdk10_built=False, full_dependency_goal_complete=False)
(root / 'native-audit.json').write_text(json.dumps(record, indent=2) + '\n')
print('PASS four AMD64 source10 native outputs, required exports and generated cDAC source; managed execution still required')
