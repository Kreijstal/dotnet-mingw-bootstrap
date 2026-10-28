using System;
using System.Linq;
using System.Management;
using System.Runtime.InteropServices;
using Microsoft.Management.Infrastructure;

static class CombinedSourceBackends
{
    static void Require(bool value,string message){if(!value)throw new Exception(message);}
    static string Cim(CimSession session)
    {
        string build=null;int count=0;
        foreach(var row in session.QueryInstances("root/cimv2","WQL","SELECT BuildNumber FROM Win32_OperatingSystem"))
        {using(row){build=(string)row.CimInstanceProperties["BuildNumber"].Value;count++;}}
        Require(count==1&&!string.IsNullOrEmpty(build),"CIM OS result");return build;
    }
    static string Wmi()
    {
        using var searcher=new ManagementObjectSearcher("root\\cimv2","SELECT BuildNumber FROM Win32_OperatingSystem");
        using var rows=searcher.Get();string build=null;int count=0;
        foreach(ManagementObject row in rows){using(row){build=(string)row["BuildNumber"];count++;}}
        Require(count==1&&!string.IsNullOrEmpty(build),"WMI OS result");return build;
    }
    static void Main()
    {
        Console.WriteLine("Runtime: "+RuntimeInformation.FrameworkDescription);
        Console.WriteLine("MMI: "+typeof(CimSession).Assembly.FullName);
        Console.WriteLine("WMI: "+typeof(ManagementObjectSearcher).Assembly.FullName);
        using var session=CimSession.Create(null);
        string first=Cim(session);Require(Wmi()==first,"CIM then WMI same OS result");
        GC.Collect(GC.MaxGeneration,GCCollectionMode.Forced,true,true);GC.WaitForPendingFinalizers();
        string second=Wmi();Require(Cim(session)==second&&first==second,"WMI then CIM after compacting GC");
        Require(!NativeDependencies.Report(),"no Framework implementation modules");
        var modules=System.Diagnostics.Process.GetCurrentProcess().Modules.Cast<System.Diagnostics.ProcessModule>().ToArray();
        Require(modules.Any(m=>m.ModuleName.Equals("mi.dll",StringComparison.OrdinalIgnoreCase)),"Windows MI module");
        Require(modules.Any(m=>m.ModuleName.Equals("msys-wmi-native.dll",StringComparison.OrdinalIgnoreCase)),"source-built WMI module");
        Console.WriteLine("PASS installed source-built CIM/MMI and WMI share one process, agree on OS build "+first+", survive GC and load no Framework implementations");
        Console.WriteLine("MMI required 3.0 compatibility remains unverified; this integration uses the experimental 2.0 port.");
    }
}
