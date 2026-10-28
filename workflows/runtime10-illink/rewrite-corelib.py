"""Run the original runtime library-build ILLink policy on raw CoreLib10."""
from pathlib import Path
import hashlib
import json
import shutil
import subprocess
import sys
import xml.etree.ElementTree as E

root, tool, out = (Path(p).resolve() for p in sys.argv[1:])
assert not out.exists(), out
out.mkdir(parents=True)
source = root / 'r'
raw = root / 'corelib10-1/corelib/lib/System.Private.CoreLib.dll'
assert raw.is_file()
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
native_root = subprocess.check_output(['cygpath', '-am', str(root)], text=True).strip()
def native(path):
    return native_root + '/' + Path(path).resolve().relative_to(root).as_posix()
def posix(path):
    path = path.replace('\\', '/')
    assert path.lower().startswith(native_root.lower() + '/')
    return root / path[len(native_root) + 1:]

# Import both the already evaluated original project and the unmodified upstream
# library-trimming targets. This includes the CoreCLR-specific suppression XML.
query = E.Element('Project')
E.SubElement(query, 'Import', Project=native(root / 'corelib10-1/corelib/Evaluation.proj'))
E.SubElement(query, 'Import', Project=native(source / 'eng/illink.targets'))
E.ElementTree(query).write(out / 'Evaluation.proj', encoding='utf-8', xml_declaration=True)
dotnet = '/clang64/lib/dotnet/dotnet.exe'
command = [dotnet, 'msbuild', native(out / 'Evaluation.proj'), '-nologo',
           '-getItem:ILLinkDescriptorsLibraryBuildXml,ILLinkSuppressionsLibraryBuildXml,ReferencePathWithRefAssemblies',
           '-getProperty:ILLinkSubstitutionsLibraryBuildXml,ILLinkDisableIPConstProp,ILLinkRewritePDBs,DeterministicSourcePaths,LinkerNoWarn']
evaluation = subprocess.run(command, text=True, capture_output=True)
(out / 'evaluation.json').write_text(evaluation.stdout)
(out / 'evaluation.stderr').write_text(evaluation.stderr)
assert evaluation.returncode == 0, evaluation.stdout + evaluation.stderr
evaluated = json.loads(evaluation.stdout)
props, items = evaluated['Properties'], evaluated['Items']
inputs = out / 'PreTrim'
inputs.mkdir()
shutil.copy2(raw, inputs / raw.name)
symbols = raw.with_suffix('.pdb')
if symbols.exists():
    shutil.copy2(symbols, inputs / symbols.name)
output = out / 'linked'
args = ['-a', native(inputs / raw.name), 'library', '-out', native(output),
        '--ignore-link-attributes', 'true', '--skip-unresolved', 'true',
        '--trim-mode', 'skip', '--action', 'skip', '--action', 'link', 'System.Private.CoreLib']
if props['ILLinkRewritePDBs'] == 'true' and symbols.exists():
    args += ['-b', 'true']
    if props['DeterministicSourcePaths'] == 'true':
        args += ['--preserve-symbol-paths']
xml_inputs = []
for kind, option in [('ILLinkDescriptorsLibraryBuildXml', '-x'),
                     ('ILLinkSuppressionsLibraryBuildXml', '--link-attributes')]:
    for item in items[kind]:
        path = posix(item['FullPath'])
        assert path.is_file(), path
        args += [option, native(path)]
        xml_inputs.append(path)
if props['ILLinkSubstitutionsLibraryBuildXml']:
    path = posix(props['ILLinkSubstitutionsLibraryBuildXml'])
    args += ['--substitutions', native(path)]
    xml_inputs.append(path)
args += ['--nowarn', ';'.join(filter(None, [props['LinkerNoWarn'], 'IL2008;IL2009;IL2121;IL2025;IL2035']))]
if props['ILLinkDisableIPConstProp'] == 'true':
    args += ['--disable-opt', 'ipconstprop']
for directory in dict.fromkeys(str(posix(item['FullPath']).parent) for item in items['ReferencePathWithRefAssemblies']):
    args += ['-d', native(Path(directory))]
linker = tool / 'illink/bin/Release/net8.0/illink.dll'
assert linker.is_file(), linker
(out / 'illink.rsp').write_text('\n'.join('"' + arg + '"' for arg in args) + '\n')
command = [dotnet, native(linker), '@' + native(out / 'illink.rsp')]
record = dict(command=command, arguments=args, raw_corelib_sha256=sha(raw),
              linker_sha256=sha(linker), upstream_targets_sha256=sha(source / 'eng/illink.targets'),
              library_build_xml_sha256={p.relative_to(source).as_posix(): sha(p) for p in xml_inputs},
              original_library_policy=True, fcall_helper_used=False,
              full_dependency_goal_complete=False)
(out / 'rewrite-inputs.json').write_text(json.dumps(record, indent=2) + '\n')
with (out / 'rewrite.log').open('w') as log:
    result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT)
(out / 'rewrite.exit').write_text(str(result.returncode) + '\n')
assert result.returncode == 0, (result.returncode, out / 'rewrite.log')
linked = output / raw.name
assert linked.is_file() and sha(linked) != sha(raw)
record.update(rewrite_exit_code=0, linked_corelib_sha256=sha(linked),
              full_original_illink_rewrite_completed=True)
(out / 'rewrite-results.json').write_text(json.dumps(record, indent=2) + '\n')
print('PASS original library-root ILLink rewrite; metadata and managed execution remain to be checked')
