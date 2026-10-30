"""Keep the audited source SDK unchanged and add real source8.0.31 host/runtime packs."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import tarfile
import xml.etree.ElementTree as E

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('sdk_seed', type=Path)
parser.add_argument('prepared_host', type=Path)
parser.add_argument('destination', type=Path)
args = parser.parse_args()
# the environment that builds: its SDK package seed, runtime8-servicing and tools
prefix = Path(os.environ.get('MINGW_PREFIX', '/clang64'))
package_prefix = os.environ.get('MINGW_PACKAGE_PREFIX', 'mingw-w64-clang-x86_64')
tree = prefix.name + '/'
seed, host, out = (p.resolve() for p in [args.sdk_seed, args.prepared_host, args.destination])
assert not out.exists()
sha = lambda data: hashlib.sha256(data).hexdigest()
out.mkdir()
baseline = {}
info = None
with subprocess.Popen(['zstd', '-dc', str(seed)], stdout=subprocess.PIPE) as process:
    with tarfile.open(fileobj=process.stdout, mode='r|') as tar:
        for item in tar:
            if item.isdir():
                continue
            assert item.isfile() and '..' not in Path(item.name).parts and not item.name.startswith('/'), item.name
            data = tar.extractfile(item).read()
            if item.name == '.PKGINFO':
                info = data.decode()
            if item.name.startswith('.'):
                continue
            assert item.name.startswith(tree), item.name
            target = out / item.name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
            target.chmod(item.mode & 0o777)
            baseline[item.name] = sha(data)
    while process.stdout.read(1024 * 1024):
        pass
    assert process.wait() == 0
assert re.search(r'^pkgver = 8\.0\.131-\d+$', info, re.M), info
assert f'pkgname = {package_prefix}-dotnet-sdk\n' in info

dotnet_root = out / tree / 'lib/dotnet'
versions = dotnet_root / 'sdk/8.0.131/Microsoft.NETCoreSdk.BundledVersions.props'
before = versions.read_bytes()
encoding = 'utf-16-le' if before.startswith(b'\xff\xfe') else 'utf-16-be' if before.startswith(b'\xfe\xff') else 'utf-8'
old = 'LatestRuntimeFrameworkVersion="8.0.0"'.encode(encoding)
new = 'LatestRuntimeFrameworkVersion="8.0.31"'.encode(encoding)
assert before.count(old) == 1
after = before.replace(old, new)
prior = E.fromstring(before)
current = E.fromstring(after)
for a, b in zip(prior.iter(), current.iter()):
    if a.attrib != b.attrib:
        assert a.tag == 'KnownFrameworkReference' and a.get('Include') == 'Microsoft.NETCore.App' and a.get('TargetFramework') == 'net8.0'
        assert b.attrib == dict(a.attrib, LatestRuntimeFrameworkVersion='8.0.31')
versions.write_bytes(after)
version_adaptation = dict(path=versions.relative_to(out).as_posix(), original_sha256=sha(before),
    adapted_sha256=sha(after), change='KnownFrameworkReference net8.0 LatestRuntimeFrameworkVersion8.0.0 ->8.0.31; minimum runtime and all other SDK metadata retained')
host_pack = dotnet_root / 'packs/Microsoft.NETCore.App.Host.win-x64/8.0.31'
new_host = host / 'host-layout/packs/Microsoft.NETCore.App.Host.win-x64/8.0.31'
assert (new_host / 'runtimes/win-x64/native/apphost.exe').is_file()
assert {p.relative_to(new_host).as_posix() for p in new_host.rglob('*') if p.is_file()} == {
    p.relative_to(host_pack).as_posix() for p in host_pack.rglob('*') if p.is_file()}
host_files = {}
for path in new_host.rglob('*'):
    if path.is_file():
        relative = path.relative_to(new_host)
        shutil.copyfile(path, host_pack / relative)
        host_files[relative.as_posix()] = sha(path.read_bytes())

doc = prefix / 'share/doc/dotnet-runtime8-servicing'
runtime = prefix / 'lib/dotnet/shared/Microsoft.NETCore.App/8.0.31'
installed = json.loads((doc / 'source-payload.json').read_text())
assert installed['source_commit'] == '1219a42122cf5190c7f512850557e38c422430dc'
assert re.fullmatch(package_prefix + r'-dotnet-runtime8-servicing 8\.0\.31-\d+', subprocess.check_output(['pacman', '-Q', package_prefix + '-dotnet-runtime8-servicing'], text=True).strip())
pack = dotnet_root / 'packs/Microsoft.NETCore.App.Runtime.win-x64/8.0.31'
managed = set((doc / 'required-framework.txt').read_text().splitlines())
assert len(managed) == 168
runtime_files = {}
native_names = ['coreclr.dll', 'clrjit.dll', 'mscorrc.dll', 'clretwrc.dll', 'hostpolicy.dll', 'System.IO.Compression.Native.dll']
for name in sorted(managed | {Path(p).stem for p in native_names}):
    filename = name + '.dll'
    source = runtime / filename
    relative = ('runtimes/win-x64/lib/net8.0/' if name in managed and name != 'System.Private.CoreLib' else 'runtimes/win-x64/native/') + filename
    digest = sha(source.read_bytes())
    assert installed['files']['shared/Microsoft.NETCore.App/8.0.31/' + filename] == digest, source
    target = pack / relative
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, target)
    runtime_files[relative] = dict(sha256=digest, managed=name in managed, installed_source=source.as_posix())
relative = 'runtimes/win-x64/native/hostfxr.dll'
source = prefix / 'lib/dotnet/host/fxr/8.0.31/hostfxr.dll'
assert sha(source.read_bytes()) == installed['files']['host/fxr/8.0.31/hostfxr.dll']
shutil.copyfile(source, pack / relative)
runtime_files[relative] = dict(sha256=sha(source.read_bytes()), managed=False, installed_source=source.as_posix())
assert len(runtime_files) == 175

here = Path(__file__).resolve().parent
inputs = here / 'inputs'
pinned = json.loads((inputs / 'source-inputs.json').read_text())
assert pinned['source_commit'] == '444c2ef3c414558e2e409ee346f1faebb19687eb'
for name, record in pinned['files'].items():
    assert sha((inputs / name).read_bytes()) == record['sha256'], name
work = out.with_name(out.name + '-runtime-list-task')
work.mkdir()
native = lambda p: subprocess.check_output(['cygpath', '-am', str(p)], text=True).strip()
project = E.Element('Project', Sdk='Microsoft.NET.Sdk')
props = E.SubElement(project, 'PropertyGroup')
for name, value in dict(TargetFramework='net8.0', EnableDefaultItems='false', NuGetAudit='false').items():
    E.SubElement(props, name).text = value
items = E.SubElement(project, 'ItemGroup')
for name in ['BuildTask.cs', 'FileUtilities.cs', 'CreateFrameworkListFile.cs']:
    E.SubElement(items, 'Compile', Include=native(inputs / name))
for name in ['Microsoft.Build.Framework', 'Microsoft.Build.Utilities.Core']:
    ref = E.SubElement(items, 'Reference', Include=name)
    E.SubElement(ref, 'HintPath').text = native(prefix / 'lib/dotnet/sdk/8.0.131' / (name + '.dll'))
    E.SubElement(ref, 'Private').text = 'false'
E.ElementTree(project).write(work / 'Tasks.csproj', encoding='utf-8', xml_declaration=True)
(work / 'NuGet.config').write_text('<configuration><packageSources><clear/></packageSources></configuration>\n')
env = dict(os.environ, DOTNET_ROOT=native(prefix / 'lib/dotnet'), DOTNET_ROOT_X64=native(prefix / 'lib/dotnet'),
           DOTNET_CLI_HOME=native(work / 'cli-home'), NUGET_PACKAGES=native(work / 'cache'),
           DOTNET_CLI_TELEMETRY_OPTOUT='1', MSBuildEnableWorkloadResolver='false')
dotnet = str(prefix / 'lib/dotnet/dotnet.exe')
with (work / 'build.log').open('w') as log:
    subprocess.run([dotnet, 'build', native(work / 'Tasks.csproj'), '-c', 'Release', '--configfile', native(work / 'NuGet.config'), '-p:UseSharedCompilation=false', '-v:minimal'], env=env, stdout=log, stderr=subprocess.STDOUT, check=True)
definition = E.Element('Project')
E.SubElement(definition, 'UsingTask', TaskName='Microsoft.DotNet.SharedFramework.Sdk.CreateFrameworkListFile', AssemblyFile=native(work / 'bin/Release/net8.0/Tasks.dll'))
items = E.SubElement(definition, 'ItemGroup')
for relative, record in sorted(runtime_files.items()):
    item = E.SubElement(items, 'RuntimeFile', Include=native(pack / relative))
    E.SubElement(item, 'TargetPath').text = str(Path(relative).parent).replace('\\', '/')
    E.SubElement(item, 'IsNative').text = 'false' if record['managed'] else 'true'
for relative in sorted(host_files):
    if Path(relative).suffix in {'.dll', '.exe'}:
        item = E.SubElement(items, 'HostPEFile', Include=native(host_pack / relative))
        E.SubElement(item, 'TargetPath').text = str(Path(relative).parent).replace('\\', '/')
        E.SubElement(item, 'IsNative').text = 'true'
runtime_props = E.parse(host / 'm/src/runtime-8.0.31-native/src/installer/pkg/sfx/Microsoft.NETCore.App/Directory.Build.props')
friendly_name = runtime_props.findtext('.//SharedFrameworkFriendlyName')
single_file = [p.get('Include') for p in runtime_props.findall('.//SingleFileHostIncludeFilename')]
assert friendly_name == '.NET Runtime' and single_file == ['NONE']
for name, value in dict(TargetFrameworkIdentifier='.NETCoreApp', TargetFrameworkVersion='8.0', FrameworkName='Microsoft.NETCore.App', Name=friendly_name).items():
    E.SubElement(items, 'RootAttribute', Include=name, Value=value)
target = E.SubElement(definition, 'Target', Name='Create')
E.SubElement(target, 'Microsoft.DotNet.SharedFramework.Sdk.CreateFrameworkListFile', Files='@(RuntimeFile)',
             TargetFile=native(pack / 'data/RuntimeList.xml'), TargetFilePrefixes='runtimes/',
             RootAttributes='@(RootAttribute)', SingleFileHostIncludeFilenames=';'.join(single_file))
E.SubElement(target, 'Microsoft.DotNet.SharedFramework.Sdk.CreateFrameworkListFile', Files='@(HostPEFile)',
             TargetFile=native(work / 'HostPEVersions.xml'), TargetFilePrefixes='runtimes/')
E.ElementTree(definition).write(work / 'Create.proj', encoding='utf-8', xml_declaration=True)
with (work / 'create.log').open('w') as log:
    subprocess.run([dotnet, 'msbuild', native(work / 'Create.proj'), '-t:Create', '-nologo', '-v:minimal'], env=env, stdout=log, stderr=subprocess.STDOUT, check=True)
manifest = E.parse(pack / 'data/RuntimeList.xml').getroot()
assert len(manifest) == 175
for item in manifest:
    assert item.get('FileVersion') == '8.0.3126.42000', item.attrib
    assert (item.get('Type') == 'Managed') == runtime_files[item.get('Path')]['managed'], item.attrib
    if item.get('Type') == 'Managed':
        assert item.get('AssemblyName') and item.get('PublicKeyToken'), item.attrib
host_metadata = E.parse(work / 'HostPEVersions.xml').getroot()
assert len(host_metadata) == 3
for item in host_metadata:
    expected = '0.0.0.0' if Path(item.get('Path')).name == 'apphost.exe' else '8.0.3126.42000'
    assert item.get('Type') == 'Native' and item.get('FileVersion') == expected, item.attrib
record = dict(sdk_source_seed=seed.name, sdk_source_seed_sha256=sha(seed.read_bytes()),
    unchanged_seed_files={p: digest for p, digest in baseline.items() if '/packs/Microsoft.NETCore.App.Host.win-x64/8.0.31/' not in p and p != version_adaptation['path']},
    sdk_metadata_adaptation=version_adaptation,
    replaced_host_files=host_files, runtime_pack_files=runtime_files,
    host_pe_metadata={item.get('Path'):item.attrib for item in host_metadata},
    runtime_list_sha256=sha((pack / 'data/RuntimeList.xml').read_bytes()),
    original_arcade_runtime_list_task=pinned, managed_runtime_assemblies=168, runtime_pack_assets=175,
    sdk_managed_assemblies_recompiled=False, host_source_version='8.0.31', broader_sdk_compatibility_complete=False)
(out / 'source-sdk-servicing.json').write_text(json.dumps(record, indent=2) + '\n')
print('PASS audited source SDK seed preserved; fresh8.0.31 host pack and175 source runtime assets; unchanged upstream RuntimeList task/PE versions')
