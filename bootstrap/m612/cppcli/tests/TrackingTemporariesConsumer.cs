using System;
using System.Reflection;
using System.Runtime.CompilerServices;
foreach(var type in typeof(TrackingTemporaries).Assembly.GetTypes())
    foreach(var method in type.GetMethods(BindingFlags.Public|BindingFlags.NonPublic|BindingFlags.Static|BindingFlags.Instance|BindingFlags.DeclaredOnly))
        if(!method.IsAbstract && method.GetMethodBody()!=null) RuntimeHelpers.PrepareMethod(method.MethodHandle);
if(TrackingTemporaries.Direct(35)!=42 || TrackingTemporaries.Saved.Value!=42 ||
    TrackingTemporaries.Delegate(36)!=43 || TrackingTemporaries.Saved.Value!=43 ||
    TrackingTemporaries.Two(20,24)!=44 || TemporaryProxy.Constructions!=4 ||
    TrackingTemporaries.Operands!=4 || TrackingTemporaries.Callbacks!=2)
    throw new Exception("temporary construction/evaluation/byref mutation/delegate");
var parameter=typeof(TrackingTemporaries).GetMethod("Receive").GetParameters()[0];
if(parameter.ParameterType!=typeof(TemporaryProxy).MakeByRefType()) throw new Exception("CLR tracking reference signature");
try { TrackingTemporaries.Direct(-1); throw new Exception("constructor exception swallowed"); } catch(ArgumentException) {}
if(TemporaryProxy.Constructions!=5 || TrackingTemporaries.Callbacks!=2) throw new Exception("failed construction invoked callback");
GC.Collect(2,GCCollectionMode.Forced,true,true);
if(TrackingTemporaries.Saved.Value!=43) throw new Exception("escaped managed temporary identity/lifetime");
Console.WriteLine("source ref-class tracking temporaries: constructors, direct/delegate/byref calls, argument evaluation once, mutation, exceptions, retained identity and moving GC PASS");
