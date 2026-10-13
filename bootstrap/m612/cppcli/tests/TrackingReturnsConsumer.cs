using System;
using System.Reflection;
using System.Runtime.CompilerServices;
foreach(var type in typeof(ReturnOwner).Assembly.GetTypes())
    foreach(var method in type.GetMethods(BindingFlags.Public|BindingFlags.Static|BindingFlags.Instance|BindingFlags.DeclaredOnly))
        if(method.GetMethodBody()!=null) RuntimeHelpers.PrepareMethod(method.MethodHandle);
var owner=new ReturnOwner {Number=10,Data=new ReturnData {Counter=20,Text="initial"}};
ref int field=ref owner.Counter(); field+=2;
if(owner.Number!=12 || TrackingReturns.Read(owner)!=12 || TrackingReturns.Alias(owner)!=19 || owner.Number!=19 || owner.Calls!=3)
    throw new Exception("returned field address/source read/alias/evaluation");
TrackingReturns.Payload(owner);
ref var payload=ref owner.Payload();
GC.Collect(2,GCCollectionMode.Forced,true,true);
if(payload.Counter!=23 || payload.Text!="retained" || owner.Data.Counter!=23 || owner.Calls!=5)
    throw new Exception("returned value field contains managed references across GC");
ref int nested=ref owner.Nested(); nested+=4;
if(owner.Data.Counter!=27) throw new Exception("nested value-field byref return");
ref int protectedField=ref owner.Protected(); protectedField+=1;
if(owner.Number!=20 || owner.FinallyCalls!=1) throw new Exception("byref return protected-region cleanup");
ref int fresh=ref TrackingReturns.Fresh();
GC.Collect(2,GCCollectionMode.Forced,true,true); fresh+=11;
if(fresh!=42) throw new Exception("returned byref retained detached heap owner");
var array=new[]{1,2}; ref int element=ref TrackingReturns.Array(array,1); element=43;
ref int shared=ref ReturnOwner.Static(); shared=44;
if(array[1]!=43 || ReturnOwner.Shared!=44 || typeof(ReturnOwner).GetMethod("Payload").ReturnType!=typeof(ReturnData).MakeByRefType())
    throw new Exception("managed array/static/CLR byref return metadata");
ref string text=ref owner.Text(); text="changed";
if(owner.Data.Text!="changed") throw new Exception("out-of-line managed handle slot return");
TrackingReturns.NestedWrite(owner,45);
if(TrackingReturns.NestedRead(owner)!=45 || owner.Data.Counter!=45)
    throw new Exception("source field read/write through value byref call result");
var values=new[]{new ReturnData {Counter=46,Text="array"}};
ref var value=ref TrackingReturns.Values(values,0);
for(int i=0;i<20;i++) {
    GC.Collect(2,GCCollectionMode.Forced,true,true);
    fresh++; element++; value.Counter++; text="gc"+i;
}
if(fresh!=62 || array[1]!=63 || values[0].Counter!=66 || owner.Data.Text!="gc19")
    throw new Exception("moving GC retained scalar/array/value/handle byrefs");
try { TrackingReturns.Array(null,0)=1; throw new Exception("null array returned an address"); }
catch(NullReferenceException) {}
try { TrackingReturns.Array(array,2)=1; throw new Exception("out-of-bounds array returned an address"); }
catch(IndexOutOfRangeException) {}
Console.WriteLine("tracking returns: actual heap field/value/nested/array/static aliases, detached owner retention, source loads, finally cleanup and moving GC PASS");
