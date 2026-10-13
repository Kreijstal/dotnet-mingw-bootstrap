using System;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using Example;
Native.Reset();
var disposed=new FinalDerived();
if(Native.Live()!=2) throw new Exception("native resources not acquired");
((IDisposable)disposed).Dispose();
((IDisposable)disposed).Dispose();
if(Native.Live()!=0||Native.Closed()!=2||Native.Trace()!=21) throw new Exception("deterministic finalizer/base cleanup order or idempotence failed");
Native.Reset();
var explicitCall=new FinalDerived();
explicitCall.CloseDerived();
if(Native.Live()!=1||Native.Trace()!=2) throw new Exception("explicit finalizer incorrectly chained base cleanup");
FinalCalls.Drop(explicitCall);
if(Native.Live()!=0||Native.Trace()!=21) throw new Exception("managed delete did not finish base cleanup");
Native.Reset();
var qualifiedBase=new FinalDerived();
qualifiedBase.CloseBaseExplicit();
if(Native.Live()!=1||Native.Trace()!=1) throw new Exception("qualified protected base finalizer call failed");
((IDisposable)qualifiedBase).Dispose();
if(Native.Live()!=0||Native.Trace()!=12) throw new Exception("disposal after explicit base finalizer call failed");
Native.Reset();
var collected=Make(false);
Collect(collected);
if(Native.Live()!=0||Native.Closed()!=2||Native.Trace()!=21) throw new Exception("GC finalization/base order failed");
Native.Reset();
var suppressed=Make(true);
Collect(suppressed);
if(Native.Live()!=0||Native.Closed()!=2||Native.Trace()!=21) throw new Exception("disposal did not suppress repeated finalization");
Native.Reset();
var finalOnly=MakeOnly();
Collect(finalOnly);
if(Native.Live()!=0||Native.Closed()!=1||Native.Trace()!=3) throw new Exception("finalizer-only native cleanup failed");
Native.Reset();
var throwing=new FinalThrows();
try {
    typeof(FinalThrows).GetMethod("Dispose",BindingFlags.Instance|BindingFlags.NonPublic,null,new[]{typeof(bool)},null).Invoke(throwing,new object[]{false});
    throw new Exception("missing finalizer exception");
} catch(TargetInvocationException error) when(error.InnerException is InvalidOperationException) { }
finally { GC.SuppressFinalize(throwing); }
if(Native.Live()!=0||Native.Closed()!=1||Native.Trace()!=1) throw new Exception("base native cleanup skipped after finalizer exception");
foreach(var type in new[]{typeof(FinalBase),typeof(FinalDerived),typeof(FinalOnly)}) {
    var method=type.GetMethod("Finalize",BindingFlags.Instance|BindingFlags.NonPublic|BindingFlags.DeclaredOnly);
    if(method==null||!method.IsFamily||!method.IsVirtual||method.GetBaseDefinition().DeclaringType!=typeof(object))
        throw new Exception("CLR Finalize override identity lost");
}
GC.KeepAlive(disposed); GC.KeepAlive(explicitCall); GC.KeepAlive(throwing); GC.KeepAlive(qualifiedBase);
Console.WriteLine("source finalizers: native release, inline/out-of-line/(void), deterministic disposal/delete, explicit own-only calls, GC/base order, suppression, idempotence and CLR override identity PASS");
[MethodImpl(MethodImplOptions.NoInlining)]
static WeakReference Make(bool dispose) {
    var value=new FinalDerived();
    if(dispose) ((IDisposable)value).Dispose();
    return new WeakReference(value);
}
[MethodImpl(MethodImplOptions.NoInlining)]
static WeakReference MakeOnly() => new WeakReference(new FinalOnly());
static void Collect(WeakReference value) {
    for(int i=0;i<10&&value.IsAlive;++i) { GC.Collect(2,GCCollectionMode.Forced,true,true); GC.WaitForPendingFinalizers(); }
    if(value.IsAlive) throw new Exception("test object remains alive");
}
static class Native {
    [DllImport("finalizers-native.dll",EntryPoint="finalizer_live",CallingConvention=CallingConvention.Cdecl)] public static extern int Live();
    [DllImport("finalizers-native.dll",EntryPoint="finalizer_closed",CallingConvention=CallingConvention.Cdecl)] public static extern int Closed();
    [DllImport("finalizers-native.dll",EntryPoint="finalizer_trace",CallingConvention=CallingConvention.Cdecl)] public static extern int Trace();
    [DllImport("finalizers-native.dll",EntryPoint="finalizer_reset",CallingConvention=CallingConvention.Cdecl)] public static extern void Reset();
}
