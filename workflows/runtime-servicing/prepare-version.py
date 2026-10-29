"""Run the exact upstream Arcade version task with a reproducible source date."""
from pathlib import Path
import datetime
import hashlib
import json
import os
import subprocess
import sys
import tarfile
import xml.etree.ElementTree as ET

root = Path(sys.argv[1]).resolve()
# the SDK of the environment that builds (CLANG64 or UCRT64)
prefix = Path(os.environ.get('MINGW_PREFIX', '/clang64'))
here = Path(__file__).resolve().parent
source = root / 'm/src/runtime-8.0.31-native'
task = here / 'inputs/CalculateAssemblyAndFileVersions.cs'
assert hashlib.sha256(task.read_bytes()).hexdigest() == 'e0bc140b716fb1c43b4609e877a357b422cdd4b213021bd919df04e963c8a02f'
details = ET.parse(source / 'eng/Version.Details.xml')
dependency = details.find(".//Dependency[@Name='Microsoft.DotNet.Arcade.Sdk']")
assert dependency.findtext('Sha') == '444c2ef3c414558e2e409ee346f1faebb19687eb'
with tarfile.open(root / 'm/src/runtime-8.0.31.tar.gz') as archive:
    timestamp = next(iter(archive)).mtime
date = datetime.datetime.fromtimestamp(timestamp, datetime.timezone.utc)
build_number = date.strftime('%Y%m%d') + '.0'
out = root / 'version-build'
out.mkdir(exist_ok=True)
native = lambda path: subprocess.check_output(['cygpath', '-am', str(path)], text=True).strip()
project = ET.Element('Project', Sdk='Microsoft.NET.Sdk')
props = ET.SubElement(project, 'PropertyGroup')
for key, value in dict(TargetFramework='net8.0', EnableDefaultItems='false', EnableNETAnalyzers='false', NuGetAudit='false', AssemblyName='RuntimeServicing.VersionBuildTasks').items():
    ET.SubElement(props, key).text = value
items = ET.SubElement(project, 'ItemGroup')
ET.SubElement(items, 'Compile', Include=native(task))
for name in ['Microsoft.Build.Framework', 'Microsoft.Build.Utilities.Core', 'NuGet.Versioning']:
    reference = ET.SubElement(items, 'Reference', Include=name)
    ET.SubElement(reference, 'HintPath').text = native(prefix / 'lib/dotnet/sdk/8.0.131' / (name + '.dll'))
    ET.SubElement(reference, 'Private').text = 'false'
ET.indent(project)
ET.ElementTree(project).write(out / 'Build.csproj', encoding='utf-8', xml_declaration=True)
(out / 'NuGet.config').write_text('<configuration><packageSources><clear/></packageSources></configuration>\n')
env = dict(os.environ, DOTNET_ROOT=native(prefix / 'lib/dotnet'), DOTNET_ROOT_X64=native(prefix / 'lib/dotnet'), DOTNET_CLI_TELEMETRY_OPTOUT='1', MSBuildEnableWorkloadResolver='false', DOTNET_CLI_HOME=native(out / 'cli-home'), NUGET_PACKAGES=native(out / 'cache'))
dotnet = str(prefix / 'lib/dotnet/dotnet.exe')
with (out / 'build.log').open('w') as log:
    subprocess.run([dotnet, 'build', native(out / 'Build.csproj'), '-c', 'Release', '--configfile', native(out / 'NuGet.config'), '-p:UseSharedCompilation=false', '-v:minimal'], env=env, check=True, stdout=log, stderr=subprocess.STDOUT)
definition = ET.Element('Project')
ET.SubElement(definition, 'UsingTask', TaskName='Microsoft.DotNet.Arcade.Sdk.CalculateAssemblyAndFileVersions', AssemblyFile=native(out / 'bin/Release/net8.0/RuntimeServicing.VersionBuildTasks.dll'))
target = ET.SubElement(definition, 'Target', Name='Calculate')
vectors = [('1.0.0', '20190102.3', '1.0.19.5203'), ('2.1.800', '20190314.5', '2.108.19.16405'), ('65535.654.9999', '20991231.99', '65535.65499.9999.63199')]
for index, (prefix, number, expected) in enumerate(vectors):
    call = ET.SubElement(target, 'Microsoft.DotNet.Arcade.Sdk.CalculateAssemblyAndFileVersions', VersionPrefix=prefix, BuildNumber=number)
    ET.SubElement(call, 'Output', TaskParameter='FileVersion', PropertyName='CheckedFileVersion' + str(index))
    ET.SubElement(target, 'Error', Condition="'$(CheckedFileVersion" + str(index) + ")' != '" + expected + "'", Text='Upstream version-vector failure')
call = ET.SubElement(target, 'Microsoft.DotNet.Arcade.Sdk.CalculateAssemblyAndFileVersions', VersionPrefix='8.0.31', BuildNumber=build_number)
ET.SubElement(call, 'Output', TaskParameter='FileVersion', PropertyName='ServicedFileVersion')
ET.SubElement(target, 'WriteLinesToFile', File=native(root / 'file-version.txt'), Lines='$(ServicedFileVersion)', Overwrite='true')
ET.indent(definition)
ET.ElementTree(definition).write(out / 'Calculate.proj', encoding='utf-8', xml_declaration=True)
with (out / 'calculate.log').open('w') as log:
    subprocess.run([dotnet, 'msbuild', native(out / 'Calculate.proj'), '-t:Calculate', '-nologo', '-v:minimal'], env=env, check=True, stdout=log, stderr=subprocess.STDOUT)
version = (root / 'file-version.txt').read_text(encoding='utf-8-sig').strip()
assert version == '8.0.3126.42000', version
(root / 'file-version.txt').write_text(version + '\n')
(root / 'version-build-inputs.json').write_text(json.dumps(dict(arcade_source_commit=dependency.findtext('Sha'), original_task_sha256=hashlib.sha256(task.read_bytes()).hexdigest(), runtime_source_date_utc=date.isoformat(), build_number=build_number, build_number_origin='Pinned runtime archive commit date; reproducible source build, not an official Microsoft build number', version_prefix='8.0.31', file_version=version, upstream_task_vectors_checked=len(vectors), private_build_only_task=True), indent=2) + '\n')
print('PASS exact original Arcade version task and three upstream vectors:', build_number, '->', version)
