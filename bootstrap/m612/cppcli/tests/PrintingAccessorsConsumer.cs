using System;
using System.Linq;
using System.Reflection;
using System.Runtime.CompilerServices;
foreach(var type in typeof(InlineLiteral).Assembly.GetTypes())
    foreach(var method in type.GetMethods(BindingFlags.Public|BindingFlags.NonPublic|BindingFlags.Static|BindingFlags.Instance|BindingFlags.DeclaredOnly).Cast<MethodBase>()
        .Concat(type.GetConstructors(BindingFlags.Public|BindingFlags.NonPublic|BindingFlags.Static|BindingFlags.Instance|BindingFlags.DeclaredOnly)))
        if(!method.IsAbstract && method.GetMethodBody()!=null) RuntimeHelpers.PrepareMethod(method.MethodHandle);
var inline=new InlineLiteral();
if(inline.Name!="RAW" || inline.Label!="東京 😀" || (int)inline.Number!=42) throw new Exception("base/field initialization or overload");
if(new ExternalLiteral().Name!="out of line" || new AliasLiteral().Name!="alias") throw new Exception("out-of-line or alias base lookup");
if(new NativeLiteral().Length!=6) throw new Exception("native constructor literal changed");
if(new ResourceLiteral().BaseName!="SourceBootstrapResource") throw new Exception("actual ResourceManager base constructor");
if((int)ConstructorCalls.Box(46)!=46 || ConstructorCalls.Typed(47)!=47 ||
    ConstructorCalls.Range(48) is not ArgumentOutOfRangeException range || (int)range.ActualValue!=48 || range.ParamName!="value")
    throw new Exception("source/imported constructor boxing or typed overload");
var table=new QualifiedTable(); table.WriteBase("key",43);
if((int)table.ReadBase("key")!=43 || (int)table["key"]!=43) throw new Exception("actual Hashtable qualified indexer");
var custom=new QualifiedCustom(); custom.WriteBase(1,44);
if(custom.ReadBase(1)!=44 || custom[1]!=44 || custom.TitleBase()!="original title") throw new Exception("metadata-selected default/named accessor");
var dispatch=new OverrideTable(); dispatch.WriteBase("key",45);
if((int)dispatch.ReadBase("key")!=45 || (string)dispatch["key"]!="override") throw new Exception("qualified base call dispatched virtually");
GC.Collect(); GC.WaitForPendingFinalizers();
if(inline.Name!="RAW" || (int)table.ReadBase("key")!=43) throw new Exception("GC storage");
Console.WriteLine("printing accessors: source/imported base literal constructors, fields/boxing, native literals, actual ResourceManager/Hashtable, custom default/named metadata and base dispatch PASS");
public class OverrideTable : QualifiedTable {
    public override object this[object key] { get=>"override"; set=>base[key]=value; }
}
