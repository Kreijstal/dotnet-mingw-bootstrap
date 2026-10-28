"""Write native host headers from the original Arcade-calculated source version."""
from pathlib import Path
import hashlib,json,sys
root=Path(sys.argv[1]).resolve();version=(root/'file-version.txt').read_text().strip()
assert version=='10.0.526.11800'
major,minor,build,revision=version.split('.')
source=root/'r';obj=source/'artifacts/obj';obj.mkdir(parents=True,exist_ok=True)
runtime='''#define RuntimeAssemblyMajorVersion 10
#define RuntimeAssemblyMinorVersion 0
#define RuntimeFileMajorVersion %s
#define RuntimeFileMinorVersion %s
#define RuntimeFileBuildVersion %s
#define RuntimeFileRevisionVersion %s
#define RuntimeProductMajorVersion 10
#define RuntimeProductMinorVersion 0
#define RuntimeProductPatchVersion 5
#define RuntimeProductVersion 10.0.5
'''%(major,minor,build,revision)
(obj/'runtime_version.h').write_text(runtime)
def resource(description):
    return '''#include <windows.h>
#define VER_COMPANYNAME_STR ".NET Foundation (source-built MSYS2 port)"
#define VER_FILEDESCRIPTION_STR "%s"
#define VER_INTERNALNAME_STR VER_FILEDESCRIPTION_STR
#define VER_ORIGINALFILENAME_STR VER_FILEDESCRIPTION_STR
#define VER_PRODUCTNAME_STR ".NET"
#define VER_LEGALCOPYRIGHT_STR "Copyright .NET Foundation and Contributors."
#define VER_PRODUCTVERSION %s,%s,%s,%s
#define VER_PRODUCTVERSION_STR "10.0.5"
#define VER_FILEVERSION %s,%s,%s,%s
#define VER_FILEVERSION_STR "%s"
#define VER_DEBUG 0
'''%(description,major,minor,build,revision,major,minor,build,revision,version)
(obj/'_version.h').write_text(resource('.NET10 source host'))
for name in ['dotnet','hostfxr','hostpolicy','comhost','ijwhost','nethost']:
    folder=root/'host-resources'/name;folder.mkdir(parents=True,exist_ok=True)
    (folder/'version_info.h').write_text(resource('.NET '+name+' source10.0.5'))
record={p.relative_to(root).as_posix():hashlib.sha256(p.read_bytes()).hexdigest()
        for p in [obj/'runtime_version.h',obj/'_version.h',*sorted((root/'host-resources').glob('*/version_info.h'))]}
(root/'native-version-inputs.json').write_text(json.dumps(dict(product_version='10.0.5',source_file_version=version,generated_header_sha256=record),indent=2)+'\n')
print('PASS native host product10.0.5 and original Arcade source file version',version)
