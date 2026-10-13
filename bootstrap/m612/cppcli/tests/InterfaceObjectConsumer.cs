using System;
using System.IO;
using System.Reflection;
using System.Runtime.CompilerServices;
foreach(var type in typeof(InterfaceObjects).Assembly.GetTypes())
    foreach(var method in type.GetMethods(BindingFlags.Public|BindingFlags.NonPublic|BindingFlags.Static|BindingFlags.Instance|BindingFlags.DeclaredOnly))
        if(!method.IsAbstract && method.GetMethodBody()!=null) RuntimeHelpers.PrepareMethod(method.MethodHandle);
IObjectItem value=new ObjectItem(42);
var holder=new ObjectHolder();
foreach(var result in new[]{InterfaceObjects.Identity(value),InterfaceObjects.Argument(value),InterfaceObjects.Return(value),
    InterfaceObjects.Field(holder,value),InterfaceObjects.Property(holder,value)})
    if(!ReferenceEquals(value,result) || ((IObjectItem)result).Read()!=42) throw new Exception("source interface Object identity/dispatch/storage");
if(InterfaceObjects.Reads!=4 || InterfaceObjects.Typed(value)!=2 || InterfaceObjects.Return(null)!=null ||
    InterfaceObjects.Argument(null)!=null) throw new Exception("operand evaluation/typed overload/null interface");
using var stream=new MemoryStream();
if(!ReferenceEquals(stream,InterfaceObjects.Imported(stream)) || !ReferenceEquals(stream,InterfaceObjects.ImportedReturn(stream)))
    throw new Exception("imported interface Object reference conversion");
IDisposable boxed=new DisposableValue();
if(!ReferenceEquals(boxed,InterfaceObjects.Imported(boxed))) throw new Exception("interface conversion reboxed a value object");
GC.Collect(2,GCCollectionMode.Forced,true,true);
if(!ReferenceEquals(value,holder.Current) || ((IObjectItem)holder.Field).Read()!=42) throw new Exception("Object storage after moving GC");
Console.WriteLine("source/imported interface Object conversions: identity, boxed-object preservation, returns/arguments/local/field/property storage, dispatch, typed overloads, nulls and GC PASS");
struct DisposableValue : IDisposable { public void Dispose() {} }
