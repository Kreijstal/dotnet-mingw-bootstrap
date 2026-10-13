using System;
using System.Linq;
using System.Reflection;
using System.Runtime.CompilerServices;
foreach(var type in typeof(InteriorIndexing).Assembly.GetTypes())
    foreach(var method in type.GetMethods(BindingFlags.Public|BindingFlags.NonPublic|BindingFlags.Static|BindingFlags.Instance|BindingFlags.DeclaredOnly).Cast<MethodBase>()
        .Concat(type.GetConstructors(BindingFlags.Public|BindingFlags.NonPublic|BindingFlags.Instance|BindingFlags.DeclaredOnly)))
        if(!method.IsAbstract && method.GetMethodBody()!=null) RuntimeHelpers.PrepareMethod(method.MethodHandle);
var values=new[] { 3,5,7 };
if(InteriorIndexing.Read(ref values[1],-1)!=3 || InteriorIndexing.Read(ref values[0],2)!=7) throw new Exception("signed index or scalar stride");
InteriorIndexing.Write(ref values[0],1,9);
if(values[1]!=9 || InteriorIndexing.Indices!=1) throw new Exception("store/evaluation count");
InteriorIndexing.Indices=0;
if(InteriorIndexing.Update(ref values[0],1)!=1243 || values[1]!=13 || InteriorIndexing.Indices!=3) throw new Exception("compound/prefix/postfix capture");
var wide=new[] { long.MinValue,5000000002L };
var floating=new[] { 1.25,2.5 };
if(InteriorIndexing.Wide(ref wide[0],1)!=5000000002L || InteriorIndexing.Floating(ref floating[0],1)!=2.5) throw new Exception("wide/floating stride");
if(InteriorIndexing.Alias(values)!=31 || values[1]!=31 || InteriorIndexing.Pinned(values)!=33 || values[1]!=33) throw new Exception("tracked indexed address or pin operation");
var bytes=new byte[] { 255,2,3 };
var copy=new InteriorSnapshot(ref bytes[0],bytes.Length);
if(!copy.Bytes.SequenceEqual(bytes)) throw new Exception("original GDI byte snapshot pattern");
if(InteriorIndexing.ByteWrap(ref bytes[0])!=255000 || bytes[0]!=0 || copy.Bytes[0]!=255) throw new Exception("byte narrowing or snapshot independence");
if(!typeof(InteriorIndexing).GetMethod("Read").GetParameters()[0].ParameterType.IsByRef) throw new Exception("managed byref metadata");
Console.WriteLine("interior indexing: signed/wide strides, loads/stores/updates, receiver/index once, tracked aliases, GDI snapshot, pins and moving GC PASS");
