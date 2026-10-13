using System;
var value=new PointerDerived();
GC.Collect(2,GCCollectionMode.Forced,true,true);
if(value.Pointer!=(IntPtr)0x1234 || value.Order!=1 || value.After!=1 || BaseValueState.Calls!=1)
    throw new Exception("value-return base initializer wrapper/evaluation/order/GC");
try { new PointerFailure(); throw new Exception("initializer exception lost"); }
catch(ArgumentException error) { if(error.Message!="base value") throw; }
if(BaseValueState.Calls!=2) throw new Exception("derived body ran after initializer threw");
Console.WriteLine("base value initializers: source IntPtr builder, wrapped constructor, evaluation once, base/member/body order, GC and exception propagation PASS");
