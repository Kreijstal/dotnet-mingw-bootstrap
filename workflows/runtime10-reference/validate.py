"""Audit all original contracts and compile a consumer requiring .NET10 APIs."""
from pathlib import Path
import hashlib
import json
import subprocess
import sys
import xml.etree.ElementTree as E

root, out = (Path(p).resolve() for p in sys.argv[1:])
here = Path(__file__).resolve().parent
native_root = subprocess.check_output(['cygpath', '-am', str(root)], text=True).strip()
def native(path):
    path = Path(path).resolve()
    if path.is_relative_to(root):
        return native_root + '/' + path.relative_to(root).as_posix()
    return subprocess.check_output(['cygpath', '-am', str(path)], text=True).strip()
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
status = json.loads((out / 'build-status.json').read_text())
assert status['complete_declared_reference_graph']
assert len(status['projects']) == 186
original_cs = {}
for name, entry in status['projects'].items():
    assert entry['exit_code'] == 0
    for path, expected in entry['source_cs'].items():
        file = root / 'r' / path if not (len(path) > 1 and path[1] == ':') else root / path.replace('\\', '/')[len(native_root) + 1:]
        assert sha(file) == expected, (name, path)
        if file.is_relative_to(root / 'r'):
            original_cs[path] = expected
    output = out / ('stubs' if entry['kind'] == 'stub' else 'ref/net10.0') / (name.removeprefix('stub:') + '.dll')
    assert sha(output) == entry['output_sha256'], name
for path, expected in json.loads((out / 'source-projects.json').read_text()).items():
    assert sha(root / 'r' / path) == expected, path
tool = out / 'audit-tool'
tool.mkdir()
tree = E.Element('Project', Sdk='Microsoft.NET.Sdk')
pg = E.SubElement(tree, 'PropertyGroup')
for k, v in dict(TargetFramework='net8.0', OutputType='Exe', AssemblyName='AuditReferencePack10',
                 UseAppHost='false', EnableDefaultItems='false', EnableNETAnalyzers='false',
                 NuGetAudit='false', Nullable='enable').items():
    E.SubElement(pg, k).text = v
ig = E.SubElement(tree, 'ItemGroup')
E.SubElement(ig, 'Compile', Include=native(here / 'inputs/AuditReferencePack.cs'))
E.ElementTree(tree).write(tool / 'Build.csproj', encoding='utf-8', xml_declaration=True)
dotnet = '/clang64/lib/dotnet/dotnet.exe'
def run(command, log, success=True):
    with log.open('w') as stream:
        result = subprocess.run(command, stdout=stream, stderr=subprocess.STDOUT)
    if success:
        assert result.returncode == 0, (command, result.returncode, log)
    return result.returncode
run([dotnet, 'build', native(tool / 'Build.csproj'), '-c', 'Release', '--configfile', native(out / 'NuGet.config'),
     '-p:UseSharedCompilation=false', '-v:minimal'], out / 'audit-build.log')
run([dotnet, native(tool / 'bin/Release/net8.0/AuditReferencePack10.dll'), native(out)], out / 'metadata-audit.json')
audit = json.loads((out / 'metadata-audit.json').read_text())
consumer = out / 'consumer'
consumer.mkdir()
cases = {}
compiler = root / 'cdac-bootstrap5/compiler/CompilerHost.dll'
for name, refs in [('net10', sorted((out / 'ref/net10.0').glob('*.dll'))),
                   ('net8-negative-control', sorted(Path('/clang64/lib/dotnet/packs/Microsoft.NETCore.App.Ref/8.0.31/ref/net8.0').glob('*.dll')))]:
    assert refs
    args = ['/nostdlib+', '/target:exe', '/optimize+', '/deterministic+', '/langversion:preview',
            '/out:"' + native(consumer / (name + '.dll')) + '"',
            *['/reference:"' + native(p) + '"' for p in refs],
            '"' + native(here / 'inputs/Net10Consumer.cs') + '"']
    rsp = consumer / (name + '.rsp')
    rsp.write_text('\n'.join(args) + '\n')
    command = [dotnet, native(compiler), '-noconfig', '@' + native(rsp)]
    code = run(command, consumer / (name + '.log'), success=name == 'net10')
    if name != 'net10':
        log = (consumer / (name + '.log')).read_text()
        assert code != 0 and 'Sequence' in log and 'AsyncEnumerable' in log, log
    cases[name] = dict(command=command, exit_code=code, references=len(refs),
                       reference_files_sha256={p.name: sha(p) for p in refs})
result = dict(original_cs_inputs=len(original_cs), all_original_source_and_output_hashes_match=True,
              metadata_audit=audit, consumer_cases=cases, consumer_executed=False,
              private_reference_graph_complete=True, reference_pack_generators_complete=False,
              full_framework10_built=False, full_sdk10_built=False, full_dependency_goal_complete=False)
(out / 'validation-results.json').write_text(json.dumps(result, indent=2) + '\n')
print('PASS 167 public contracts,19 original facade stubs and metadata graph; net10 API consumer compiles and net8 control rejects it')
