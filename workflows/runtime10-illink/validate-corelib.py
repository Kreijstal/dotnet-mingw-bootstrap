"""Check the real ILLink output, then execute it with the source10 native CLR."""
from pathlib import Path
import hashlib
import json
import subprocess
import sys
import xml.etree.ElementTree as E

root, tool, rewrite = (Path(p).resolve() for p in sys.argv[1:])
here = Path(__file__).resolve().parent
native_root = subprocess.check_output(['cygpath', '-am', str(root)], text=True).strip()
def native(path):
    return native_root + '/' + Path(path).resolve().relative_to(root).as_posix()
dotnet = '/clang64/lib/dotnet/dotnet.exe'
raw = root / 'corelib10-1/corelib/lib/System.Private.CoreLib.dll'
linked = rewrite / 'linked/System.Private.CoreLib.dll'
assert linked.is_file()
check = rewrite / 'audit'
check.mkdir()
tree = E.Element('Project', Sdk='Microsoft.NET.Sdk')
pg = E.SubElement(tree, 'PropertyGroup')
for k, v in dict(TargetFramework='net8.0', AssemblyName='AuditCoreLib10', OutputType='Exe',
                 UseAppHost='false', EnableDefaultItems='false', NuGetAudit='false',
                 EnableNETAnalyzers='false', Nullable='enable').items():
    E.SubElement(pg, k).text = v
ig = E.SubElement(tree, 'ItemGroup')
E.SubElement(ig, 'Compile', Include=native(here / 'inputs/AuditCoreLib.cs'))
E.SubElement(ig, 'ProjectReference', Include=native(tool / 'Mono.Cecil/Build.csproj'))
E.ElementTree(tree).write(check / 'Build.csproj', encoding='utf-8', xml_declaration=True)
def run(command, log):
    with log.open('w') as output:
        result = subprocess.run(command, stdout=output, stderr=subprocess.STDOUT)
    assert result.returncode == 0, (command, result.returncode, log)
run([dotnet, 'build', native(check / 'Build.csproj'), '-c', 'Release', '--configfile',
     native(tool / 'NuGet.config'), '-p:UseSharedCompilation=false', '-v:minimal'], rewrite / 'audit-build.log')
run([dotnet, native(check / 'bin/Release/net8.0/AuditCoreLib10.dll'), native(raw), native(linked)],
    rewrite / 'api-audit.json')
audit = json.loads((rewrite / 'api-audit.json').read_text())
assert audit['public_api_unchanged'] and audit['internal_calls_with_body'] == 0

managed = rewrite / 'managed-check'
corelib_workflow = root / 'corelib-workflow'
run(['/usr/bin/python3', str(corelib_workflow / 'prepare-managed-check.py'), str(root), str(managed)],
    rewrite / 'prepare-managed.log')
layout = managed / 'native CLR10 λ'
# Replace only the staged raw CoreLib, before compiling the probe. No fcall RVA
# helper or post-link metadata modification is performed anywhere in this check.
import shutil
shutil.copy2(linked, layout / linked.name)
inputs = json.loads((managed / 'layout-inputs.json').read_text())
inputs['original_files_sha256'][linked.name] = hashlib.sha256(linked.read_bytes()).hexdigest()
inputs.update(corelib_fcall_rva_rewrite_required=False, full_original_illink_rewrite_completed=True)
(managed / 'layout-inputs.json').write_text(json.dumps(inputs, indent=2) + '\n')
run([dotnet, native(root / 'cdac-bootstrap5/compiler/CompilerHost.dll'), '-noconfig',
     '@' + native(managed / 'probe.rsp')], managed / 'probe-build.log')
run(['/clang64/bin/python.exe', native(corelib_workflow / 'check-managed.py'), native(managed)],
    rewrite / 'managed-run.log')
managed_results = json.loads((managed / 'results.json').read_text())
assert managed_results['all_four_managed_cases_passed']
assert managed_results['tested_files_sha256'][linked.name] == hashlib.sha256(linked.read_bytes()).hexdigest()
result = dict(api_audit=audit, managed_results=managed_results,
              original_illink_output_executed_without_modification=True,
              full_original_illink_rewrite_completed=True, full_runtime10_built=False,
              full_sdk10_built=False, full_dependency_goal_complete=False)
(rewrite / 'validation-results.json').write_text(json.dumps(result, indent=2) + '\n')
print('PASS original ILLink CoreLib: public API/resources/PDB/InternalCall audit and four managed CLR10 probes')
