"""Run with Windows Python: validate the new native host without a .NET10 runtime."""
from pathlib import Path
import ctypes as C
from ctypes import wintypes as W
import hashlib,json,os,shutil,struct,subprocess,sys

host,out=(Path(p).resolve() for p in sys.argv[1:3]);out.mkdir()
layout=out/'native host λ';layout.mkdir()
(layout/'host/fxr/10.0.5').mkdir(parents=True)
shutil.copy2(host/'dotnet.exe',layout/'dotnet.exe')
shutil.copy2(host/'hostfxr.dll',layout/'host/fxr/10.0.5/hostfxr.dll')
env=dict(os.environ,DOTNET_ROOT=str(layout),DOTNET_ROOT_X64=str(layout),DOTNET_MULTILEVEL_LOOKUP='0',DOTNET_CLI_TELEMETRY_OPTOUT='1')
info=subprocess.run([str(layout/'dotnet.exe'),'--info'],env=env,text=True,encoding='utf-8',capture_output=True,timeout=30)
assert info.returncode==0,(info.stdout,info.stderr)
(out/'dotnet-info.txt').write_text(info.stdout+info.stderr,encoding='utf-8')
assert '10.0.5' in info.stdout

version=C.WinDLL('version',use_last_error=True)
version.GetFileVersionInfoSizeW.argtypes=[W.LPCWSTR,C.POINTER(W.DWORD)]
version.GetFileVersionInfoW.argtypes=[W.LPCWSTR,W.DWORD,W.DWORD,C.c_void_p]
version.VerQueryValueW.argtypes=[C.c_void_p,W.LPCWSTR,C.POINTER(C.c_void_p),C.POINTER(W.UINT)]
def file_version(path):
    import struct
    size=version.GetFileVersionInfoSizeW(str(path),None);assert size,path
    data=C.create_string_buffer(size);assert version.GetFileVersionInfoW(str(path),0,size,data)
    value=C.c_void_p();length=W.UINT();assert version.VerQueryValueW(data,'\\',C.byref(value),C.byref(length))
    raw=C.string_at(value,length.value);hi,lo=struct.unpack_from('<II',raw,8)
    return '.'.join(map(str,[hi>>16,hi&65535,lo>>16,lo&65535]))
versions={name:file_version(host/name) for name in ['dotnet.exe','hostfxr.dll','hostpolicy.dll','nethost.dll','comhost.dll','ijwhost.dll']}
assert set(versions.values())=={'10.0.526.11800'},versions

# Check the actual PE debug-directory flag, not just the linker command line.
data=(host/'apphost.exe').read_bytes();pe=struct.unpack_from('<I',data,0x3c)[0]
assert data[pe:pe+4]==b'PE\0\0' and struct.unpack_from('<H',data,pe+4)[0]==0x8664
sections=struct.unpack_from('<H',data,pe+6)[0];optional=pe+24
optional_size=struct.unpack_from('<H',data,pe+20)[0]
def file_offset(rva):
    for index in range(sections):
        location=optional+optional_size+40*index
        virtual_size,address,raw_size,raw=struct.unpack_from('<IIII',data,location+8)
        if address<=rva<address+max(virtual_size,raw_size):return raw+rva-address
    raise AssertionError(('Unmapped native RVA',rva))
debug_rva,debug_size=struct.unpack_from('<II',data,optional+112+6*8)
debug=file_offset(debug_rva);cet=False
for entry in range(debug,debug+debug_size,28):
    kind,size,address,raw=struct.unpack_from('<IIII',data,entry+12)
    if kind==20 and size>=4:cet=bool(struct.unpack_from('<I',data,raw)[0]&1)
assert cet,'Original apphost CET compatibility must be preserved'

class Parameters(C.Structure):
    _fields_=[('size',C.c_size_t),('assembly_path',W.LPCWSTR),('dotnet_root',W.LPCWSTR)]
parameters=Parameters(C.sizeof(Parameters),None,str(layout))
nethost=C.WinDLL(str(host/'nethost.dll'))
get_path=nethost.get_hostfxr_path
get_path.argtypes=[W.LPWSTR,C.POINTER(C.c_size_t),C.POINTER(Parameters)]
get_path.restype=C.c_int
size=C.c_size_t(0)
rc=get_path(None,C.byref(size),C.byref(parameters))
assert rc&0xffffffff==0x80008098 and size.value>1,hex(rc&0xffffffff)
buffer=C.create_unicode_buffer(size.value)
assert get_path(buffer,C.byref(size),C.byref(parameters))==0
assert Path(buffer.value).resolve()==layout/'host/fxr/10.0.5/hostfxr.dll'

class Environment(C.Structure):
    _fields_=[('size',C.c_size_t),('hostfxr_version',W.LPCWSTR),('hostfxr_commit_hash',W.LPCWSTR),
              ('sdk_count',C.c_size_t),('sdks',C.c_void_p),('framework_count',C.c_size_t),('frameworks',C.c_void_p)]
seen=[]
@C.CFUNCTYPE(None,C.POINTER(Environment),C.c_void_p)
def capture(pointer,context):
    value=pointer.contents
    seen.append(dict(size=value.size,hostfxr_version=value.hostfxr_version,commit=value.hostfxr_commit_hash,
                     sdk_count=value.sdk_count,framework_count=value.framework_count))
fxr=C.CDLL(buffer.value)
get_environment=fxr.hostfxr_get_dotnet_environment_info
get_environment.argtypes=[W.LPCWSTR,C.c_void_p,C.c_void_p,C.c_void_p]
get_environment.restype=C.c_int
assert get_environment(str(layout),None,capture,None)==0
assert len(seen)==1 and seen[0]['hostfxr_version']=='10.0.5',seen
assert seen[0]['commit']=='081d220c0a773ffb7c6bea6b48727833576a65ef',seen
assert seen[0]['sdk_count']==0 and seen[0]['framework_count']==0,seen
upstream={}
if len(sys.argv)==4:
    tests=Path(sys.argv[3]).resolve();run=out/'upstream-tests';run.mkdir()
    for name in ['test_fx_ver.exe','nativehost.exe']:shutil.copy2(tests/name,run/name)
    shutil.copy2(host/'nethost.dll',run/'nethost.dll')
    commands={'fx_ver':[str(run/'test_fx_ver.exe')]}
    for mode in ['true','false']:
        commands['get_hostfxr_path_explicit_'+mode]=[str(run/'nativehost.exe'),'get_hostfxr_path',mode,'nullptr',str(layout)]
    for name,command in commands.items():
        result=subprocess.run(command,env=env,text=True,encoding='utf-8',capture_output=True,timeout=30)
        (out/(name+'.log')).write_text(result.stdout+result.stderr,encoding='utf-8')
        assert result.returncode==0,(name,result.returncode,result.stdout,result.stderr)
        upstream[name]=dict(command=command,exit_code=0)
record=dict(native_product_version='10.0.5',native_file_versions=versions,nethost_resolved_path=buffer.value,
            environment_info=seen[0],unicode_path_and_explicit_root=True,apphost_cet_compatibility=True,
            original_native_test_cases=upstream,
            host_files_sha256={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in host.iterdir() if p.is_file()},
            full_runtime10_built=False,full_sdk10_built=False)
(out/'results.json').write_text(json.dumps(record,indent=2)+'\n')
print('PASS source10 native host versions, Unicode muxer path, nethost sizing/path API and hostfxr environment callback; no runtime/SDK10 claimed')
