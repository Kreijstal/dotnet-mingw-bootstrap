"""Build exact source9 dependencies needed by the declared Roslyn4.14 graph."""
from pathlib import Path
import hashlib
import json
import subprocess
import sys
import tarfile
import xml.etree.ElementTree as E

root, out = (Path(p).resolve() for p in sys.argv[1:])
assert not out.exists(), out
out.mkdir(parents=True)
here = Path(__file__).resolve().parent
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
archive = root / 'runtime-v9.0.0.tar.gz'
assert sha(archive) == '32755d528ead386762fb9da054249f6151ac78ef6a92f437044838cd0ab00def'
source = out / 'runtime-source'
source.mkdir()
removed = []
with tarfile.open(archive) as tar:
    assert tar.pax_headers['comment'] == '9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3'
    for member in tar:
        if not member.isfile():
            continue
        relative = member.name.partition('/')[2]
        assert '..' not in Path(relative).parts and not Path(relative).is_absolute()
        data = tar.extractfile(member).read()
        if Path(relative).suffix.lower() in {'.dll', '.exe', '.pdb', '.nupkg', '.zip', '.mdb', '.obj', '.lib'} or data[:2] == b'MZ':
            removed.append(dict(path=relative, sha256=hashlib.sha256(data).hexdigest()))
            continue
        path = source / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
native_root = subprocess.check_output(['cygpath', '-am', str(root)], text=True).strip()
def native(path):
    return native_root + '/' + Path(path).resolve().relative_to(root).as_posix()
def posix(path):
    path = path.replace('\\', '/')
    assert path.lower().startswith(native_root.lower() + '/')
    return root / path[len(native_root) + 1:]
libs = source / 'src/libraries'
names = ['System.Collections.Immutable', 'System.Reflection.Metadata',
         'System.Composition.AttributedModel', 'System.Composition.Runtime',
         'System.Composition.Hosting', 'System.Composition.Convention',
         'System.Composition.TypedParts', 'System.IO.Pipelines']
wrappers = {}
for name in names:
    project = libs / name / 'src' / (name + '.csproj')
    folder = out / 'evaluation' / name
    folder.mkdir(parents=True)
    body = project.read_text(encoding='utf-8-sig').replace('$(MSBuildThisFileDirectory)', native(project.parent) + '/')
    tree = E.ElementTree(E.fromstring(body));tree.getroot().attrib.pop('Sdk')
    for item in tree.iter('Compile'):
        value = item.get('Include', '').replace('\\', '/')
        if value and '$(' not in value:
            item.set('Include', native(project.parent / value))
    tree.write(folder / 'upstream.proj', encoding='utf-8', xml_declaration=True)
    wrapper = E.Element('Project', Sdk='Microsoft.NET.Sdk')
    pg = E.SubElement(wrapper, 'PropertyGroup')
    for k, v in dict(TargetFramework='net8.0', ImportDirectoryBuildProps='false',
                     ImportDirectoryBuildTargets='false', EnableDefaultItems='false',
                     NetCoreAppCurrent='net9.0', NetCoreAppPrevious='net8.0', NetCoreAppMinimum='net8.0',
                     NetFrameworkMinimum='net462', TargetOS='windows', TargetsWindows='true',
                     CommonPath=native(libs / 'Common/src') + '/', LibrariesProjectRoot=native(libs) + '/',
                     CoreLibSharedDir=native(libs / 'System.Private.CoreLib/src') + '/').items():
        E.SubElement(pg, k).text = v
    E.SubElement(wrapper, 'Import', Project='upstream.proj')
    E.ElementTree(wrapper).write(folder / (name + '.csproj'), encoding='utf-8', xml_declaration=True)
    wrappers[name] = native(folder / (name + '.csproj'))
(out / 'wrappers.json').write_text(json.dumps(wrappers, indent=2) + '\n')
dotnet = '/clang64/lib/dotnet/dotnet.exe'
sdk = Path('/clang64/lib/dotnet/sdk/8.0.131')
import os
env = dict(os.environ, MSBuildSDKsPath=subprocess.check_output(['cygpath', '-am', str(sdk / 'Sdks')], text=True).strip(),
           MSBUILD_EXE_PATH=subprocess.check_output(['cygpath', '-am', str(sdk / 'MSBuild.dll')], text=True).strip(),
           MSBuildEnableWorkloadResolver='false')
tool = root / 'reference10-2/evaluation-tool/bin/Release/net8.0/EvaluateReferenceProjects.dll'
with (out / 'evaluation.log').open('w') as log:
    subprocess.run([dotnet, native(tool), native(out / 'wrappers.json'), native(out / 'evaluated.json')], env=env, stdout=log, stderr=subprocess.STDOUT, check=True)
evaluated = json.loads((out / 'evaluated.json').read_text())
keys = out / 'keys';keys.mkdir()
for key in ['Microsoft', 'Open']:
    (keys / (key + '.pubkey')).write_bytes(bytes.fromhex((here / 'inputs' / (key + '.pubkey.hex')).read_text()))
records, localizations = {}, []
for name in names:
    project = libs / name / 'src' / (name + '.csproj')
    original = E.parse(project)
    selected = [posix(item['full_path']) for item in evaluated[name]['items']['Compile']]
    resource = project.parent / 'Resources/Strings.resx'
    if resource.is_file():
        selected.append(libs / 'Common/src/System/SR.cs')
    assert len(selected) == len(set(selected)) and all(p.is_file() for p in selected)
    folder = out / name;folder.mkdir()
    tree = E.Element('Project', Sdk='Microsoft.NET.Sdk')
    pg = E.SubElement(tree, 'PropertyGroup')
    nullable = original.findtext('.//Nullable') or 'enable'
    key = 'Microsoft' if name != 'System.IO.Pipelines' else 'Open'
    values = dict(TargetFramework='net8.0', AssemblyName=name, PackageId=name, Version='9.0.0',
                  AssemblyVersion='9.0.0.0', FileVersion='9.0.0.0', InformationalVersion='9.0.0+9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3',
                  IncludeSourceRevisionInInformationalVersion='false', EnableDefaultItems='false',
                  EnableNETAnalyzers='false', NuGetAudit='false', Nullable=nullable, AllowUnsafeBlocks='true',
                  LangVersion='preview', CscToolPath=native(root / 'cdac-bootstrap5/compiler'), CscToolExe='csc50.cmd',
                  UseSharedCompilation='false', SignAssembly='true', PublicSign='true', AssemblyOriginatorKeyFile='../keys/' + key + '.pubkey',
                  DefineConstants=evaluated[name]['properties']['DefineConstants'],
                  PackageLicenseExpression='MIT', RepositoryUrl='https://github.com/dotnet/runtime',
                  RepositoryCommit='9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3')
    for k, v in values.items():E.SubElement(pg, k).text = v
    ig = E.SubElement(tree, 'ItemGroup')
    for path in selected:E.SubElement(ig, 'Compile', Include=native(path))
    dependencies = []
    for item in evaluated[name]['items']['ProjectReference']:
        dependency = Path(item['full_path'].replace('\\', '/')).stem
        assert dependency in names, (name, dependency)
        if dependency not in dependencies:
            dependencies.append(dependency)
            E.SubElement(ig, 'PackageReference', Include=dependency, Version='[9.0.0]')
    if resource.is_file():
        logical = 'FxResources.' + name + '.SR'
        code = ['#nullable enable', 'namespace FxResources.' + name + ' { internal static class SR {} }',
                'namespace System { internal static partial class SR {',
                'internal static global::System.Resources.ResourceManager ResourceManager { get; } = new global::System.Resources.ResourceManager("' + logical + '", typeof(FxResources.' + name + '.SR).Assembly);']
        strings = {}
        for item in E.parse(resource).iter('data'):
            key_name = item.get('name');assert key_name.isidentifier()
            strings[key_name] = item.findtext('value') or ''
            code.append('internal static string @' + key_name + ' => GetResourceString("' + key_name + '");')
        code.append('}}')
        (folder / 'SR.Generated.cs').write_text('\n'.join(code) + '\n')
        E.SubElement(ig, 'Compile', Include='SR.Generated.cs')
        E.SubElement(ig, 'EmbeddedResource', Include=native(resource), LogicalName=logical + '.resources')
        localizations.append(dict(assembly=name, logical_name=logical, culture='', strings=strings))
        ns={'x':'urn:oasis:names:tc:xliff:document:1.2'}
        for translation in sorted((resource.parent / 'xlf').glob('Strings.*.xlf')):
            xlf=E.parse(translation);culture=xlf.find('x:file',ns).get('target-language')
            values={u.get('id'):''.join(u.find('x:target',ns).itertext()) for u in xlf.findall('.//x:trans-unit',ns) if u.find('x:target',ns) is not None}
            localized=E.parse(resource)
            for item in localized.iter('data'):
                if item.get('name') in values:item.find('value').text=values[item.get('name')]
            target=folder/('Strings.'+culture+'.resx');localized.write(target,encoding='utf-8',xml_declaration=True)
            E.SubElement(ig,'EmbeddedResource',Include=target.name,LogicalName=logical+'.'+culture+'.resources',Culture=culture,WithCulture='true')
            localizations.append(dict(assembly=name,logical_name=logical,culture=culture,strings={item.get('name'):item.findtext('value') or '' for item in localized.iter('data')}))
    target = E.SubElement(tree, 'Target', Name='RemoveFrameworkSelfReference', BeforeTargets='CoreCompile')
    group = E.SubElement(target, 'ItemGroup')
    for kind in ['ReferencePath', 'ReferencePathWithRefAssemblies']:
        E.SubElement(group, kind, Remove='@(' + kind + ')', Condition="'%({0}.FrameworkReferenceName)' == 'Microsoft.NETCore.App' And '%({0}.Filename)' == '$(AssemblyName)'".format(kind))
    E.indent(tree);E.ElementTree(tree).write(folder/'Build.csproj',encoding='utf-8',xml_declaration=True)
    records[name] = dict(original_project=project.relative_to(source).as_posix(), original_project_sha256=sha(project),
                        original_cs={p.relative_to(source).as_posix():sha(p) for p in selected},
                        dependencies=dependencies, production_cs_changes=0, original_supported_framework='net8.0',
                        resource_files_sha256={p.relative_to(source).as_posix():sha(p) for p in [resource,*sorted((resource.parent/'xlf').glob('Strings.*.xlf'))]} if resource.is_file() else {})
(out/'NuGet.config').write_text('<configuration><packageSources><clear/><add key="source-private" value="'+native(out/'feed')+'"/></packageSources></configuration>\n')
(out/'source-inputs.json').write_text(json.dumps(dict(runtime_version='9.0.0',runtime_commit='9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3',source_archive_sha256=sha(archive),projects=records,removed_binary_inputs=removed,full_dependency_goal_complete=False),indent=2)+'\n')
(out/'localization.json').write_text(json.dumps(localizations,ensure_ascii=False,indent=2)+'\n')
print('Prepared eight complete original runtime9 projects:',{name:len(record['original_cs']) for name,record in records.items()},'localized payloads',len(localizations))
