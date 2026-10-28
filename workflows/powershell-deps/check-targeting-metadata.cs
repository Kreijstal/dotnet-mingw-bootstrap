using System;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Xml.Linq;

string root=Path.GetFullPath(args[0]);
string data=Path.Combine(root,"packs/Microsoft.NETCore.App.Ref/8.0.31/data");
var rows=File.ReadAllLines(Path.Combine(data,"PlatformManifest.txt")).Select(l=>l.Split('|')).ToDictionary(r=>r[0],r=>r[3],StringComparer.OrdinalIgnoreCase);
int count=0;
foreach(var item in XDocument.Load(Path.Combine(data,"FrameworkList.xml")).Root.Elements("File").Where(e=>(string)e.Attribute("Type")=="Managed"))
{
    string name=(string)item.Attribute("AssemblyName");
    string file=Path.Combine(root,"shared/Microsoft.NETCore.App/8.0.0",name+".dll");
    string actual=FileVersionInfo.GetVersionInfo(file).FileVersion;
    if(actual!=(string)item.Attribute("FileVersion")||actual!=rows[name+".dll"])
        throw new Exception("Targeting/runtime metadata mismatch: "+name+" actual="+actual);
    count++;
}
if(count!=rows.Count)throw new Exception("Targeting metadata entry count mismatch");
Console.WriteLine("PASS: FrameworkList and PlatformManifest match actual runtime PE file versions for all "+count+" managed assemblies");
