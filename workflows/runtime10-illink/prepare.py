"""Prepare original runtime10 ILLink and its exact pinned Cecil source dependency."""
from pathlib import Path
import glob
import hashlib
import json
import subprocess
import sys
import tarfile
import xml.etree.ElementTree as E

root, archive = (Path(p).resolve() for p in sys.argv[1:3])
source = root / 'r'
out = Path(sys.argv[3]).resolve() if len(sys.argv) > 3 else root / 'illink10-1'
assert not out.exists(), out
out.mkdir()
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
assert sha(archive) == '3d82d4708f93dabd7442bcdb6f2667489fdd3ee2162d821b96d8aad62c33d019'
details = E.parse(source / 'eng/Version.Details.xml')
dep = details.find(".//Dependency[@Name='Microsoft.DotNet.Cecil']")
assert dep.get('Version') == '0.11.5-alpha.26116.108'
assert dep.findtext('Sha') == '12f41dd0da1a94621e039e27aef264179c1104b5'
here = Path(__file__).resolve().parent
manifest = json.loads((here / 'inputs/runtime10-vmr-source-manifest.json').read_text())
cecil_commit = next(item['commitSha'] for item in manifest['repositories'] if item['path'] == 'cecil')
assert cecil_commit == '27abc57253a0d3010418e877fdfb35f9fa6806b9'
cecil = out / 'cecil-source'
cecil.mkdir()
removed = []
with tarfile.open(archive) as tar:
    assert tar.pax_headers['comment'] == cecil_commit
    for member in tar:
        if not member.isfile():
            continue
        prefix, _, name = member.name.partition('/')
        assert '..' not in Path(name).parts and not Path(name).is_absolute()
        data = tar.extractfile(member).read()
        if Path(name).suffix.lower() in {'.exe', '.dll', '.pdb', '.mdb', '.nupkg', '.zip'} or data[:2] == b'MZ':
            removed.append(dict(path=name, sha256=hashlib.sha256(data).hexdigest()))
            continue
        target = cecil / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)

native_root = subprocess.check_output(['cygpath', '-am', str(root)], text=True).strip()
def native(path):
    path = Path(path).resolve()
    if path.is_relative_to(root):
        return native_root + '/' + path.relative_to(root).as_posix()
    return subprocess.check_output(['cygpath', '-am', str(path)], text=True).strip()
compiler = root / 'cdac-bootstrap5/compiler'
assert (compiler / 'CompilerHost.dll').is_file()
projects = {
    'Mono.Cecil': cecil / 'Mono.Cecil.csproj',
    'Mono.Cecil.Rocks': cecil / 'rocks/Mono.Cecil.Rocks.csproj',
    'Mono.Cecil.Pdb': cecil / 'symbols/pdb/Mono.Cecil.Pdb.csproj',
    'Mono.Cecil.Mdb': cecil / 'symbols/mdb/Mono.Cecil.Mdb.csproj'}
records = {}
def create(name, inputs, exe=False):
    folder = out / name
    folder.mkdir()
    tree = E.Element('Project', Sdk='Microsoft.NET.Sdk')
    props = E.SubElement(tree, 'PropertyGroup')
    values = dict(TargetFramework='net8.0', AssemblyName=name, EnableDefaultItems='false',
                  EnableNETAnalyzers='false', NuGetAudit='false', Nullable='enable' if exe else 'disable',
                  LangVersion='preview', AllowUnsafeBlocks='true', UseSharedCompilation='false',
                  CscToolPath=native(compiler), CscToolExe='csc50.cmd', IsPackable='false',
                  CopyLocalLockFileAssemblies='true')
    if exe:
        values['OutputType'] = 'Exe'
        values['UseAppHost'] = 'false'
    for k, v in values.items():
        E.SubElement(props, k).text = v
    items = E.SubElement(tree, 'ItemGroup')
    for path in inputs:
        E.SubElement(items, 'Compile', Include=native(path))
    return tree, props, items, folder
def save(tree, folder):
    E.indent(tree)
    E.ElementTree(tree).write(folder / 'Build.csproj', encoding='utf-8', xml_declaration=True)

def remove_framework_duplicates(tree):
    target = E.SubElement(tree, 'Target', Name='RemoveFrameworkDuplicates', BeforeTargets='CoreCompile')
    group = E.SubElement(target, 'ItemGroup')
    for kind in ['ReferencePath', 'ReferencePathWithRefAssemblies']:
        condition = "'%({0}.FrameworkReferenceName)' == 'Microsoft.NETCore.App' And ('%({0}.Filename)' == 'System.Collections.Immutable' Or '%({0}.Filename)' == 'System.Reflection.Metadata')".format(kind)
        E.SubElement(group, kind, Remove='@(' + kind + ')', Condition=condition)

for name, project in projects.items():
    selected = []
    for item in E.parse(project).iter('Compile'):
        value = item.get('Include').replace('\\', '/')
        paths = [Path(p).resolve() for p in glob.glob(str(project.parent / value))]
        assert paths, value
        selected.extend(sorted(paths))
    assert len(selected) == len(set(selected))
    tree, props, items, folder = create(name, selected)
    E.SubElement(props, 'GenerateAssemblyInfo').text = 'false'
    E.SubElement(props, 'DefineConstants').text = '$(DefineConstants);NET_CORE;' + ('INSIDE_ROCKS' if name.endswith('.Rocks') else 'CECIL' if name.endswith('.Mdb') else '')
    E.SubElement(props, 'SignAssembly').text = 'true'
    E.SubElement(props, 'PublicSign').text = 'true'
    E.SubElement(props, 'AssemblyOriginatorKeyFile').text = native(cecil / 'cecil.snk')
    if name != 'Mono.Cecil':
        E.SubElement(items, 'ProjectReference', Include='../Mono.Cecil/Build.csproj')
    save(tree, folder)
    records[name] = dict(original_project_sha256=sha(project), original_cs={p.relative_to(cecil).as_posix(): sha(p) for p in selected}, production_cs_changes=0)

# These two runtime10 libraries explicitly support net8.0 upstream. Build their
# complete evaluated net8 source sets, rather than substituting a type-name parser.
libs = source / 'src/libraries'
key = bytes.fromhex((here / 'inputs/Microsoft.pubkey.hex').read_text())
assert hashlib.sha1(key).digest()[-8:][::-1].hex() == 'b03f5f7f11d50a3a'
(out / 'Microsoft.public.snk').write_bytes(key)
for name in ['System.Collections.Immutable', 'System.Reflection.Metadata']:
    project = libs / name / 'src' / (name + '.csproj')
    evaluation = out / (name + '-evaluation')
    evaluation.mkdir()
    upstream = E.parse(project)
    upstream.getroot().attrib.pop('Sdk')
    for item in upstream.iter('Compile'):
        value = item.get('Include', '').replace('\\', '/')
        if value and '$(' not in value:
            item.set('Include', native(project.parent / value))
    upstream.write(evaluation / 'upstream.proj', encoding='utf-8', xml_declaration=True)
    wrapper = E.Element('Project', Sdk='Microsoft.NET.Sdk')
    pg = E.SubElement(wrapper, 'PropertyGroup')
    values = dict(TargetFramework='net8.0', ImportDirectoryBuildProps='false',
                  ImportDirectoryBuildTargets='false', EnableDefaultItems='false',
                  NetCoreAppCurrent='net10.0', NetCoreAppPrevious='net9.0',
                  NetCoreAppMinimum='net8.0', NetFrameworkMinimum='net462',
                  CommonPath=native(libs / 'Common/src') + '/',
                  LibrariesProjectRoot=native(libs) + '/',
                  CoreLibSharedDir=native(libs / 'System.Private.CoreLib/src') + '/')
    for k, v in values.items():
        E.SubElement(pg, k).text = v
    E.SubElement(wrapper, 'Import', Project='upstream.proj')
    E.ElementTree(wrapper).write(evaluation / 'Evaluation.proj', encoding='utf-8', xml_declaration=True)
    command = ['/clang64/lib/dotnet/dotnet.exe', 'msbuild', native(evaluation / 'Evaluation.proj'),
               '-nologo', '-getItem:Compile', '-getProperty:DefineConstants,TargetFramework']
    result = subprocess.run(command, cwd=evaluation, text=True, capture_output=True)
    (evaluation / 'evaluation.json').write_text(result.stdout)
    (evaluation / 'evaluation.stderr').write_text(result.stderr)
    assert result.returncode == 0, result.stdout + result.stderr
    evaluated = json.loads(result.stdout)
    def posix(path):
        normalized = path.replace('\\', '/')
        assert normalized.lower().startswith(native_root.lower() + '/')
        return root / normalized[len(native_root) + 1:]
    selected = [posix(item['FullPath']) for item in evaluated['Items']['Compile']]
    selected += [libs / 'Common/src/System/SR.cs']
    assert len(selected) == len(set(selected)) and all(p.is_file() for p in selected)
    tree, props, items, folder = create(name, selected)
    for k, v in dict(Nullable='enable', DefineConstants=evaluated['Properties']['DefineConstants'],
                     Version='10.0.5', AssemblyVersion='10.0.0.0', SignAssembly='true',
                     PublicSign='true', AssemblyOriginatorKeyFile='../Microsoft.public.snk').items():
        E.SubElement(props, k).text = v
    resource = project.parent / 'Resources/Strings.resx'
    E.SubElement(items, 'EmbeddedResource', Include=native(resource), LogicalName=name + '.Strings.resources')
    code = ['#nullable enable', 'namespace System { internal static partial class SR {',
            'private static readonly global::System.Resources.ResourceManager ResourceManager = new global::System.Resources.ResourceManager("' + name + '.Strings", typeof(SR).Assembly);']
    for item in E.parse(resource).iter('data'):
        key_name = item.get('name')
        assert key_name.isidentifier(), key_name
        code.append('internal static string @' + key_name + ' => GetResourceString("' + key_name + '");')
    code.append('}}')
    (folder / 'SR.Generated.cs').write_text('\n'.join(code) + '\n')
    E.SubElement(items, 'Compile', Include='SR.Generated.cs')
    if name == 'System.Reflection.Metadata':
        E.SubElement(items, 'ProjectReference', Include='../System.Collections.Immutable/Build.csproj')
    remove_framework_duplicates(tree)
    save(tree, folder)
    records[name] = dict(original_project_sha256=sha(project),
                         original_cs={p.relative_to(source).as_posix(): sha(p) for p in selected},
                         production_cs_changes=0, original_supported_framework='net8.0',
                         resource_files_sha256={resource.relative_to(source).as_posix(): sha(resource)})

linker = source / 'src/tools/illink/src/linker'
shared = source / 'src/tools/illink/src/ILLink.Shared'
selected = sorted(p for p in linker.rglob('*.cs') if 'ref' not in p.relative_to(linker).parts)
selected += sorted(shared.rglob('*.cs'))
selected += [source / 'src/libraries/Common/src/System/Text/ValueStringBuilder.cs']
assert len(selected) == len(set(selected))
tree, props, items, folder = create('illink', selected, exe=True)
E.SubElement(props, 'DefineConstants').text = '$(DefineConstants);ILLINK'
E.SubElement(props, 'Version').text = '10.0.5'
E.SubElement(props, 'NoWarn').text = '$(NoWarn);CS0436'
for name in projects:
    E.SubElement(items, 'ProjectReference', Include='../' + name + '/Build.csproj')
for name in ['System.Collections.Immutable', 'System.Reflection.Metadata']:
    E.SubElement(items, 'ProjectReference', Include='../' + name + '/Build.csproj')
E.SubElement(items, 'EmbeddedResource', Include=native(linker / 'Resources/Strings.resx'), LogicalName='Mono.Linker.Resources.Strings.resources')
E.SubElement(items, 'EmbeddedResource', Include=native(shared / 'SharedStrings.resx'), LogicalName='ILLink.Shared.SharedStrings.resources')
code = ['#nullable disable', 'namespace ILLink.Shared { internal static class SharedStrings {',
        'internal static global::System.Resources.ResourceManager ResourceManager { get; } = new global::System.Resources.ResourceManager("ILLink.Shared.SharedStrings", typeof(SharedStrings).Assembly);']
for item in E.parse(shared / 'SharedStrings.resx').iter('data'):
    key = item.get('name')
    assert key.isidentifier(), key
    code.append('internal static string @' + key + ' => ResourceManager.GetString("' + key + '")!;')
code.append('}}')
(folder / 'SharedStrings.Generated.cs').write_text('\n'.join(code) + '\n')
E.SubElement(items, 'Compile', Include='SharedStrings.Generated.cs')
remove_framework_duplicates(tree)
save(tree, folder)
records['illink'] = dict(original_project_sha256=sha(linker / 'Mono.Linker.csproj'),
                         original_shared_project_sha256=sha(shared / 'ILLink.Shared.projitems'),
                         original_cs={p.relative_to(source).as_posix(): sha(p) for p in selected},
                         production_cs_changes=0, original_framework='net10.0', bootstrap_framework='net8.0',
                         original_source_build_staticcs_path=True,
                         resource_files_sha256={p.relative_to(source).as_posix(): sha(p) for p in [linker / 'Resources/Strings.resx', shared / 'SharedStrings.resx']})
tree, props, items, folder = create('NormalizeBootstrapDeps', [here / 'inputs/NormalizeBootstrapDeps.cs'], exe=True)
save(tree, folder)
(out / 'NuGet.config').write_text('<configuration><packageSources><clear/></packageSources></configuration>\n')
(out / 'source-inputs.json').write_text(json.dumps(dict(runtime_version='10.0.5', runtime_commit='081d220c0a773ffb7c6bea6b48727833576a65ef',
    cecil_declared_package_version=dep.get('Version'), cecil_source_commit=cecil_commit,
    cecil_source_archive_sha256=sha(archive), removed_binary_inputs=removed, projects=records,
    private_bootstrap_framework='net8.0', full_sdk10_built=False, full_dependency_goal_complete=False), indent=2) + '\n')
print('Prepared original ILLink/Cecil source projects:', {name: len(item['original_cs']) for name, item in records.items()}, '; binary inputs removed:', len(removed))
