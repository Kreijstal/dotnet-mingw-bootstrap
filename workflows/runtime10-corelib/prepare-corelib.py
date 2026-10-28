"""Select the complete original Windows CoreLib10 project and private build tools."""
from pathlib import Path
import hashlib
import json
import os
import subprocess
import sys
import xml.etree.ElementTree as E

root = Path(sys.argv[1]).resolve()
source = root / 'r'
out = root / 'corelib10-1/corelib'
assert not out.exists(), out
out.mkdir(parents=True)
here = Path(__file__).resolve().parent
native_root = subprocess.check_output(['cygpath', '-am', str(root)], text=True).strip()
def native(path):
    path = Path(path).resolve()
    if path.is_relative_to(root):
        return native_root + '/' + path.relative_to(root).as_posix()
    return subprocess.check_output(['cygpath', '-am', str(path)], text=True).strip()
def posix(path):
    path = path.replace('\\', '/')
    if path.lower().startswith(native_root.lower() + '/'):
        return root / path[len(native_root) + 1:]
    return Path(subprocess.check_output(['cygpath', '-u', path], text=True).strip()).resolve()
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
libs = source / 'src/libraries'
project = source / 'src/coreclr/System.Private.CoreLib/System.Private.CoreLib.csproj'
text = project.read_text(encoding='utf-8-sig').replace('$(MSBuildThisFileDirectory)', native(project.parent) + '/')
tree = E.ElementTree(E.fromstring(text))
tree.getroot().attrib.pop('Sdk')
rebased = []
for item in tree.iter():
    attribute = 'Project' if item.tag == 'Import' else 'Include' if item.tag in {'Compile', 'ProjectReference'} else None
    if not attribute or not item.get(attribute):
        continue
    value = item.get(attribute).replace('\\', '/')
    if '$(' in value or value.startswith('/') or (len(value) > 1 and value[1] == ':'):
        continue
    absolute = native((project.parent / value).resolve())
    item.set(attribute, absolute)
    rebased.append(dict(kind=item.tag, original=value, absolute=absolute))
E.indent(tree)
tree.write(out / 'upstream.proj', encoding='utf-8', xml_declaration=True)
wrapper = E.Element('Project', Sdk='Microsoft.NET.Sdk')
props = E.SubElement(wrapper, 'PropertyGroup')
values = dict(ImportDirectoryBuildProps='false', ImportDirectoryBuildTargets='false',
              EnableDefaultItems='false', NetCoreAppCurrent='net10.0',
              TargetOS='windows', TargetsWindows='true', Platform='x64',
              TargetArchitecture='x64', Configuration='Release',
              LibrariesProjectRoot=native(libs) + '/',
              CoreLibSharedDir=native(libs / 'System.Private.CoreLib/src') + '/',
              CoreClrProjectRoot=native(source / 'src/coreclr') + '/')
for key, value in values.items():
    E.SubElement(props, key).text = value
E.SubElement(wrapper, 'Import', Project='upstream.proj')
E.indent(wrapper)
E.ElementTree(wrapper).write(out / 'Evaluation.proj', encoding='utf-8', xml_declaration=True)
env = dict(os.environ, DOTNET_CLI_TELEMETRY_OPTOUT='1', MSBuildEnableWorkloadResolver='false')
command = ['/clang64/lib/dotnet/dotnet.exe', 'msbuild', native(out / 'Evaluation.proj'), '-nologo',
           '-getItem:Compile,EmbeddedResource,AdditionalFiles,ILLinkDescriptorsLibraryBuildXml,ILLinkSubstitutionsXmls,ILLinkLinkAttributesXmls',
           '-getProperty:DefineConstants,FeatureCominterop,FeatureComWrappers,FeaturePerfTracing,TargetFramework']
result = subprocess.run(command, cwd=out, env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
(out / 'evaluation.json').write_text(result.stdout)
(out / 'evaluation.err').write_text(result.stderr)
assert result.returncode == 0, result.stdout + result.stderr
evaluation = json.loads(result.stdout)
inputs = [posix(item['FullPath']) for item in evaluation['Items']['Compile']]
assert len(inputs) == len(set(inputs)) and all(path.is_file() for path in inputs)
assert all(path.is_relative_to(source) for path in inputs)
properties = evaluation['Properties']
assert all(properties[key] == 'true' for key in ['FeatureCominterop', 'FeatureComWrappers', 'FeaturePerfTracing'])
assert properties['TargetFramework'] == 'net10.0'
defines = properties['DefineConstants'].split(';')
# Direct csc invocation: these are the SDK10 implicit framework symbols,
# derived by Microsoft.NET.Sdk.BeforeCommon.targets for net10.0.
defines += ['NET', 'NET10_0', *['NET' + str(v) + '_0_OR_GREATER' for v in range(5, 11)],
            *['NETCOREAPP' + v + '_OR_GREATER' for v in ['1_0', '1_1', '2_0', '2_1', '2_2', '3_0', '3_1']]]
defines = list(dict.fromkeys(defines))
(out / 'compiler-inputs.txt').write_text('\n'.join(native(path) for path in inputs) + '\n')
(out / 'defines.json').write_text(json.dumps(defines, indent=2) + '\n')
key = bytes.fromhex((here / 'inputs/SilverlightPlatform.pubkey.hex').read_text())
assert hashlib.sha1(key).digest()[-8:][::-1].hex() == '7cec85d7bea7798e'
(out / 'SilverlightPlatform.pubkey').write_bytes(key)

tools = out / 'tools'
tools.mkdir()
sdk = Path('/clang64/lib/dotnet/sdk/8.0.131')
def tool_project(folder, assembly, inputs, exe=False):
    directory = tools / folder
    directory.mkdir()
    p = E.Element('Project', Sdk='Microsoft.NET.Sdk')
    props = E.SubElement(p, 'PropertyGroup')
    values = dict(TargetFramework='net8.0', EnableDefaultItems='false',
                  EnableNETAnalyzers='false', NuGetAudit='false', AssemblyName=assembly,
                  UseSharedCompilation='false', Nullable='disable')
    if exe:
        values['OutputType'] = 'Exe'
    for k, v in values.items():
        E.SubElement(props, k).text = v
    items = E.SubElement(p, 'ItemGroup')
    for path in inputs:
        E.SubElement(items, 'Compile', Include=native(path))
    return p, items, directory
task = source / 'src/tools/illink/src/ILLink.Tasks/CreateRuntimeRootDescriptorFile.cs'
p, items, directory = tool_project('roots', 'CoreLib10.RuntimeRoots', [task])
for name in ['Microsoft.Build.Framework', 'Microsoft.Build.Utilities.Core']:
    reference = E.SubElement(items, 'Reference', Include=name)
    E.SubElement(reference, 'HintPath').text = native(sdk / (name + '.dll'))
    E.SubElement(reference, 'Private').text = 'false'
E.indent(p)
E.ElementTree(p).write(directory / 'Build.csproj', encoding='utf-8', xml_declaration=True)
p, items, directory = tool_project('fcall', 'FcallRva', [here / 'inputs/fcallrva.cs'], exe=True)
E.indent(p)
E.ElementTree(p).write(directory / 'Build.csproj', encoding='utf-8', xml_declaration=True)
p, items, directory = tool_project('metadata', 'CoreLib10.MetadataCheck', [here / 'inputs/MetadataCheck.cs'], exe=True)
E.indent(p)
E.ElementTree(p).write(directory / 'Build.csproj', encoding='utf-8', xml_declaration=True)
(tools / 'NuGet.config').write_text('<configuration><packageSources><clear/></packageSources></configuration>\n')
record = dict(source_runtime_version='10.0.5', source_runtime_commit='081d220c0a773ffb7c6bea6b48727833576a65ef',
              original_project_sha256=sha(project), original_cs={p.relative_to(source).as_posix(): sha(p) for p in inputs},
              production_cs_changes=0, rebased_project_paths=rebased, evaluated_properties=properties,
              direct_compiler_defines=defines, runtime_root_task_source_sha256=sha(task),
              private_bootstrap_tools_framework='net8.0', corelib_framework='net10.0',
              full_original_illink_rewrite_completed=False, managed_execution_verified=False,
              full_dependency_goal_complete=False)
(out / 'source-inputs.json').write_text(json.dumps(record, indent=2) + '\n')
print('PASS complete original CoreLib10 MSBuild selection:', len(inputs), 'C# inputs; Windows COM/IJW/tracing retained')
