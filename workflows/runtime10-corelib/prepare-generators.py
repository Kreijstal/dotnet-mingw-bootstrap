"""Prepare complete original .NET10 generator projects for a private net8 bootstrap."""
from pathlib import Path
import hashlib
import json
import os
import re
import subprocess
import sys
import xml.etree.ElementTree as E

root = Path(sys.argv[1]).resolve()
source = root / 'r'
out = root / 'corelib10-1/generators'
assert not out.exists(), out
out.mkdir(parents=True)
native = lambda p: subprocess.check_output(['cygpath', '-am', str(p)], text=True).strip()
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
libs = source / 'src/libraries'
common = libs / 'Common/src'
core = libs / 'System.Private.CoreLib/src'
interop = libs / 'System.Runtime.InteropServices/gen'
projects = {
    'Microsoft.Interop.SourceGeneration': interop / 'Microsoft.Interop.SourceGeneration/Microsoft.Interop.SourceGeneration.csproj',
    'Microsoft.Interop.LibraryImportGenerator': interop / 'LibraryImportGenerator/LibraryImportGenerator.csproj',
    'Microsoft.Interop.ComInterfaceGenerator': interop / 'ComInterfaceGenerator/ComInterfaceGenerator.csproj',
    'System.Private.CoreLib.Generators': libs / 'System.Private.CoreLib/gen/System.Private.CoreLib.Generators.csproj'}
compiler = root / 'cdac-bootstrap5/compiler'
assert (compiler / 'CompilerHost.dll').is_file()
records = {}
for name, project in projects.items():
    folder = out / name
    folder.mkdir()
    tree = E.parse(project)
    tree.getroot().attrib.pop('Sdk')
    explicit = []
    package_refs = []
    for group in tree.getroot().findall('ItemGroup'):
        for item in list(group):
            if item.tag == 'Compile':
                value = item.get('Include').replace('\\', '/')
                value = value.replace('$(CoreLibSharedDir)', str(core) + '/')
                value = value.replace('$(CommonPath)', str(common) + '/')
                value = value.replace('$(LibrariesProjectRoot)', str(libs) + '/')
                assert '$(' not in value, value
                path = Path(value) if Path(value).is_absolute() else project.parent / value
                assert path.is_file(), path
                explicit.append(path.resolve())
                group.remove(item)
            elif item.tag == 'PackageReference':
                package_refs.append(dict(id=item.get('Include'), version=item.get('Version')))
                group.remove(item)
            elif item.tag == 'ProjectReference':
                original = (project.parent / item.get('Include').replace('\\', '/')).resolve()
                peer = next(key for key, value in projects.items() if value == original)
                item.set('Include', '../' + peer + '/Build.csproj')
    own = [] if name == 'System.Private.CoreLib.Generators' else sorted(project.parent.rglob('*.cs'))
    selected = own + explicit
    assert len(selected) == len(set(selected)), name
    E.indent(tree)
    tree.write(folder / 'upstream.proj', encoding='utf-8', xml_declaration=True)
    wrapper = E.Element('Project', Sdk='Microsoft.NET.Sdk')
    props = E.SubElement(wrapper, 'PropertyGroup')
    for key, value in dict(EnableDefaultItems='false', ImportDirectoryBuildProps='false',
                           ImportDirectoryBuildTargets='false').items():
        E.SubElement(props, key).text = value
    E.SubElement(wrapper, 'Import', Project='upstream.proj')
    props = E.SubElement(wrapper, 'PropertyGroup')
    for key, value in dict(TargetFramework='net8.0', AssemblyName=name, Nullable='enable',
                           LangVersion='preview', EnableDefaultItems='false',
                           EnableNETAnalyzers='false', NuGetAudit='false',
                           IsPackable='false', UseSharedCompilation='false',
                           CscToolPath=native(compiler), CscToolExe='csc50.cmd',
                           CopyLocalLockFileAssemblies='true', Version='10.0.5').items():
        E.SubElement(props, key).text = value
    items = E.SubElement(wrapper, 'ItemGroup')
    for path in selected:
        E.SubElement(items, 'Compile', Include=native(path))
    for assembly in ['Microsoft.CodeAnalysis', 'Microsoft.CodeAnalysis.CSharp']:
        ref = E.SubElement(items, 'Reference', Include=assembly)
        E.SubElement(ref, 'HintPath').text = native(compiler / (assembly + '.dll'))
        E.SubElement(ref, 'Private').text = 'true'
    if any('Workspaces' in item['id'] for item in package_refs):
        E.SubElement(items, 'PackageReference', Include='Microsoft.CodeAnalysis.CSharp.Workspaces', Version='[4.9.2]')
        for package in ['Microsoft.CodeAnalysis.Common', 'Microsoft.CodeAnalysis.CSharp']:
            E.SubElement(items, 'PackageReference', Include=package, Version='[4.9.2]',
                         ExcludeAssets='compile;runtime')
    generated = []
    if name != 'System.Private.CoreLib.Generators':
        resource = interop / 'Common/Resources/Strings.resx'
        anchor = 'FxResources.' + name + '.SR'
        E.SubElement(items, 'EmbeddedResource', Include=native(resource), LogicalName=anchor + '.resources')
        E.SubElement(items, 'Compile', Include=native(common / 'System/SR.cs'))
        code = ['namespace ' + anchor.rsplit('.', 1)[0] + ' { internal static class SR {} }',
                'namespace System { internal static partial class SR {',
                'internal static global::System.Resources.ResourceManager ResourceManager { get; } = new global::System.Resources.ResourceManager("' + anchor + '", typeof(' + anchor + ').Assembly);']
        for item in E.parse(resource).iter('data'):
            key = item.get('name')
            assert key.isidentifier(), key
            code.append('internal static string ' + key + ' => GetResourceString("' + key + '");')
        code.append('}}')
        (folder / 'SR.Generated.cs').write_text('\n'.join(code) + '\n')
        E.SubElement(items, 'Compile', Include='SR.Generated.cs')
        generated.append('SR.Generated.cs')
    capture = E.SubElement(wrapper, 'Target', Name='CaptureBootstrapCompilerInputs', BeforeTargets='CoreCompile')
    for kind in ['Compile', 'ReferencePath', 'Analyzer']:
        E.SubElement(capture, 'WriteLinesToFile', File='$(IntermediateOutputPath)' + kind + '.txt',
                     Lines='@(' + kind + "->'%(FullPath)')", Overwrite='true', Encoding='UTF-8')
    E.indent(wrapper)
    E.ElementTree(wrapper).write(folder / 'Build.csproj', encoding='utf-8', xml_declaration=True)
    records[name] = dict(original_project=project.relative_to(source).as_posix(),
                         original_project_sha256=sha(project),
                         original_cs={p.relative_to(source).as_posix(): sha(p) for p in selected},
                         production_cs_changes=0, original_package_references=package_refs,
                         generated_sources=generated, bootstrap_framework='net8.0',
                         bootstrap_roslyn_core='source5.0.0',
                         bootstrap_workspaces='source4.9.2; declared4.14.0 closure not yet packaged')
(out / 'NuGet.config').write_text('<configuration><packageSources><clear/><add key="installed-source" value="' + native(Path('/clang64/lib/dotnet/library-packs')) + '"/></packageSources></configuration>\n')
(out / 'source-inputs.json').write_text(json.dumps(records, indent=2) + '\n')
print('Prepared complete original source10 generators:', {name: len(item['original_cs']) for name, item in records.items()})
