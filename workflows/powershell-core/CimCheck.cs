using System;
using System.Linq;
using System.Management.Automation;
using System.Management.Automation.Runspaces;
using System.Reflection;
using Microsoft.Management.Infrastructure;
class Program
{
 static void Require(bool value,string message){if(!value)throw new Exception(message);}
 static void Main()
 {
  var sma=typeof(PowerShell).Assembly;var mmi=typeof(CimInstance).Assembly;
  Require(sma.GetName().Version==new Version(7,4,6,500)&&mmi.GetName().Version==new Version(1,0,0,0),"Source PowerShell/MMI3 identities");
  Require(sma.GetCustomAttribute<AssemblyInformationalVersionAttribute>().InformationalVersion.Contains("013edf910bbe781f503c27370941ce54d02d3f33"),"Pinned SMA source identity");
  var initial=InitialSessionState.CreateDefault2();initial.Variables.Add(new SessionStateVariableEntry("PSModuleAutoLoadingPreference","None","Owned CIM check"));
  using var runspace=RunspaceFactory.CreateRunspace(initial);runspace.Open();using var shell=PowerShell.Create();shell.Runspace=runspace;
  PSObject[] Invoke(string source,bool handledNativeError=false)
  {
   shell.Commands.Clear();shell.Streams.Error.Clear();shell.AddScript(source);var result=shell.Invoke();
   if(shell.HadErrors&&!handledNativeError)
   {
    foreach(var error in shell.Streams.Error)Console.WriteLine("ERROR "+error.FullyQualifiedErrorId+": "+error.Exception);
    throw new Exception("PowerShell invocation failed: "+source+"; state="+shell.InvocationStateInfo.State+"; reason="+shell.InvocationStateInfo.Reason);
   }
   if(handledNativeError)Require(shell.Streams.Error.Count==0&&shell.InvocationStateInfo.State==PSInvocationState.Completed,"Caught native errors must leave no unhandled error records");
   Console.WriteLine("PASS invocation "+source);return result.ToArray();
  }
  string assembly=System.IO.Path.Combine(AppContext.BaseDirectory,"Microsoft.Management.Infrastructure.CimCmdlets.dll").Replace("'","''");
  Invoke("Import-Module '"+assembly+"' -ErrorAction Stop");
  var command=Invoke("Get-Command Get-CimInstance").Single();Require(command.Properties["Source"].Value.ToString().Contains("CimCmdlets"),"Original cmdlet assembly imported");
  var os=Invoke("$script:OwnedOs = Get-CimInstance -Namespace root/cimv2 -ClassName Win32_OperatingSystem -ErrorAction Stop; $script:OwnedOs").Single();
  Require(os.BaseObject is CimInstance,"Actual native CIM instance reaches the real PowerShell pipeline");
  string build=os.Properties["BuildNumber"].Value.ToString();Require(build==Environment.OSVersion.Version.Build.ToString(),"CIM native OS build");
  Require(Invoke("Get-CimClass -Namespace root/cimv2 -ClassName Win32_Process -ErrorAction Stop").Single().BaseObject is CimClass,"Actual PowerShell CIM class metadata");
  string processId=Environment.ProcessId.ToString();
  var process=Invoke("Get-CimInstance -Namespace root/cimv2 -ClassName Win32_Process -Filter 'ProcessId = "+processId+"' -ErrorAction Stop").Single();
  Require(Convert.ToUInt32(process.Properties["ProcessId"].Value)==Environment.ProcessId,"Current-process CIM query through cmdlet");
  var method=Invoke("$p=Get-CimInstance -ClassName Win32_Process -Filter 'ProcessId = "+processId+"'; Invoke-CimMethod -InputObject $p -MethodName GetOwner -ErrorAction Stop").Single();
  Require(Convert.ToUInt32(method.Properties["ReturnValue"].Value)==0&&!string.IsNullOrEmpty(method.Properties["User"].Value.ToString()),"Read-only native CIM method through real binder/cmdlet");
  Require(Invoke("try { Get-CimInstance -ClassName Missing_Source_Port_Class -ErrorAction Stop; 'missed' } catch { $_.Exception.GetType().FullName }",true).Single().BaseObject.ToString().Contains("CimException"),"Native invalid-class error reaches PowerShell catch");
  var serialized=Invoke("$xml=[System.Management.Automation.PSSerializer]::Serialize($script:OwnedOs); $copy=[System.Management.Automation.PSSerializer]::Deserialize($xml); $copy.BuildNumber");
  Require(serialized.Single().BaseObject.ToString()==build,"Real PowerShell CIM serialization/deserialization");
  string wmi=Invoke("$s=[System.Management.ManagementObjectSearcher]::new('SELECT BuildNumber FROM Win32_OperatingSystem'); try { foreach($o in $s.Get()) { $o.BuildNumber } } finally { $s.Dispose() }").Single().BaseObject.ToString();
  Require(wmi==build,"Source WMI and CIM agree inside the actual PowerShell runspace");
  GC.Collect();GC.WaitForPendingFinalizers();GC.Collect();
  Require(Invoke("$script:OwnedOs.BuildNumber").Single().BaseObject.ToString()==build,"CIM object survives compacting GC through PowerShell ownership");
  foreach(System.Diagnostics.ProcessModule module in System.Diagnostics.Process.GetCurrentProcess().Modules)
   Require(!module.FileName.Replace('\\','/').Contains("/Microsoft.NET/Framework",StringComparison.OrdinalIgnoreCase),"Framework implementation loaded");
  Console.WriteLine("PASS actual source PowerShell7.4.20/CimCmdlets with experimental MMI3: module import, local OS/process queries, class metadata, GetOwner, native errors, CIM serialization, shared WMI/CIM OS build "+build+", GC and no Framework implementation");
 }
}
