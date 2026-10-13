using System;
using System.Reflection;
using System.Runtime.CompilerServices;
foreach(var type in typeof(ConstructorTracking).Assembly.GetTypes())
    foreach(var method in type.GetMethods(BindingFlags.Public|BindingFlags.NonPublic|BindingFlags.Static|BindingFlags.Instance|BindingFlags.DeclaredOnly))
        if(!method.IsAbstract && method.GetMethodBody()!=null) RuntimeHelpers.PrepareMethod(method.MethodHandle);
int value=10; var data=new CtorData {Value=20}; object output=null; var payload=new CtorPayload(30);
var result=ConstructorTracking.Create(ref value,ref data,ref output,payload);
if(value!=17 || data.Value!=25 || (string)output!="constructor" || payload.Value!=36 || result.Snapshot!=10 || result.AfterBase!=13)
    throw new Exception("derived/base tracking constructors, initialization order and mutation");
int direct=40;
var baseObject=new TrackingCtorBase(ref direct);
if(direct!=43 || baseObject.Snapshot!=40) throw new Exception("C# constructor byref API");
var array=new[]{1,50};
if(ConstructorTracking.Array(array)!=50 || array[1]!=53 || ConstructorTracking.Indices!=1)
    throw new Exception("array element address/index once across constructor GC");
int number=60;
if(ConstructorTracking.Value(ref number).Snapshot!=60 || number!=69) throw new Exception("value construction tracking reference");
int failed=70;
try { new ThrowingTrackingCtor(ref failed); throw new Exception("missing constructor exception"); } catch(ArgumentException) {}
if(failed!=72) throw new Exception("constructor exception discarded byref mutation");
var signature=typeof(TrackingCtorDerived).GetConstructors()[0].GetParameters();
if(signature.Length!=4 || !Array.TrueForAll(signature,p=>p.ParameterType.IsByRef) ||
    signature[3].ParameterType!=typeof(CtorPayload).MakeByRefType()) throw new Exception("constructor CLR byref signatures");
GC.Collect(2,GCCollectionMode.Forced,true,true);
if(result.AfterBase!=13 || payload.Value!=36 || (string)output!="constructor") throw new Exception("post-constructor GC");
Console.WriteLine("tracking constructors: CLR byrefs, primitive/value/Object/ref-class mutation, base/member order, array address once, value construction, exceptions and moving GC PASS");
