#!/usr/bin/env bash
set -euo pipefail
layout=$(cd "${1:?usage: check-json-binding.sh RUNTIME_LAYOUT NEW_OUTPUT}" && pwd)
mkdir "${2:?missing new output}"; out=$(cd "$2" && pwd)
export DOTNET_ROOT=$(cygpath -am /clang64/lib/dotnet)
export DOTNET_ROOT_X64=$DOTNET_ROOT
export DOTNET_CLI_HOME=$(cygpath -am "$out/cli-home") NUGET_PACKAGES=$(cygpath -am "$out/cache")
export DOTNET_CLI_TELEMETRY_OPTOUT=1 MSBuildEnableWorkloadResolver=false
cat > "$out/Check.csproj" <<'XML'
<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><TargetFramework>net8.0</TargetFramework><OutputType>Exe</OutputType><UseAppHost>false</UseAppHost><EnableNETAnalyzers>false</EnableNETAnalyzers><NuGetAudit>false</NuGetAudit></PropertyGroup><ItemGroup><PackageReference Include="System.Text.Json" Version="[8.0.5]"/></ItemGroup></Project>
XML
cat > "$out/NuGet.config" <<XML
<configuration><packageSources><clear/><add key="installed-source" value="$(cygpath -am /clang64/lib/dotnet/library-packs)"/></packageSources></configuration>
XML
cat > "$out/Program.cs" <<'CS'
using System;
using System.Diagnostics;
using System.IO;
using System.Reflection;
using System.Security.Cryptography;
using System.Text.Json;
class Program
{
 static void Main(string[] args)
 {
  var assembly=typeof(JsonSerializer).Assembly;
  Console.WriteLine("Runtime: "+Environment.Version);
  Console.WriteLine("JSON path: "+assembly.Location);
  Console.WriteLine("JSON file version: "+FileVersionInfo.GetVersionInfo(assembly.Location).FileVersion);
  Console.WriteLine("JSON source: "+assembly.GetCustomAttribute<AssemblyInformationalVersionAttribute>().InformationalVersion);
  if(Environment.Version!=new Version(8,0,31))throw new Exception("Actual serviced runtime required");
  string expected=Path.Combine(args[0],"System.Text.Json.dll");
  if(!SHA256.HashData(File.ReadAllBytes(expected)).AsSpan().SequenceEqual(SHA256.HashData(File.ReadAllBytes(assembly.Location))))
   throw new Exception("Older private JSON overrides the serviced framework bytes");
  if(JsonSerializer.Deserialize<int[]>("[1,2,3]").Length!=3)throw new Exception("JSON execution");
  Console.WriteLine("PASS source NuGet consumer selects actual serviced framework JSON implementation");
 }
}
CS
/clang64/lib/dotnet/dotnet.exe build "$(cygpath -am "$out/Check.csproj")" -c Release --configfile "$(cygpath -am "$out/NuGet.config")" \
 -p:UseSharedCompilation=false -p:CscToolPath="$(cygpath -am /clang64/lib/dotnet/compiler-packs/roslyn-4.9.2)" -p:CscToolExe=csc49.cmd -v:minimal
export DOTNET_ROOT=$(cygpath -am "$layout") DOTNET_ROOT_X64=$(cygpath -am "$layout") DOTNET_MULTILEVEL_LOOKUP=0
timeout 60 "$layout/dotnet.exe" exec --fx-version 8.0.31 "$(cygpath -am "$out/bin/Release/net8.0/Check.dll")" "$(cygpath -am "$layout/shared/Microsoft.NETCore.App/8.0.31")"
