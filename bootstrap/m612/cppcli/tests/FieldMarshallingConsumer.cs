using System;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
foreach(var method in typeof(FieldDefaults).Assembly.GetTypes())
    foreach(var member in method.GetMethods(BindingFlags.Public|BindingFlags.Static|BindingFlags.Instance|BindingFlags.DeclaredOnly))
        if(member.GetMethodBody()!=null) RuntimeHelpers.PrepareMethod(member.MethodHandle);
if(Marshal.SizeOf<FieldDefaults>()!=24 || Marshal.OffsetOf<FieldDefaults>("DeviceMode").ToInt64()!=8 ||
    Marshal.OffsetOf<FieldDefaults>("Access").ToInt64()!=16 || Marshal.SizeOf<FieldDocInfo>()!=40)
    throw new Exception("native printer/document record layout");
foreach(var field in typeof(FieldDefaults).GetFields()) {
    var expected=field.Name=="DataType" ? UnmanagedType.LPWStr : field.Name=="DeviceMode" ? UnmanagedType.SysInt : UnmanagedType.U4;
    if(field.GetCustomAttribute<MarshalAsAttribute>()?.Value!=expected) throw new Exception("CLR field native marshal descriptor");
}
int collections=0;
using var stop=new CancellationTokenSource();
var collector=Task.Run(()=> {
    while(!stop.IsCancellationRequested) {
        bool inside=Native.Stage()==1;
        GC.Collect(2,GCCollectionMode.Forced,true,true);
        if(inside && Native.Stage()==1) Interlocked.Increment(ref collections);
    }
});
try {
    var defaults=new FieldDefaults(new string("東京 λ 😀".ToCharArray()),new IntPtr(0x123456789abcdef0L),FieldAccess.High);
    if(Native.Defaults(defaults)!=42 || (uint)defaults.Access!=0x80000042u || defaults.DataType!="東京 λ 😀")
        throw new Exception("native Unicode/pointer/enum marshalling and copy-back");
    if(Native.Doc(new FieldDocInfo("λ document"))!=40) throw new Exception("DOCINFOW strings/nulls/types marshalling");
} finally { stop.Cancel(); collector.GetAwaiter().GetResult(); }
if(collections==0) throw new Exception("no compacting GC during field marshalling");
Console.WriteLine($"field marshal metadata: actual PRINTER_DEFAULTSW/DOCINFOW layouts, Unicode/null strings, native integers, enum copy-back and {collections} compacting collections PASS");
static class Native {
    [DllImport("field-marshalling-native.dll",EntryPoint="FieldMarshalStage")] public static extern int Stage();
    [DllImport("field-marshalling-native.dll",EntryPoint="FieldMarshalDefaults")] public static extern int Defaults([In,Out] FieldDefaults value);
    [DllImport("field-marshalling-native.dll",EntryPoint="FieldMarshalDoc")] public static extern int Doc([In] FieldDocInfo value);
}
