#!/usr/bin/env bash
set -euo pipefail
out=$(cd "${1:?usage: build-roslyn414.sh PREPARED_OUTPUT}" && pwd)
source="$out/roslyn-source"
export DOTNET_ROOT=$(cygpath -am /clang64/lib/dotnet) DOTNET_ROOT_X64=$(cygpath -am /clang64/lib/dotnet)
export DOTNET_CLI_HOME=$(cygpath -am "$out/cli-home") NUGET_PACKAGES=$(cygpath -am "$out/cache")
export DOTNET_CLI_TELEMETRY_OPTOUT=1 MSBuildEnableWorkloadResolver=false
dotnet=/clang64/lib/dotnet/dotnet.exe
mkdir -p "$out/feed"
if [[ ! -d "$out/generated" ]]; then
for name in CSharpSyntaxGenerator BoundTreeGenerator CSharpErrorFactsGenerator; do
 "$dotnet" build "$(cygpath -am "$out/$name/Build.csproj")" -c Release --configfile "$(cygpath -am "$out/NuGet.config")" -p:UseSharedCompilation=false -v:minimal > "$out/$name/build.log" 2>&1
done
mkdir "$out/generated"
"$dotnet" "$(cygpath -am "$out/CSharpSyntaxGenerator/bin/Release/net8.0/CSharpSyntaxGenerator.dll")" "$(cygpath -am "$source/src/Compilers/CSharp/Portable/Syntax/Syntax.xml")" "$(cygpath -am "$out/generated")" > "$out/syntax-generation.log" 2>&1
"$dotnet" "$(cygpath -am "$out/BoundTreeGenerator/bin/Release/net8.0/BoundTreeGenerator.dll")" CSharp "$(cygpath -am "$source/src/Compilers/CSharp/Portable/BoundTree/BoundNodes.xml")" "$(cygpath -am "$out/generated/BoundNodes.xml.Generated.cs")" > "$out/bound-generation.log" 2>&1
"$dotnet" "$(cygpath -am "$out/CSharpErrorFactsGenerator/bin/Release/net8.0/CSharpErrorFactsGenerator.dll")" "$(cygpath -am "$source/src/Compilers/CSharp/Portable/Errors/ErrorCode.cs")" "$(cygpath -am "$out/generated/ErrorFacts.Generated.cs")" > "$out/errors-generation.log" 2>&1
fi
/usr/bin/python3 "$(dirname "$0")/check-generated.py" "$source" "$out/generated" > "$out/generated-audit.log"
for name in Microsoft.CodeAnalysis Microsoft.CodeAnalysis.CSharp; do
 "$dotnet" pack "$(cygpath -am "$out/$name/Build.csproj")" -c Release --configfile "$(cygpath -am "$out/NuGet.config")" -o "$(cygpath -am "$out/feed")" -p:UseSharedCompilation=false -v:minimal > "$out/$name/build.log" 2>&1
 printf 'PASS original source Roslyn4.14 package: %s\n' "$name"
done
