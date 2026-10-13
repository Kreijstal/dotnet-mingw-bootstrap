using System;
using System.Reflection;
using System.Runtime.InteropServices;
var change=typeof(IArrayService).GetMethod("Change").GetParameters()[0];
var sized=change.GetCustomAttribute<MarshalAsAttribute>();
var fixedArray=typeof(IArrayService).GetMethod("Fixed").GetParameters()[0].GetCustomAttribute<MarshalAsAttribute>();
var combined=typeof(IArrayService).GetMethod("Combined").GetParameters()[0].GetCustomAttribute<MarshalAsAttribute>();
var first=typeof(IArrayService).GetMethod("FirstIndex").GetParameters()[1].GetCustomAttribute<MarshalAsAttribute>();
if(!change.IsIn||!change.IsOut||sized.Value!=UnmanagedType.LPArray||sized.SizeParamIndex!=1||
    fixedArray.Value!=UnmanagedType.LPArray||fixedArray.ArraySubType!=UnmanagedType.I4||fixedArray.SizeConst!=3||
    combined.SizeParamIndex!=1||combined.SizeConst!=3||combined.ArraySubType!=UnmanagedType.I4||first.SizeParamIndex!=0)
    throw new Exception("source LPArray field-marshalling metadata lost");
var service=new ArrayService();
IntPtr pointer=Marshal.GetComInterfaceForObject(service,typeof(IArrayService));
try {
    for(int i=0;i<3;++i) {
        GC.Collect(2,GCCollectionMode.Forced,true,true);
        int result=Native.Call(pointer);
        if(result!=0) throw new Exception("actual native array marshalling failed: "+result);
    }
} finally { Marshal.Release(pointer); }
GC.KeepAlive(service);
Console.WriteLine("LPArray metadata, zero/one SizeParamIndex, SizeConst/combined counts, explicit element type, native-to-managed array length and in/out mutation with sentinel bounds/GC PASS");
static class Native {
    [DllImport("array-marshal-native.dll",EntryPoint="call_array_service",CallingConvention=CallingConvention.Cdecl)]
    public static extern int Call(IntPtr service);
}
