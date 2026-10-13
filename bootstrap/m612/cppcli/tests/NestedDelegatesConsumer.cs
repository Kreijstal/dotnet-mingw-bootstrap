using System;
using System.Reflection;
using System.Runtime.CompilerServices;
foreach(var type in typeof(NestedDelegates).Assembly.GetTypes())
    foreach(var method in type.GetMethods(BindingFlags.Public|BindingFlags.NonPublic|BindingFlags.Static|BindingFlags.Instance|BindingFlags.DeclaredOnly))
        if(!method.IsAbstract && method.GetMethodBody()!=null) RuntimeHelpers.PrepareMethod(method.MethodHandle);
var outer=typeof(NestedDelegates.Transform);
var inner=typeof(NestedDelegates.Inner.Transform);
if(outer.DeclaringType!=typeof(NestedDelegates) || inner.DeclaringType!=typeof(NestedDelegates.Inner) ||
    outer==inner || !outer.IsNestedPublic || outer.BaseType!=typeof(MulticastDelegate))
    throw new Exception("nested delegate identity/owner/visibility/base");
if(!typeof(NestedDelegates).GetNestedType("Hidden",BindingFlags.NonPublic).IsNestedPrivate ||
    !typeof(NestedDelegates).GetNestedType("Protected",BindingFlags.NonPublic).IsNestedFamily)
    throw new Exception("private/protected nested delegate metadata");
var callback=NestedDelegates.Static();
var instance=new NestedDelegates(7).Instance();
var nested=NestedDelegates.Inner.Create();
var eventSource=new NestedDelegates(3);
int events=0;
EventHandler handler=(sender,args)=> { if(!ReferenceEquals(sender,eventSource) || args!=EventArgs.Empty) throw new Exception("imported automatic event reference signature"); ++events; };
eventSource.Changed+=handler; eventSource.Fire();
eventSource.Changed-=handler; eventSource.Fire();
if(events!=1) throw new Exception("imported automatic event add/remove/raise");
int value=1;
NestedDelegates.Change()(ref value);
if(callback(21)!=42 || instance(35)!=42 || nested(35)!=42 || NestedDelegates.Call(callback,21)!=42 ||
    NestedDelegates.Private(21)!=42 || value!=42 || callback.GetType()!=outer || nested.GetType()!=inner)
    throw new Exception("nested delegate source/C# call, method binding or byref dispatch");
GC.Collect(2,GCCollectionMode.Forced,true,true);
if(instance(35)!=42 || nested(35)!=42) throw new Exception("nested delegate target retention across moving GC");
Console.WriteLine("nested CLR delegates: actual owner/identity/public/private/protected metadata, static/instance/deep/byref calls and GC PASS");
