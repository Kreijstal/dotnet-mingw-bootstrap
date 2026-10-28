#!/usr/bin/env bash
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "${1:?usage: check-localaccounts-api.sh BUILD_ROOT NEW_OUTPUT}" && pwd)
mkdir "${2:?missing output}"; out=$(cd "$2" && pwd)
export DOTNET_ROOT=$(cygpath -am /clang64/lib/dotnet) DOTNET_ROOT_X64=$(cygpath -am /clang64/lib/dotnet)
export DOTNET_CLI_HOME=$(cygpath -am "$out/cli-home") NUGET_PACKAGES=$(cygpath -am "$out/empty-packages")
export DOTNET_CLI_TELEMETRY_OPTOUT=1 MSBuildEnableWorkloadResolver=false DOTNET_NOLOGO=1
dotnet=/clang64/lib/dotnet/dotnet.exe
"$dotnet" msbuild "$(cygpath -am "$root/PowerShell-7.4.20/src/Microsoft.PowerShell.LocalAccounts/Microsoft.PowerShell.LocalAccounts.csproj")" \
 -p:Configuration=Release -getProperty:DefineConstants -nologo > "$out/define-constants.txt"
/usr/bin/python3 - "$root" "$out" <<'PY'
from pathlib import Path
import base64,hashlib,json,re,subprocess,sys,tarfile
root,out=(Path(p) for p in sys.argv[1:]);source=root/'PowerShell-7.4.20'
archive=root/'powershell-7.4.20.tar.gz'
assert hashlib.sha256(archive.read_bytes()).hexdigest()=='3dac352752acd64a0502c1f002ecf65eca177d2919bea3bcefbf8856afedd9eb'
native=lambda p:subprocess.check_output(['cygpath','-am',str(p)],text=True).strip().replace('\\','/')
prefix=native(source)+'/'
with tarfile.open(archive) as tar:
 original={p.name.removeprefix('PowerShell-7.4.20/'):tar.extractfile(p).read().decode() for p in tar if p.isfile() and p.name.endswith('.cs') and p.name.startswith('PowerShell-7.4.20/src/Microsoft.PowerShell.LocalAccounts/')}
records=[]
for line in (root/'manifests/Microsoft.PowerShell.LocalAccounts-compile.txt').read_text(encoding='utf-8-sig').splitlines():
 path=line.replace('\\','/');assert path.lower().startswith(prefix.lower());relative=path[len(prefix):]
 adapted=(source/relative).read_text();records.append(dict(path=path,original=original.get(relative,adapted),adapted=adapted))
references=(root/'manifests/Microsoft.PowerShell.LocalAccounts-references.txt').read_text(encoding='utf-8-sig').splitlines()
symbols=(out/'define-constants.txt').read_text().strip().splitlines()[-1].split(';')
assert 'CORECLR' in symbols and all(re.fullmatch(r'[A-Za-z_]\w*',p) for p in symbols),symbols
(out/'inputs.json').write_text(json.dumps(dict(sources=records,references=references,symbols=symbols,original_public_key=base64.b64encode((source/'src/signing/visualstudiopublic.snk').read_bytes()).decode())))
PY
cp "$here/CheckLocalAccountsApi.cs" "$out/Program.cs"
cat > "$out/Check.csproj" <<'PROJECT'
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><UseAppHost>false</UseAppHost><EnableDefaultItems>false</EnableDefaultItems><NuGetAudit>false</NuGetAudit></PropertyGroup><ItemGroup><Compile Include="Program.cs"/><PackageReference Include="Microsoft.CodeAnalysis.CSharp" Version="[4.9.2]"/></ItemGroup></Project>
PROJECT
"$dotnet" build "$(cygpath -am "$out/Check.csproj")" -c Release --configfile "$(cygpath -am "$root/NuGet.config")" -p:UseSharedCompilation=false -v:minimal
"$dotnet" "$(cygpath -am "$out/bin/Release/net8.0/Check.dll")" "$(cygpath -am "$out/inputs.json")" "$(cygpath -am "$out/api-results.json")" \
 "$(cygpath -am "$root/PowerShell-7.4.20/src/Microsoft.PowerShell.LocalAccounts/bin/Release/net8.0/Microsoft.PowerShell.LocalAccounts.dll")"
