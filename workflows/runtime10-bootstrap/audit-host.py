"""Audit staged native host names, AMD64 architecture, imports and source outputs."""
from pathlib import Path
import hashlib,json,re,subprocess,sys
root=Path(sys.argv[1]).resolve();host=root/'host/corehost'
names=['dotnet.exe','hostfxr.dll','hostpolicy.dll','apphost.exe','comhost.dll','ijwhost.dll','nethost.dll']
records={}
for name in names:
    path=host/name
    output=subprocess.check_output(['/clang64/bin/llvm-readobj','--file-headers','--coff-imports','--coff-exports','--coff-debug-directory',str(path)],text=True)
    assert 'IMAGE_FILE_MACHINE_AMD64' in output,name
    imports=re.findall(r'Import \{.*?Name: ([^\r\n]+)',output,re.S)
    assert imports,name
    assert all('msvcp' not in p.lower() and 'vcruntime' not in p.lower() and 'libc++' not in p.lower() and 'libunwind' not in p.lower() for p in imports),(name,imports)
    if name=='apphost.exe':assert 'IMAGE_DLL_CHARACTERISTICS_EX_CET_COMPAT' in output
    original=root/'host-build'
    outputs={'dotnet.exe':'dotnet/dotnet.exe','hostfxr.dll':'fxr/standalone/hostfxr.dll','hostpolicy.dll':'hostpolicy/standalone/hostpolicy.dll','apphost.exe':'apphost/standalone/apphost.exe','comhost.dll':'comhost/comhost.dll','ijwhost.dll':'ijwhost/ijwhost.dll','nethost.dll':'nethost/nethost.dll'}
    assert path.read_bytes()==(original/outputs[name]).read_bytes(),name
    records[name]=dict(sha256=hashlib.sha256(path.read_bytes()).hexdigest(),imports=imports,
                      exports=re.findall(r'Export \{.*?Name: ([^\r\n]+)',output,re.S))
required=['nethost.h','hostfxr.h','coreclr_delegates.h','libnethost.a','libhostfxr.a',
          'nethost.dll.a','hostfxr.dll.a','hostpolicy.dll.a','comhost.dll.a','ijwhost.dll.a']
for name in required:assert (host/name).is_file() and (host/name).stat().st_size>0,name
record=dict(runtime_source_version='10.0.5',native_images=records,
            support_assets={name:hashlib.sha256((host/name).read_bytes()).hexdigest() for name in required},
            product_ijwhost_built=True,portable_native_tests_built=True,
            excluded_msvc_only_test_projects=['comsxs','ijw','typelibs'],
            full_runtime10_built=False,full_sdk10_built=False)
(root/'host-audit.json').write_text(json.dumps(record,indent=2)+'\n')
print('PASS seven original source10 native host images, support libraries/headers, AMD64 and preserved CET; CLR/SDK10 remain open')
