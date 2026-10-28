"""Generate the source targeting pack's complete upstream platform manifest."""
from pathlib import Path
import argparse
import copy
import hashlib
import json
import os
import shutil
import subprocess
import tarfile
import xml.etree.ElementTree as E

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('framework_root', type=Path)
parser.add_argument('pack', type=Path)
parser.add_argument('destination', type=Path)
args = parser.parse_args()
frame, pack, out = (p.resolve() for p in (args.framework_root, args.pack, args.destination))
work = out.with_name(out.name + '-manifest-build')
assert not out.exists() and not work.exists(), (out, work)
source = frame / 'm/src/runtime-8.0.31-native'
inputs = Path(__file__).resolve().parent / 'targeting-inputs'
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
native = lambda p: subprocess.check_output(['cygpath', '-am', str(p)], text=True).strip()
pinned = json.loads((inputs / 'source-inputs.json').read_text())
commit = '444c2ef3c414558e2e409ee346f1faebb19687eb'
assert pinned['source_commit'] == commit
details = E.parse(source / 'eng/Version.Details.xml')
assert details.find(".//Dependency[@Name='Microsoft.DotNet.SharedFramework.Sdk']/Sha").text == commit
for name, record in pinned['files'].items():
    assert sha(inputs / name) == record['sha256'], name
relative_inputs = ['src/libraries/NetCoreAppLibrary.props',
                   'src/installer/pkg/sfx/Microsoft.NETCore.App/Directory.Build.props',
                   'src/installer/pkg/sfx/Microsoft.NETCore.App/Directory.Build.targets']
with tarfile.open(frame / 'm/src/runtime-8.0.31.tar.gz') as archive:
    for name in relative_inputs:
        assert (source / name).read_bytes() == archive.extractfile('runtime-8.0.31/' + name).read(), name
file_version = (frame / 'file-version.txt').read_text().strip()
assert file_version == '8.0.3126.42000'
manifest = json.loads((pack / 'source-payload.json').read_text())
assert manifest['reference_assemblies'] == 163 and manifest['file_version'] == file_version
for relative, record in manifest['files'].items():
    assert sha(pack / relative) == record['sha256'], relative
work.mkdir()
env = dict(os.environ, DOTNET_ROOT=native(Path('/clang64/lib/dotnet')),
           DOTNET_ROOT_X64=native(Path('/clang64/lib/dotnet')),
           DOTNET_CLI_HOME=native(work / 'cli-home'), NUGET_PACKAGES=native(work / 'cache'),
           DOTNET_CLI_TELEMETRY_OPTOUT='1', MSBuildEnableWorkloadResolver='false')
dotnet = '/clang64/lib/dotnet/dotnet.exe'
project = E.Element('Project', Sdk='Microsoft.NET.Sdk')
props = E.SubElement(project, 'PropertyGroup')
for name, value in dict(TargetFramework='net8.0', EnableDefaultItems='false',
                        EnableNETAnalyzers='false', NuGetAudit='false',
                        AssemblyName='SourceTargeting.PlatformManifestTasks').items():
    E.SubElement(props, name).text = value
items = E.SubElement(project, 'ItemGroup')
for name in pinned['files']:
    if name.endswith('.cs'):
        E.SubElement(items, 'Compile', Include=native(inputs / name))
for name in ['Microsoft.Build.Framework', 'Microsoft.Build.Utilities.Core']:
    reference = E.SubElement(items, 'Reference', Include=name)
    E.SubElement(reference, 'HintPath').text = native(Path('/clang64/lib/dotnet/sdk/8.0.131') / (name + '.dll'))
    E.SubElement(reference, 'Private').text = 'false'
E.ElementTree(project).write(work / 'Tasks.csproj', encoding='utf-8', xml_declaration=True)
(work / 'NuGet.config').write_text('<configuration><packageSources><clear/></packageSources></configuration>\n')
with (work / 'build.log').open('w') as log:
    subprocess.run([dotnet, 'build', native(work / 'Tasks.csproj'), '-c', 'Release',
                    '--configfile', native(work / 'NuGet.config'), '-p:UseSharedCompilation=false',
                    '-v:minimal'], env=env, check=True, stdout=log, stderr=subprocess.STDOUT)

# Preserve the original template items, conditions and architecture calculations.
# Only the repository-wide installer imports and unrelated packaging targets are
# omitted from this private invocation of the unchanged original C# task.
definition = E.Element('Project')
props = E.SubElement(definition, 'PropertyGroup')
for name, value in dict(LibrariesProjectRoot=native(source / 'src/libraries') + '/',
                        Configuration='Release', LibrariesConfiguration='Release',
                        TargetArchitecture='x64', BuildArchitecture='x64',
                        FileVersion=file_version, AssemblyVersion='8.0.0.0').items():
    E.SubElement(props, name).text = value
original = E.parse(source / relative_inputs[1]).getroot()
for element in original:
    if element.tag == 'PropertyGroup':
        wanted = {'UseDefaultPlatformManifestFallbackVersions', 'WindowsForwarderFileVersion',
                  'MicrosoftDiaSymReaderNativeFileVersion', 'MsQuicFileVersion',
                  '_hostArch', '_longNameTargetArch'}
        group = E.Element('PropertyGroup', element.attrib)
        for child in element:
            if child.tag in wanted:
                group.append(copy.deepcopy(child))
        if len(group):
            definition.append(group)
    elif element.tag == 'ItemGroup':
        group = E.Element('ItemGroup', element.attrib)
        for child in element:
            if child.tag in {'_pastShimFiles', 'PlatformManifestFileEntry'}:
                group.append(copy.deepcopy(child))
        if len(group):
            definition.append(group)
    elif element.tag == 'Import' and 'NetCoreAppLibrary.props' in element.get('Project', ''):
        definition.append(copy.deepcopy(element))
items = E.SubElement(definition, 'ItemGroup')
for ref in sorted((pack / 'ref/net8.0').glob('*.dll')):
    E.SubElement(items, 'FilesToPackage', Include=native(ref))
E.SubElement(definition, 'UsingTask', TaskName='Microsoft.DotNet.SharedFramework.Sdk.GeneratePlatformManifestEntriesFromTemplate',
             AssemblyFile=native(work / 'bin/Release/net8.0/SourceTargeting.PlatformManifestTasks.dll'))
target = E.SubElement(definition, 'Target', Name='Calculate')
long_name = E.parse(source / relative_inputs[2]).find("Target[@Name='AddLongNameDacToPlatformManifest']/ItemGroup")
assert long_name is not None
target.append(copy.deepcopy(long_name))
E.SubElement(target, 'WriteLinesToFile', File=native(work / 'templates.txt'),
             Lines="@(PlatformManifestFileEntry->'%(Identity)|%(IsNative)|%(FallbackAssemblyVersion)|%(FallbackFileVersion)')",
             Overwrite='true', Encoding='UTF-8')
call = E.SubElement(target, 'Microsoft.DotNet.SharedFramework.Sdk.GeneratePlatformManifestEntriesFromTemplate',
                    PlatformManifestEntryTemplates='@(PlatformManifestFileEntry)', Files='@(FilesToPackage)',
                    FallbackAssemblyVersion='$(AssemblyVersion)', FallbackFileVersion='$(FileVersion)')
E.SubElement(call, 'Output', TaskParameter='PlatformManifestEntries', ItemName='_PlatformManifestEntry')
E.SubElement(target, 'WriteLinesToFile', File=native(work / 'PlatformManifest.txt'),
             Lines="@(_PlatformManifestEntry->'%(Identity)|Microsoft.NETCore.App.Ref|%(AssemblyVersion)|%(FileVersion)')",
             Overwrite='true', Encoding='UTF-8')
E.ElementTree(definition).write(work / 'Calculate.proj', encoding='utf-8', xml_declaration=True)
with (work / 'calculate.log').open('w') as log:
    subprocess.run([dotnet, 'msbuild', native(work / 'Calculate.proj'), '-t:Calculate', '-v:minimal',
                    '-nologo'], env=env, check=True, stdout=log, stderr=subprocess.STDOUT)

# Independently compare every original task result with the actual reference
# identities or the original documented native/private fallback rule.
templates = [line.split('|') for line in (work / 'templates.txt').read_text(encoding='utf-8-sig').splitlines()]
actual = [line.split('|') for line in (work / 'PlatformManifest.txt').read_text(encoding='utf-8-sig').splitlines()]
ref_audit = json.loads((frame / 'reference-audit.json').read_text())
expected = []
for name, is_native, assembly_fallback, file_fallback in templates:
    if name.removesuffix('.dll') in ref_audit['references'] and (pack / 'ref/net8.0' / name).is_file():
        identity = (frame / 'm/build/refpack-8.0.31/out' / name).with_suffix('.meta').read_text().split()
        assembly, version = identity[1], file_version
    else:
        assembly = '' if is_native == 'true' else assembly_fallback or '8.0.0.0'
        version = '0.0.0.0' if is_native == 'true' and Path(name).suffix not in {'.dll', '.exe'} else file_fallback or file_version
    expected.append([name, 'Microsoft.NETCore.App.Ref', assembly, version])
assert actual == expected, [(a, e) for a, e in zip(actual, expected) if a != e]
names = {entry[0] for entry in actual}
assert {name + '.dll' for name, item in ref_audit['references'].items() if item['kind'] != 'stub'} <= names
private = {'System.Private.CoreLib.dll', 'System.Private.DataContractSerialization.dll',
           'System.Private.Uri.dll', 'System.Private.Xml.dll', 'System.Private.Xml.Linq.dll'}
assert private <= names
assert {'coreclr.dll', 'clrjit.dll', 'System.IO.Compression.Native.dll', 'clrcompression.dll',
        'libSystem.Native.so', 'mscordaccore_amd64_amd64_' + file_version + '.dll'} <= names
assert len(names) > 300, len(names)
shutil.copytree(pack, out)
shutil.copy2(work / 'PlatformManifest.txt', out / 'data/PlatformManifest.txt')
proof = dict(arcade_source_commit=commit, original_task_inputs=pinned,
             runtime_original_inputs={name: sha(source / name) for name in relative_inputs},
             compiler_build_log_sha256=sha(work / 'build.log'),
             invocation_project_sha256=sha(work / 'Calculate.proj'),
             raw_template_items=templates, entries=actual, unique_entry_count=len(names),
             platform_manifest_sha256=sha(out / 'data/PlatformManifest.txt'),
             all_163_source_reference_identities_checked=True,
             private_and_native_entries_are_conflict_resolution_metadata=True,
             optional_debugger_browser_nativeaot_payloads_not_built=True)
(out / 'source-platform-manifest.json').write_text(json.dumps(proof, indent=2) + '\n')
manifest['platform_manifest_sha256'] = proof['platform_manifest_sha256']
manifest['platform_manifest_source_proof_sha256'] = sha(out / 'source-platform-manifest.json')
manifest['platform_manifest_entries'] = len(actual)
manifest['platform_manifest_unique_entries'] = len(names)
(out / 'source-payload.json').write_text(json.dumps(manifest, indent=2) + '\n')
print('PASS exact original Arcade template task:', len(actual), 'entries,', len(names),
      'unique names; all163 reference identities and private/native fallback rules verified')
