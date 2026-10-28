"""Prepare original cDAC generator sources for a private source.NET8 bootstrap host."""
from pathlib import Path
import hashlib
import json
import shutil
import subprocess
import sys
import tarfile
import xml.etree.ElementTree as ET

root, archive = (Path(p).resolve() for p in sys.argv[1:3])
here = Path(__file__).resolve().parent
sha = lambda data: hashlib.sha256(data).hexdigest()
assert sha(archive.read_bytes()) == 'a88afc443b5241c7d25866b961d9e364a3b6a309c35eb0de07e3338b0d72f65b'
audit = json.loads((here / 'inputs/command-line-vmr-audit.json').read_text())
assert audit['vmr_commit'] == '12f41dd0da1a94621e039e27aef264179c1104b5'
manifest = json.loads((here / 'inputs/runtime10-vmr-source-manifest.json').read_text())
assert next(item['commitSha'] for item in manifest['repositories']
            if item['path'] == 'command-line-api') == audit['command_line_source_commit']
details = ET.parse(root / 'r/eng/Version.Details.xml')
dependency = details.find(".//Dependency[@Name='System.CommandLine']")
assert dependency.get('Version') == '2.0.4'
assert dependency.findtext('Sha') == audit['vmr_commit']
name = sys.argv[3] if len(sys.argv) == 4 else 'cdac-bootstrap'
assert Path(name).name == name and name not in {'.', '..'}
out = root / name
assert not out.exists(), out
source = out / 'command-line-source'
source.mkdir(parents=True)
with tarfile.open(archive) as tar:
    assert tar.pax_headers['comment'] == audit['command_line_source_commit']
    for member in tar:
        prefix, _, name = member.name.partition('/')
        if name not in audit['verified_original_inputs'] and name != 'LICENSE.TXT':
            continue
        assert member.isfile() and '..' not in Path(name).parts and not Path(name).is_absolute()
        data = tar.extractfile(member).read()
        if name in audit['verified_original_inputs']:
            item = audit['verified_original_inputs'][name]
            assert sha(data) == item['sha256'], name
            blob = hashlib.sha1(b'blob ' + str(len(data)).encode() + b'\0' + data).hexdigest()
            assert blob == item['git_blob_sha1'], name
        target = source / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
for name in audit['verified_original_inputs']:
    assert (source / name).is_file(), name
native = lambda path: subprocess.check_output(['cygpath', '-am', str(path)], text=True).strip()
compiler = out / 'compiler'
compiler_source = Path('/clang64/lib/dotnet-bootstrap/roslyn-5.0.0')
shutil.copytree(compiler_source, compiler)
(compiler / 'csc.runtimeconfig.json').write_text(json.dumps(dict(runtimeOptions=dict(
    tfm='net8.0', framework=dict(name='Microsoft.NETCore.App', version='8.0.31')))) + '\n')
command = '@echo off\r\n"' + native(Path('/clang64/lib/dotnet/dotnet.exe'))
command += '" "' + native(compiler / 'CompilerHost.dll') + '" %*\r\n'
command += 'exit /b %errorlevel%\r\n'
(compiler / 'csc50.cmd').write_bytes(command.encode('utf-8'))
host = ET.Element('Project', Sdk='Microsoft.NET.Sdk')
props = ET.SubElement(host, 'PropertyGroup')
for key, value in dict(TargetFramework='net8.0', OutputType='Exe', AssemblyName='CompilerHost',
                       EnableDefaultItems='false', EnableNETAnalyzers='false', NuGetAudit='false',
                       Nullable='enable', UseSharedCompilation='false',
                       CscToolPath=native(Path('/clang64/lib/dotnet/compiler-packs/roslyn-4.9.2')),
                       CscToolExe='csc49.cmd', OutputPath=native(compiler),
                       AppendTargetFrameworkToOutputPath='false').items():
    ET.SubElement(props, key).text = value
items = ET.SubElement(host, 'ItemGroup')
ET.SubElement(items, 'Compile', Include=native(here / 'inputs/CompilerHost.cs'))
(out / 'compiler-host').mkdir()
ET.indent(host)
ET.ElementTree(host).write(out / 'compiler-host/Build.csproj', encoding='utf-8', xml_declaration=True)
def project(name, assembly, inputs, exe=False):
    directory = out / name
    directory.mkdir()
    tree = ET.Element('Project', Sdk='Microsoft.NET.Sdk')
    properties = ET.SubElement(tree, 'PropertyGroup')
    values = dict(TargetFramework='net8.0', AssemblyName=assembly,
                  EnableDefaultItems='false', EnableNETAnalyzers='false',
                  NuGetAudit='false', Nullable='enable', AllowUnsafeBlocks='true',
                  LangVersion='preview', UseSharedCompilation='false',
                  CscToolPath=native(compiler), CscToolExe='csc50.cmd',
                  IsPackable='false', Version='2.0.4' if not exe else '10.0.5')
    if exe:
        values['OutputType'] = 'Exe'
    for key, value in values.items():
        ET.SubElement(properties, key).text = value
    items = ET.SubElement(tree, 'ItemGroup')
    for path in inputs:
        ET.SubElement(items, 'Compile', Include=native(path))
    ET.indent(tree)
    path = directory / 'Build.csproj'
    ET.ElementTree(tree).write(path, encoding='utf-8', xml_declaration=True)
    return path, tree, items
command_inputs = sorted((source / 'src/System.CommandLine').rglob('*.cs'))
command_inputs.append(source / 'src/System.Diagnostics.CodeAnalysis.cs')
command_project, tree, items = project('command-line', 'System.CommandLine', command_inputs)
resources = list((source / 'src/System.CommandLine').rglob('*.resx'))
assert len(resources) == 1
resource = ET.SubElement(items, 'EmbeddedResource', Include=native(resources[0]))
ET.SubElement(resource, 'LogicalName').text = 'System.CommandLine.Properties.Resources.resources'
ET.indent(tree)
ET.ElementTree(tree).write(command_project, encoding='utf-8', xml_declaration=True)
cdac_source = root / 'r/src/coreclr/tools/cdac-build-tool'
cdac_inputs = sorted(cdac_source.rglob('*.cs'))
cdac_project, tree, items = project('tool', 'cdac-build-tool', cdac_inputs, exe=True)
ET.SubElement(items, 'ProjectReference', Include=native(command_project))
ET.indent(tree)
ET.ElementTree(tree).write(cdac_project, encoding='utf-8', xml_declaration=True)
(out / 'NuGet.config').write_text('<configuration><packageSources><clear/></packageSources></configuration>\n')
project('compiler-probe', 'CSharp14BootstrapProbe', [here / 'inputs/CSharp14Probe.cs'], exe=True)
record = dict(runtime_version='10.0.5', vmr_commit=audit['vmr_commit'],
              command_line_declared_version='2.0.4', command_line_source_commit=audit['command_line_source_commit'],
              command_line_archive_sha256=sha(archive.read_bytes()),
              command_line_original_cs=len(command_inputs), cdac_original_cs=len(cdac_inputs),
              source_files_sha256={native(p): sha(p.read_bytes()) for p in [*command_inputs, *resources, *cdac_inputs]},
              production_cs_changes=0, bootstrap_framework='net8.0',
              source_compiler='installed source Roslyn5.0.0, originally bootstrapped on Mono, hosted here by source.NET8',
              source_compiler_files_sha256={p.name: sha(p.read_bytes()) for p in compiler_source.iterdir() if p.is_file()},
              compiler_entry_adapter='private net8 host calls original Program.Run and analyzer loader with managed arguments; compiler bytes unchanged',
              compiler_host_source_sha256=sha((here / 'inputs/CompilerHost.cs').read_bytes()),
              compiler_csharp14_probe_source_sha256=sha((here / 'inputs/CSharp14Probe.cs').read_bytes()),
              original_tool_framework='net10.0', shipping_package=False,
              full_runtime10_built=False, full_dependency_goal_complete=False)
(out / 'source-inputs.json').write_text(json.dumps(record, indent=2) + '\n')
print('PASS original cDAC and VMR-matched command-line sources prepared:',
      len(cdac_inputs), '+', len(command_inputs), 'C# files; private net8 bootstrap only')
