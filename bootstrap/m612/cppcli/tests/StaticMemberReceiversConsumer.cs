using System;
var owner=new StaticReceiver();
if(StaticReceiver.Field(owner)!=7 || StaticReceiver.Property(owner)!=8 || StaticReceiver.Calls!=2)
    throw new Exception("static field/property receiver evaluation");
if(StaticReceiver.Field(null)!=7 || StaticReceiver.Property(null)!=8 || StaticReceiver.Calls!=4)
    throw new Exception("static member access must evaluate receiver without instance dereference");
if(StaticReceiver.NativeSize()!=IntPtr.Size || StaticReceiver.Size((IntPtr)123)!=IntPtr.Size || StaticReceiver.Calls!=5)
    throw new Exception("IntPtr static property accessed through value/call receiver");
Console.WriteLine("static member receivers: source fields/properties, receiver evaluation once, null reference receivers, actual IntPtr.Size and GC PASS");
