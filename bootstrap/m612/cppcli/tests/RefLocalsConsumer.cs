using System;
using System.Reflection;
using System.Runtime.CompilerServices;
foreach(var type in typeof(RefLocalChecks).Assembly.GetTypes())
    foreach(var method in type.GetMethods(BindingFlags.Static|BindingFlags.Instance|BindingFlags.Public|BindingFlags.NonPublic|BindingFlags.DeclaredOnly))
        if(method.GetMethodBody()!=null) RuntimeHelpers.PrepareMethod(method.MethodHandle);
var table=RefLocalChecks.Store();
var data=table["entry"]; var typeData=data.GetType();
if(typeData.FullName!="RefLocalScope.Data" || typeData.IsValueType || typeData.IsPublic || Convert.ToInt32(typeData.GetField("mask").GetValue(data))!=2 ||
   (bool)typeData.GetField("single").GetValue(data)) throw new Exception("original-shaped private ref struct/local percent identity");
var text=new string("λ🚀".ToCharArray());
var escaped=RefLocalChecks.Escape(text); var bound=RefLocalChecks.Bind(text);
for(int i=0;i<20;i++) {
    GC.Collect(2,GCCollectionMode.Forced,true,true);
    if(escaped.Value!=9 || !ReferenceEquals(escaped.Text,text) || bound.Value!=13 || !ReferenceEquals(bound.Text,text) || table["entry"]!=data)
        throw new Exception("ref-class local and percent handle survive scope exit and moving GC");
}
if(RefLocalChecks.Default() is not RefLocalNode node || node.Value!=11 || node.Label!="default")
    throw new Exception("source ref local constructor and Object conversion");
Console.WriteLine("ref locals: private ref struct/Hashtable escape, constructor fields/properties, percent identity, exact tracking binding and compacting GC PASS");
