using System;
using System.Collections.Generic;
using System.Linq;
using System.Management.Automation;
using System.Management.Automation.Runspaces;
using System.Reflection;
class Program
{
 static void Require(bool value,string message){if(!value)throw new Exception(message);}
 static void Main()
 {
  var sma=typeof(PowerShell).Assembly;
  Require(sma.GetName().Version==new Version(7,4,6,500),"Original frozen PowerShell assembly identity");
  Require(Convert.ToHexString(sma.GetName().GetPublicKeyToken()).ToLowerInvariant()=="31bf3856ad364e35","PowerShell public identity");
  Require(sma.GetCustomAttribute<AssemblyInformationalVersionAttribute>().InformationalVersion=="7.4.20 SHA: 013edf910bbe781f503c27370941ce54d02d3f33","Pinned source version");
  var initial=InitialSessionState.CreateDefault2();
  initial.Variables.Add(new SessionStateVariableEntry("PSModuleAutoLoadingPreference","None","Owned source check"));
  using var runspace=RunspaceFactory.CreateRunspace(initial);runspace.Open();
  using var shell=PowerShell.Create();shell.Runspace=runspace;
  object[] Invoke(string source)
  {
   shell.Commands.Clear();shell.Streams.Error.Clear();shell.AddScript(source);var result=shell.Invoke();
   Require(!shell.HadErrors,string.Join("\n",shell.Streams.Error.Select(e=>e.ToString())));
   return result.Select(o=>o.BaseObject).ToArray();
  }
  Require((string)Invoke("$PSVersionTable.PSVersion.ToString()").Single()=="7.4.20","Generated version constants and PSVersionTable");
  Require(Convert.ToInt32(Invoke("& { param($n) $sum=0; foreach($i in 1..$n){$sum += $i}; $sum } 100").Single())==5050,"Actual parser/compiler/functions/loop invocation");
  var pipeline=Invoke("1..20 | ForEach-Object { $_ * $_ } | Where-Object { ($_ % 2) -eq 0 }");
  Require(pipeline.Length==10&&Convert.ToInt32(pipeline[0])==4&&Convert.ToInt32(pipeline.Last())==400,"Actual built-in pipeline callbacks");
  Require((string)Invoke("$x=[System.Xml.XmlDocument]::new(); $x.LoadXml('<root><value>42</value></root>'); $x.root.value").Single()=="42","Generated .NET type catalog and XML adapter");
  Require((string)Invoke("class Owned { [int] $Value; Owned([int]$n){$this.Value=$n} [int] Add([int]$x){return $this.Value+$x} }; [Owned]::new(40).Add(2).ToString()").Single()=="42","Actual PowerShell class compiler");
  Require((string)Invoke("try { throw 'owned failure' } catch { $_.Exception.Message }").Single()=="owned failure","Exceptions and PowerShell error records");
  var ast=System.Management.Automation.Language.Parser.ParseInput("function Owned { param([int] $n) $n + 1 }",out var tokens,out var errors);
  Require(errors.Length==0&&tokens.Length>10&&ast.FindAll(n=>n is System.Management.Automation.Language.FunctionDefinitionAst,true).Count()==1,"Source parser AST API");
  foreach(System.Diagnostics.ProcessModule module in System.Diagnostics.Process.GetCurrentProcess().Modules)
   Require(!module.FileName.Replace('\\','/').Contains("/Microsoft.NET/Framework",StringComparison.OrdinalIgnoreCase),"Framework implementation loaded");
  Console.WriteLine("PASS source-built PowerShell7.4.20 core identity/version generator, real runspace, parser/AST/compiler, functions/loops, pipelines, XML type resolution, classes, errors and no Framework implementations");
 }
}
