"""Prepare complete runtime10 reference-project evaluation and source bootstrap."""
from pathlib import Path
import hashlib
import json
import re
import subprocess
import sys
import xml.etree.ElementTree as E

root = Path(sys.argv[1]).resolve()
out = Path(sys.argv[2]).resolve()
assert not out.exists(), out
out.mkdir(parents=True)
here = Path(__file__).resolve().parent
source = root / 'r'
libs = source / 'src/libraries'
native_root = subprocess.check_output(['cygpath', '-am', str(root)], text=True).strip()
def native(path):
    path = Path(path).resolve()
    if path.is_relative_to(root):
        return native_root + '/' + path.relative_to(root).as_posix()
    return subprocess.check_output(['cygpath', '-am', str(path)], text=True).strip()
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()

text = (libs / 'NetCoreAppLibrary.props').read_text()
netfx = re.search(r'<NetFxReference>(.*?)</NetFxReference>', text, re.S).group(1)
names = [n.strip() for n in re.search(r'<NetCoreAppLibrary>(.*?)</NetCoreAppLibrary>', text, re.S).group(1).replace('$(NetFxReference)', netfx).split(';') if n.strip()]
projects = {}
for name in names:
    if name.startswith('System.Private.'):
        continue
    for kind, project in [('ref', libs / name / 'ref' / (name + '.csproj')),
                          ('shimref', libs / 'shims' / name / 'ref' / (name + '.csproj')),
                          ('shim', libs / 'shims' / name / 'src' / (name + '.csproj'))]:
        if project.is_file():
            projects[name] = dict(kind=kind, project=project)
            break
    else:
        raise AssertionError(('Missing declared public reference project', name))
for project in sorted((libs / 'shims/stubs').glob('*.csproj')):
    projects['stub:' + project.stem] = dict(kind='stub', project=project)

clones = out / 'project-inputs'
clones.mkdir()
cloned = {}
original_projects = {}
def clone(path, strip_imports=False):
    path = path.resolve()
    cache_key = str(path) + str(strip_imports)
    if cache_key in cloned:
        return cloned[cache_key]
    name = hashlib.sha256(cache_key.encode()).hexdigest()[:16] + '.proj'
    target = clones / name
    cloned[cache_key] = target
    body = path.read_text(encoding='utf-8-sig')
    body = body.replace('$(MSBuildThisFileDirectory)', native(path.parent) + '/')
    body = body.replace('$(MSBuildProjectDirectory)', native(path.parent))
    tree = E.ElementTree(E.fromstring(body))
    tree.getroot().attrib.pop('Sdk', None)
    for parent in tree.iter():
        for item in list(parent):
            if item.tag == 'Import' and strip_imports:
                parent.remove(item)
                continue
            for attribute in ['Include', 'Update', 'Remove', 'Project']:
                if attribute not in item.attrib:
                    continue
                if item.tag not in {'Compile', 'ProjectReference', 'Import', 'EmbeddedResource', 'None', 'Content'}:
                    continue
                value = item.get(attribute).replace('\\', '/')
                if '$(' not in value and not value.startswith('/') and not (len(value) > 1 and value[1] == ':'):
                    item.set(attribute, native(path.parent / value))
    E.indent(tree)
    tree.write(target, encoding='utf-8', xml_declaration=True)
    original_projects[path.relative_to(source).as_posix()] = sha(path)
    return target

common_props = clone(libs / 'Directory.Build.props', strip_imports=True)
shim_props = clone(libs / 'shims/Directory.Build.props', strip_imports=True)
stub_props = clone(libs / 'shims/stubs/Directory.Build.props', strip_imports=True)
wrappers = {}
for name, entry in projects.items():
    project = entry['project']
    folder = out / 'p' / name.replace(':', '-')
    folder.mkdir(parents=True)
    wrapper = E.Element('Project', Sdk='Microsoft.NET.Sdk')
    pg = E.SubElement(wrapper, 'PropertyGroup')
    values = dict(TargetFramework='net10.0', ImportDirectoryBuildProps='false',
                  ImportDirectoryBuildTargets='false', EnableDefaultItems='false',
                  EnableDefaultCompileItems='false', NetCoreAppCurrent='net10.0',
                  NetCoreAppPrevious='net9.0', NetCoreAppMinimum='net8.0',
                  NetFrameworkMinimum='net462', MajorVersion='10', MinorVersion='0',
                  TargetOS='windows', TargetsWindows='true', TargetArchitecture='x64',
                  Platform='x64', Configuration='Release', RuntimeFlavor='CoreCLR',
                  IsReferenceAssemblyProject='true' if entry['kind'] != 'stub' else 'false',
                  LibrariesProjectRoot=native(libs) + '/', RepoRoot=native(source) + '/',
                  RepositoryEngineeringDir=native(source / 'eng') + '/',
                  CoreLibSharedDir=native(libs / 'System.Private.CoreLib/src') + '/')
    for k, v in values.items():
        E.SubElement(pg, k).text = v
    E.SubElement(wrapper, 'Import', Project=native(common_props))
    E.SubElement(wrapper, 'Import', Project=native(libs / 'NetCoreAppLibrary.props'))
    if entry['kind'] in {'shim', 'shimref', 'stub'}:
        E.SubElement(wrapper, 'Import', Project=native(shim_props))
    if entry['kind'] == 'stub':
        E.SubElement(wrapper, 'Import', Project=native(stub_props))
    local = project.parent.parent / 'Directory.Build.props'
    if entry['kind'] != 'stub' and local.is_file() and local not in [libs / 'Directory.Build.props', libs / 'shims/Directory.Build.props']:
        E.SubElement(wrapper, 'Import', Project=native(clone(local, strip_imports=True)))
    if entry['kind'] in {'shim', 'shimref'}:
        items = E.SubElement(wrapper, 'ItemGroup')
        for path in sorted(project.parent.glob('*.cs')):
            E.SubElement(items, 'Compile', Include=native(path))
    E.SubElement(wrapper, 'Import', Project=native(clone(project)))
    if entry['kind'] == 'stub':
        E.SubElement(wrapper, 'Import', Project=native(clone(libs / 'shims/stubs/Directory.Build.targets', strip_imports=True)))
        pg = E.SubElement(wrapper, 'PropertyGroup')
        E.SubElement(pg, 'ForwardedTypesCompilePath').text = native(folder / (project.stem + '.ForwardedTypes.cs'))
    path = folder / project.name
    E.indent(wrapper)
    E.ElementTree(wrapper).write(path, encoding='utf-8', xml_declaration=True)
    wrappers[name] = native(path)
    entry['project'] = project.relative_to(source).as_posix()

(out / 'wrappers.json').write_text(json.dumps(wrappers, indent=2) + '\n')
(out / 'projects.json').write_text(json.dumps(projects, indent=2) + '\n')
(out / 'source-projects.json').write_text(json.dumps(original_projects, indent=2) + '\n')
keys = out / 'keys'
keys.mkdir()
for path in (here / 'inputs').glob('*.pubkey.hex'):
    (keys / (path.stem.replace('.pubkey', '') + '.pubkey')).write_bytes(bytes.fromhex(path.read_text()))

tool = out / 'evaluation-tool'
tool.mkdir()
tree = E.Element('Project', Sdk='Microsoft.NET.Sdk')
pg = E.SubElement(tree, 'PropertyGroup')
for k, v in dict(TargetFramework='net8.0', OutputType='Exe', AssemblyName='EvaluateReferenceProjects',
                 EnableDefaultItems='false', EnableNETAnalyzers='false', NuGetAudit='false', Nullable='enable',
                 UseAppHost='false').items():
    E.SubElement(pg, k).text = v
ig = E.SubElement(tree, 'ItemGroup')
E.SubElement(ig, 'Compile', Include=native(here / 'inputs/EvaluateProjects.cs'))
sdk = Path('/clang64/lib/dotnet/sdk/8.0.131')
for name in ['Microsoft.Build', 'Microsoft.Build.Framework', 'Microsoft.Build.Utilities.Core', 'Microsoft.NET.StringTools']:
    reference = E.SubElement(ig, 'Reference', Include=name)
    E.SubElement(reference, 'HintPath').text = native(sdk / (name + '.dll'))
E.indent(tree)
E.ElementTree(tree).write(tool / 'Build.csproj', encoding='utf-8', xml_declaration=True)
(out / 'NuGet.config').write_text('<configuration><packageSources><clear/></packageSources></configuration>\n')
generate = E.Element('Project')
target = E.SubElement(generate, 'Target', Name='Generate')
E.SubElement(target, 'MSBuild', Projects=';'.join(wrappers[n] for n in projects if projects[n]['kind'] == 'stub'),
             Targets='CreateCompileSourceForForwardedTypes', BuildInParallel='false')
E.ElementTree(generate).write(out / 'GenerateStubs.proj', encoding='utf-8', xml_declaration=True)
print('Prepared', len(projects), 'original reference/stub projects;', len(names), 'declared runtime assemblies; evaluation and builds remain required')
