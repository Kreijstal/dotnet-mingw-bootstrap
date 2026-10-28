"""Replay the unchanged SDK apphost task and compare the original project's exe."""
from pathlib import Path
import hashlib
import json
import os
import subprocess
import sys
import xml.etree.ElementTree as E

root, sdk, stage, out = (Path(p).resolve() for p in sys.argv[1:])
out.mkdir()
source = root / 'PowerShell-7.4.20'
console = source / 'src/powershell-win-core'
template = sdk / 'packs/Microsoft.NETCore.App.Host.win-x64/8.0.31/runtimes/win-x64/native/apphost.exe'
task = sdk / 'sdk/8.0.131/Sdks/Microsoft.NET.Sdk/tools/net8.0/Microsoft.NET.Build.Tasks.dll'
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
provenance = json.loads((stage / 'source-sdk-servicing.json').read_text())
assert sha(template) == provenance['replaced_host_files']['runtimes/win-x64/native/apphost.exe']
assert sha(task) == provenance['unchanged_seed_files']['clang64/lib/dotnet/' + task.relative_to(sdk).as_posix()]
intermediate = console / 'obj/Release/net8.0/pwsh.dll'
assert intermediate.read_bytes() == (console / 'bin/Release/net8.0/pwsh.dll').read_bytes()
native = lambda p: subprocess.check_output(['cygpath', '-am', str(p)], text=True).strip()
project = E.Element('Project')
E.SubElement(project, 'UsingTask', TaskName='CreateAppHost', AssemblyFile=native(task))
target = E.SubElement(project, 'Target', Name='Replay')
E.SubElement(target, 'CreateAppHost', AppHostSourcePath=native(template),
             AppHostDestinationPath=native(out / 'pwsh.exe'), AppBinaryName='pwsh.dll',
             IntermediateAssembly=native(intermediate), WindowsGraphicalUserInterface='false', EnableMacOSCodeSign='false')
E.ElementTree(project).write(out / 'Replay.proj', encoding='utf-8', xml_declaration=True)
env = dict(os.environ, DOTNET_ROOT=native(sdk), DOTNET_ROOT_X64=native(sdk),
           DOTNET_CLI_HOME=native(out / 'cli-home'), DOTNET_CLI_TELEMETRY_OPTOUT='1',
           MSBuildEnableWorkloadResolver='false', DOTNET_NOLOGO='1')
subprocess.run([str(sdk / 'dotnet.exe'), 'msbuild', native(out / 'Replay.proj'), '-t:Replay', '-nologo', '-v:minimal'], env=env, check=True)
actual = console / 'bin/Release/net8.0/pwsh.exe'
assert actual.read_bytes() == (out / 'pwsh.exe').read_bytes(), 'Apphost replay differs from original project output'
assert b'pwsh.dll' in actual.read_bytes()
record = dict(original_project='src/powershell-win-core/powershell-win-core.csproj',
              apphost_template=str(template), apphost_template_sha256=sha(template),
              original_source_sdk_task=str(task), original_source_sdk_task_sha256=sha(task),
              intermediate_assembly_sha256=sha(intermediate), native_executable_sha256=sha(actual),
              exact_original_sdk_task_replay_matches=True, template_source_version='8.0.31')
(root / 'native-console-source.json').write_text(json.dumps(record, indent=2) + '\n')
print('PASS original project native pwsh.exe exactly matches unchanged source SDK task replay using freshly source-built8.0.31 apphost and original assembly resources')
