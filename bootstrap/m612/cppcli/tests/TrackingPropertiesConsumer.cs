using System;
var owner=new ProbeProperty();
var text=new string("λ🚀".ToCharArray());
ProbeProperty.Write(ref owner,text);
if(owner.Sets!=1 || !ReferenceEquals(owner.Data,text) || !ReferenceEquals(ProbeProperty.Read(ref owner),text) || owner.Gets!=1)
    throw new Exception("property getter/setter on ref-class tracking receiver identity and evaluation");
for(int i=0;i<20;i++) {
    GC.Collect(2,GCCollectionMode.Forced,true,true);
    if(!ReferenceEquals(ProbeProperty.Read(ref owner),text)) throw new Exception("receiver/property result survives moving GC");
}
var original=owner;
ProbeProperty.ReplaceDuringWrite(ref owner,ref owner);
if(ReferenceEquals(owner,original) || original.Data is not string value || value!="replacement" || original.Sets!=2 || owner.Data!=null)
    throw new Exception("receiver is captured before RHS replaces aliased handle storage");
ProbeProperty.Write(ref owner,null);
if(ProbeProperty.Read(ref owner)!=null || typeof(ProbeProperty).GetMethod("Read").GetParameters()[0].ParameterType!=typeof(ProbeProperty).MakeByRefType())
    throw new Exception("null value and actual tracking receiver metadata");
ProbeProperty.Write(ref owner,text); ProbeProperty.Clear(ref owner);
if(owner.Data!=null || owner.Sets!=3) throw new Exception("source nullptr property capture/setter");
unsafe {
    var native=new NativeNullProperty {Data=(void*)0x1234};
    NativeNullProperty.Clear(native);
    if(native.Data!=null || native.Sets!=1)
        throw new Exception("native null assignment preserves setter and stores native zero");
}
Console.WriteLine("tracking properties: class receiver capture, getter/setter identity and evaluation, RHS handle replacement, nulls, CLR byref metadata and compacting GC PASS");
