"""Generate CoreLib10 resources, original runtime-root task inputs and csc response."""
from pathlib import Path
import hashlib
import json
import subprocess
import sys
import xml.etree.ElementTree as E

root = Path(sys.argv[1]).resolve()
here = Path(__file__).resolve().parent
out = root / 'corelib10-1/corelib'
source = root / 'r'
obj = out / 'obj'
assert not obj.exists(), obj
obj.mkdir()
(out / 'lib').mkdir()
for assembly, generator in [
        ('Microsoft.Interop.LibraryImportGenerator', 'Microsoft.Interop.LibraryImportGenerator'),
        ('Microsoft.Interop.ComInterfaceGenerator', 'Microsoft.Interop.ComInterfaceGenerator'),
        ('System.Private.CoreLib.Generators', 'Generators.EventSourceGenerator'),
        ('System.Private.CoreLib.Generators', 'Generators.NativeRuntimeEventSourceGenerator'),
        ('System.Private.CoreLib.Generators', 'Generators.ProductVersionInfoGenerator')]:
    (obj / 'generated' / assembly / generator).mkdir(parents=True, exist_ok=True)
native_root = subprocess.check_output(['cygpath', '-am', str(root)], text=True).strip()
def native(path):
    path = Path(path).resolve()
    if path.is_relative_to(root):
        return native_root + '/' + path.relative_to(root).as_posix()
    return subprocess.check_output(['cygpath', '-am', str(path)], text=True).strip()
def posix(path):
    path = path.replace('\\', '/')
    assert path.lower().startswith(native_root.lower() + '/'), path
    return root / path[len(native_root) + 1:]
record = json.loads((out / 'source-inputs.json').read_text())
evaluation = json.loads((out / 'evaluation.json').read_text())
defines = record['direct_compiler_defines']
resource = source / 'src/libraries/System.Private.CoreLib/src/Resources/Strings.resx'
code = ['#nullable disable', 'namespace System { internal static partial class SR {',
        'private static global::System.Resources.ResourceManager s_resourceManager;',
        'internal static global::System.Resources.ResourceManager ResourceManager => s_resourceManager ?? (s_resourceManager = new global::System.Resources.ResourceManager("System.Private.CoreLib.Strings", typeof(SR).Assembly));']
entries = list(E.parse(resource).iter('data'))
for item in entries:
    key = item.get('name')
    assert key.isidentifier() and not item.get('type') and not item.get('mimetype'), key
    code.append('internal static string @' + key + ' => GetResourceString("' + key + '");')
code.append('}}')
(obj / 'SR.g.cs').write_text('\n'.join(code) + '\n')
version = (root / 'file-version.txt').read_text().strip()
assert version == '10.0.526.11800'
attributes = dict(AssemblyCompany='Microsoft Corporation', AssemblyDescription='System.Private.CoreLib',
                  AssemblyProduct='Microsoft .NET', AssemblyTitle='System.Private.CoreLib',
                  AssemblyVersion='10.0.0.0', AssemblyFileVersion=version,
                  AssemblyInformationalVersion='10.0.5+081d220c0a773ffb7c6bea6b48727833576a65ef')
code = ['[assembly: System.Reflection.' + key + '(' + json.dumps(value) + ')]' for key, value in attributes.items()]
for key, value in [('.NETFrameworkAssembly', ''), ('Serviceable', 'True'), ('IsTrimmable', 'True')]:
    code.append('[assembly: System.Reflection.AssemblyMetadata(' + json.dumps(key) + ', ' + json.dumps(value) + ')]')
code.append('[assembly: System.Runtime.Versioning.TargetFramework(".NETCoreApp,Version=v10.0", FrameworkDisplayName=".NET10.0")]')
(obj / 'AssemblyInfo.g.cs').write_text('\n'.join(code) + '\n')
(obj / 'build.globalconfig').write_text('is_global = true\nbuild_property.TargetFrameworkIdentifier = .NETCoreApp\nbuild_property.TargetFrameworkVersion = v10.0\nbuild_property.LibraryImportGenerator_UseMarshalType = true\nbuild_property.InformationalVersion = 10.0.5+081d220c0a773ffb7c6bea6b48727833576a65ef\n')

illink = source / 'src/coreclr/System.Private.CoreLib/src/ILLink'
shared = source / 'src/libraries/System.Private.CoreLib/src/ILLink'
descriptor_inputs = [illink / 'ILLink.Descriptors.Shared.xml', illink / 'ILLink.Descriptors.Windows.xml', shared / 'ILLink.Descriptors.Shared.xml']
def combine(path, inputs):
    assert inputs and all(p.is_file() for p in inputs)
    tree = E.Element('linker')
    for p in inputs:
        for element in E.parse(p).getroot():
            tree.append(element)
    E.indent(tree)
    E.ElementTree(tree).write(path, encoding='utf-8', xml_declaration=True)
combine(obj / 'ILLink.Descriptors.Combined.xml', descriptor_inputs)
resources = [('ILLink.Descriptors.xml', obj / 'ILLink.Descriptors.xml')]
for kind, name in [('ILLinkSubstitutionsXmls', 'ILLink.Substitutions.xml'), ('ILLinkLinkAttributesXmls', 'ILLink.LinkAttributes.xml')]:
    inputs = [posix(item['FullPath']) for item in evaluation['Items'][kind]]
    combine(obj / name, inputs)
    resources.append((name, obj / name))
task_project = E.Element('Project', Sdk='Microsoft.NET.Sdk')
properties = E.SubElement(task_project, 'PropertyGroup')
for name, value in dict(TargetFramework='net8.0', EnableDefaultItems='false',
                        EnableNETAnalyzers='false', NuGetAudit='false').items():
    E.SubElement(properties, name).text = value
E.SubElement(task_project, 'UsingTask', TaskName='ILLink.Tasks.CreateRuntimeRootILLinkDescriptorFile',
             AssemblyFile=native(out / 'tools/roots/bin/Release/net8.0/CoreLib10.RuntimeRoots.dll'))
target = E.SubElement(task_project, 'Target', Name='ResourcesAndRoots')
E.SubElement(target, 'GenerateResource', Sources=native(resource),
             OutputResources=native(obj / 'System.Private.CoreLib.Strings.resources'),
             ExecuteAsTool='false')
E.SubElement(target, 'ILLink.Tasks.CreateRuntimeRootILLinkDescriptorFile',
             NamespaceFilePath=native(source / 'src/coreclr/vm/namespace.h'),
             MscorlibFilePath=native(source / 'src/coreclr/vm/corelib.h'),
             CortypeFilePath=native(source / 'src/coreclr/inc/cortypeinfo.h'),
             RexcepFilePath=native(source / 'src/coreclr/vm/rexcep.h'),
             ILLinkTrimXmlFilePath=native(obj / 'ILLink.Descriptors.Combined.xml'),
             DefineConstants=';'.join(defines + ['FOR_ILLINK']),
             RuntimeRootDescriptorFilePath=native(obj / 'ILLink.Descriptors.xml'))
E.indent(task_project)
E.ElementTree(task_project).write(out / 'ResourcesAndRoots.proj', encoding='utf-8', xml_declaration=True)

args = ['/nostdlib+', '/target:library', '/optimize+', '/deterministic+',
        '/unsafe+', '/nullable:enable', '/debug:portable', '/langversion:preview',
        '/runtimemetadataversion:v4.0.30319', '/publicsign+',
        '/keyfile:"' + native(out / 'SilverlightPlatform.pubkey') + '"',
        '/out:"' + native(out / 'lib/System.Private.CoreLib.dll') + '"',
        '/define:' + ';'.join(defines),
        '/analyzerconfig:"' + native(obj / 'build.globalconfig') + '"',
        '/generatedfilesout:"' + native(obj / 'generated') + '"',
        '/resource:"' + native(obj / 'System.Private.CoreLib.Strings.resources') + '",System.Private.CoreLib.Strings.resources']
for name, path in resources:
    args.append('/resource:"' + native(path) + '",' + name)
generators = root / 'corelib10-1/generators'
for name in ['Microsoft.Interop.SourceGeneration', 'Microsoft.Interop.LibraryImportGenerator', 'Microsoft.Interop.ComInterfaceGenerator', 'System.Private.CoreLib.Generators']:
    path = generators / name / 'bin/Release/net8.0' / (name + '.dll')
    assert path.is_file(), path
    args.append('/analyzer:"' + native(path) + '"')
for item in evaluation['Items']['AdditionalFiles']:
    args.append('/additionalfile:"' + item['FullPath'] + '"')
args.extend('"' + item['FullPath'] + '"' for item in evaluation['Items']['Compile'])
args.extend('"' + native(obj / name) + '"' for name in ['SR.g.cs', 'AssemblyInfo.g.cs'])
(out / 'corelib.rsp').write_text('\n'.join(args) + '\n')
record = dict(original_corelib_cs=len(evaluation['Items']['Compile']), resource_strings=len(entries),
              original_runtime_root_descriptor_inputs=[native(p) for p in descriptor_inputs],
              response_sha256=hashlib.sha256((out / 'corelib.rsp').read_bytes()).hexdigest(),
              generated_sources_sha256={name: hashlib.sha256((obj / name).read_bytes()).hexdigest() for name in ['SR.g.cs', 'AssemblyInfo.g.cs']},
              full_original_illink_rewrite_completed=False, corelib_compiled=False,
              managed_execution_verified=False, full_dependency_goal_complete=False)
(out / 'compile-inputs.json').write_text(json.dumps(record, indent=2) + '\n')
print('PASS CoreLib10 direct compiler inputs:', record['original_corelib_cs'], 'original C# sources,', len(entries), 'strings, original generators and runtime-root task')
