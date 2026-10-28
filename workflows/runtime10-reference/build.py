"""Build the complete declared net10 reference graph from evaluated source inputs."""
from pathlib import Path
import hashlib
import json
import subprocess
import sys
import xml.etree.ElementTree as E

root, out = (Path(p).resolve() for p in sys.argv[1:3])
only = sys.argv[3] if len(sys.argv) > 3 else None
here = Path(__file__).resolve().parent
native_root = subprocess.check_output(['cygpath', '-am', str(root)], text=True).strip()
def native(path):
    return native_root + '/' + Path(path).resolve().relative_to(root).as_posix()
def posix(path):
    path = path.replace('\\', '/')
    assert path.lower().startswith(native_root.lower() + '/')
    return root / path[len(native_root) + 1:]
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
projects = json.loads((out / 'projects.json').read_text())
evaluated = json.loads((out / 'evaluated.json').read_text())
assert set(projects) == set(evaluated)
source = root / 'r'
by_project = {native(source / p['project']).lower(): n for n, p in projects.items()}
dependencies = {}
for name, entry in evaluated.items():
    deps = []
    for item in entry['items']['ProjectReference']:
        if item['metadata'].get('ReferenceOutputAssembly', '').lower() == 'false':
            continue
        assert item['metadata'].get('OutputItemType', '') != 'Analyzer', (name, item)
        path = item['full_path'].replace('\\', '/')
        dependency = by_project.get(path.lower())
        if dependency is None:
            dependency = Path(path).stem
            assert dependency in projects, (name, 'unresolved original project dependency', path)
        if dependency != name and dependency not in deps:
            deps.append(dependency)
    if projects[name]['kind'] == 'stub' and 'System.Runtime' not in deps:
        deps.append('System.Runtime')
    dependencies[name] = deps
order, seen = [], set()
def visit(name, stack=()):
    if name in seen:
        return
    assert name not in stack, ('Reference dependency cycle', stack, name)
    for dependency in dependencies[name]:
        visit(dependency, stack + (name,))
    seen.add(name)
    order.append(name)
for name in ([only] if only else sorted(projects)):
    visit(name)
(out / 'build-order.json').write_text(json.dumps(dict(order=order, dependencies=dependencies), indent=2) + '\n')
public = out / 'ref/net10.0'
stubs = out / 'stubs'
public.mkdir(parents=True, exist_ok=True)
stubs.mkdir(exist_ok=True)
def product(name):
    return (stubs if projects[name]['kind'] == 'stub' else public) / (name.removeprefix('stub:') + '.dll')

dotnet = '/clang64/lib/dotnet/dotnet.exe'
compiler = root / 'cdac-bootstrap5/compiler'
assert (compiler / 'CompilerHost.dll').is_file()
sdk_native = subprocess.check_output(['cygpath', '-am', '/clang64/lib/dotnet/sdk/8.0.131'], text=True).strip()
genfacades_inputs = json.loads((here / 'inputs/genfacades-source-inputs.json').read_text())
for name, entry in genfacades_inputs.items():
    assert sha(here / 'inputs' / name) == entry['sha256'], name
version_source = root / 'corelib10-1/corelib/obj/AssemblyInfo.g.cs'
import re
file_version = re.search(r'AssemblyFileVersion\("([^"]+)"\)', version_source.read_text()).group(1)
version_inputs = json.loads((root / 'version-build-inputs.json').read_text())
assert version_inputs['original_task_sha256'] == '7740ecad63add6b83cdca96b60ebb9c44ff4ece302833a8e5d2a9f43307cf0b7'
file_versions = {'10': file_version}
def library_file_version(major):
    if major in file_versions:
        return file_versions[major]
    task = root / 'version-build/bin/Release/net8.0/RuntimeServicing.VersionBuildTasks.dll'
    assert task.is_file()
    project = E.Element('Project')
    E.SubElement(project, 'UsingTask', TaskName='Microsoft.DotNet.Arcade.Sdk.CalculateAssemblyAndFileVersions', AssemblyFile=native(task))
    target = E.SubElement(project, 'Target', Name='Calculate')
    call = E.SubElement(target, 'Microsoft.DotNet.Arcade.Sdk.CalculateAssemblyAndFileVersions',
                        VersionPrefix=major + '.0.5', BuildNumber=version_inputs['build_number'])
    E.SubElement(call, 'Output', TaskParameter='FileVersion', PropertyName='LibraryFileVersion')
    path = out / ('file-version-' + major + '.txt')
    E.SubElement(target, 'WriteLinesToFile', File=native(path), Lines='$(LibraryFileVersion)', Overwrite='true')
    proj = out / ('CalculateVersion' + major + '.proj')
    E.ElementTree(project).write(proj, encoding='utf-8', xml_declaration=True)
    with (out / ('calculate-version-' + major + '.log')).open('w') as log:
        subprocess.run([dotnet, 'msbuild', native(proj), '-t:Calculate', '-nologo', '-v:minimal'], stdout=log, stderr=subprocess.STDOUT, check=True)
    value = path.read_text(encoding='utf-8-sig').strip()
    file_versions[major] = value
    return value
task_folder = out / 'clear-version-task'
task = task_folder / 'bin/Release/net8.0/Reference10.ClearVersionTask.dll'
if not only and not task.is_file():
    task_folder.mkdir(exist_ok=True)
    tree = E.Element('Project', Sdk='Microsoft.NET.Sdk')
    pg = E.SubElement(tree, 'PropertyGroup')
    for k, v in dict(TargetFramework='net8.0', AssemblyName='Reference10.ClearVersionTask',
                     EnableDefaultItems='false', EnableNETAnalyzers='false', NuGetAudit='false').items():
        E.SubElement(pg, k).text = v
    ig = E.SubElement(tree, 'ItemGroup')
    for name in ['GenFacades-BuildTask.cs', 'ClearAssemblyReferenceVersions.cs']:
        E.SubElement(ig, 'Compile', Include=native(here / 'inputs' / name))
    for name in ['Microsoft.Build.Framework', 'Microsoft.Build.Utilities.Core']:
        r = E.SubElement(ig, 'Reference', Include=name)
        E.SubElement(r, 'HintPath').text = sdk_native + '/' + name + '.dll'
        E.SubElement(r, 'Private').text = 'false'
    E.ElementTree(tree).write(task_folder / 'Build.csproj', encoding='utf-8', xml_declaration=True)
    with (task_folder / 'build.log').open('w') as log:
        subprocess.run([dotnet, 'build', native(task_folder / 'Build.csproj'), '-c', 'Release',
                        '--configfile', native(out / 'NuGet.config'), '-p:UseSharedCompilation=false', '-v:minimal'],
                       stdout=log, stderr=subprocess.STDOUT, check=True)

status = {}
for name in order:
    entry, project = evaluated[name], projects[name]
    props = entry['properties']
    folder = out / 'p' / name.replace(':', '-')
    cs = list(dict.fromkeys(posix(item['full_path']) for item in entry['items']['Compile']))
    if project['kind'] == 'stub':
        cs.append(folder / (name.removeprefix('stub:') + '.ForwardedTypes.cs'))
    assert cs and all(p.is_file() for p in cs), (name, cs)
    assert all(p.is_relative_to(source) for p in cs) or project['kind'] == 'stub'
    assembly = props['AssemblyName']
    assembly_version = props['AssemblyVersion'] or props['MajorVersion'] + '.0.0.0'
    file_version = library_file_version(props['MajorVersion'])
    key = out / 'keys' / (props['StrongNameKeyId'] + '.pubkey')
    assert key.is_file(), (name, key)
    code = ['[assembly: System.Reflection.AssemblyVersion(' + json.dumps(assembly_version) + ')]',
            '[assembly: System.Reflection.AssemblyFileVersion(' + json.dumps(file_version) + ')]',
            '[assembly: System.Reflection.AssemblyInformationalVersion("10.0.5+081d220c0a773ffb7c6bea6b48727833576a65ef")]',
            '[assembly: System.Runtime.Versioning.TargetFramework(".NETCoreApp,Version=v10.0")]',
            '[assembly: System.Reflection.AssemblyMetadata("Serviceable", "True")]']
    if props['CLSCompliant'] == 'true':
        code.append('[assembly: System.CLSCompliant(true)]')
    for attribute in entry['items']['AssemblyAttribute']:
        if attribute['identity'] == 'System.Runtime.CompilerServices.ReferenceAssemblyAttribute':
            continue  # /refonly emits the original reference-assembly marker.
        metadata = attribute['metadata']
        values = []
        for param in sorted(k for k in metadata if re.fullmatch('_Parameter[0-9]+', k)):
            values.append(metadata[param] if metadata.get(param + '_IsLiteral') == 'true' else json.dumps(metadata[param]))
        code.append('[assembly: ' + attribute['identity'] + '(' + ', '.join(values) + ')]')
    assembly_info = folder / 'AssemblyInfo.g.cs'
    assembly_info.write_text('\n'.join(code) + '\n')
    defines = list(dict.fromkeys(props['DefineConstants'].split(';') +
        ['NET', 'NET10_0', *['NET' + str(v) + '_0_OR_GREATER' for v in range(5, 11)],
         *['NETCOREAPP' + v + '_OR_GREATER' for v in ['1_0', '1_1', '2_0', '2_1', '2_2', '3_0', '3_1']]]))
    args = ['/nostdlib+', '/target:library', '/refonly', '/deterministic+', '/unsafe+',
            '/nullable:enable', '/langversion:preview', '/runtimemetadataversion:v4.0.30319',
            '/publicsign+', '/keyfile:"' + native(key) + '"', '/out:"' + native(product(name)) + '"',
            '/define:' + ';'.join(defines), '/nowarn:' + props['NoWarn']]
    closure = set()
    def add_dependencies(n):
        for dependency in dependencies[n]:
            if dependency not in closure:
                closure.add(dependency)
                add_dependencies(dependency)
    add_dependencies(name)
    refs = [n for n in order if n in closure]
    assert all(product(n).is_file() for n in refs), (name, refs)
    args += ['/reference:"' + native(product(n)) + '"' for n in refs]
    args += ['"' + native(p) + '"' for p in [*cs, assembly_info]]
    rsp = folder / 'compile.rsp'
    rsp.write_text('\n'.join(args) + '\n')
    inputs = dict(name=name, kind=project['kind'], assembly_version=assembly_version,
                  file_version=file_version, key_sha256=sha(key),
                  source_cs={str(p.relative_to(source)) if p.is_relative_to(source) else native(p): sha(p) for p in cs},
                  assembly_info_sha256=sha(assembly_info), response_sha256=sha(rsp),
                  dependency_sha256={n: sha(product(n)) for n in refs},
                  compiler_sha256={p.name: sha(p) for p in [compiler / 'CompilerHost.dll', compiler / 'csc.exe']},
                  zero_version_policy=props['GenFacadesForceZeroVersionSeeds'] == 'true')
    fingerprint = hashlib.sha256(json.dumps(inputs, sort_keys=True).encode()).hexdigest()
    previous = folder / 'build-result.json'
    if previous.is_file() and product(name).is_file():
        old = json.loads(previous.read_text())
        if old.get('fingerprint') == fingerprint and old.get('output_sha256') == sha(product(name)) and old.get('exit_code') == 0:
            status[name] = old
            print('Cached', name, flush=True)
            continue
    command = [dotnet, native(compiler / 'CompilerHost.dll'), '-noconfig', '@' + native(rsp)]
    with (folder / 'compile.log').open('w') as log:
        result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT)
    inputs.update(command=command, fingerprint=fingerprint, exit_code=result.returncode)
    (folder / 'build-inputs.json').write_text(json.dumps(inputs, indent=2) + '\n')
    assert result.returncode == 0, (name, result.returncode, folder / 'compile.log')
    if inputs['zero_version_policy']:
        assert task.is_file(), 'Build complete graph to initialize original GenFacades task'
        tree = E.Element('Project')
        E.SubElement(tree, 'UsingTask', TaskName='Microsoft.DotNet.GenFacades.ClearAssemblyReferenceVersions', AssemblyFile=native(task))
        target = E.SubElement(tree, 'Target', Name='Clear')
        E.SubElement(target, 'Microsoft.DotNet.GenFacades.ClearAssemblyReferenceVersions', Assembly=native(product(name)))
        E.ElementTree(tree).write(folder / 'Clear.proj', encoding='utf-8', xml_declaration=True)
        with (folder / 'clear-versions.log').open('w') as log:
            subprocess.run([dotnet, 'msbuild', native(folder / 'Clear.proj'), '-t:Clear', '-v:minimal', '-nologo'],
                           stdout=log, stderr=subprocess.STDOUT, check=True)
    inputs['output_sha256'] = sha(product(name))
    previous.write_text(json.dumps(inputs, indent=2) + '\n')
    status[name] = inputs
    (out / 'build-status.json').write_text(json.dumps(dict(projects=status, complete_declared_reference_graph=set(status) == set(projects),
        full_runtime10_built=False, full_sdk10_built=False, full_dependency_goal_complete=False), indent=2) + '\n')
    print('PASS', name, 'original C# inputs', len(cs), flush=True)
(out / 'build-status.json').write_text(json.dumps(dict(projects=status, complete_declared_reference_graph=set(status) == set(projects),
    full_runtime10_built=False, full_sdk10_built=False, full_dependency_goal_complete=False), indent=2) + '\n')
print('PASS', len(status), 'requested reference graph projects built; metadata and consumer checks remain required')
