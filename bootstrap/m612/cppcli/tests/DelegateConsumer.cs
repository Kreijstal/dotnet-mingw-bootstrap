using System;
using System.Reflection;
using System.Runtime.InteropServices;
var value = DelegateBridge.Static();
if (value(21) != 42 || DelegateBridge.Call(value, 10) != 20 || DelegateBridge.Invoke(value, 5) != 10 ||
    DelegateBridge.Global()(40) != 42 || value.Target != null || !value.Method.IsStatic || value.Method.Name != "Twice")
    throw new Exception("static/global delegate identity or invocation failed");
var target = new CounterTarget(40);
value = DelegateBridge.Instance(target);
if (value(2) != 42 || !ReferenceEquals(value.Target, target) || value.Method.Name != "Add")
    throw new Exception("bound delegate identity failed");
var weak = new WeakReference(target);
target = null;
GC.Collect(2, GCCollectionMode.Forced, true, true);
if (!weak.IsAlive || value(2) != 42) throw new Exception("delegate target lost during GC");
if (DelegateBridge.Virtual(new DerivedTarget())(32) != 42) throw new Exception("virtual delegate slot binding failed");
var multiple = DelegateBridge.Static() + DelegateBridge.Global();
if (multiple(40) != 42 || multiple.GetInvocationList().Length != 2 || (multiple - DelegateBridge.Global())(21) != 42)
    throw new Exception("CLR multicast/remove semantics failed");
int number = 0;
DelegateBridge.Updater()(ref number);
DelegateBridge.Change(DelegateBridge.Updater(), ref number);
if (number != 42 || DelegateBridge.Text()("α 東京 😀") != "α 東京 😀") throw new Exception("byref/string delegate signatures failed");
DelegateBridge.Notification()(0);
unsafe {
    void* pointer = (void*)0x1234;
    if (DelegateBridge.Pointers()(pointer) != pointer) throw new Exception("native pointer delegate signature failed");
}
static void Expect<T>(Action action) where T : Exception {
    try { action(); } catch (T) { return; }
    throw new Exception("missing " + typeof(T).Name);
}
Expect<NullReferenceException>(() => DelegateBridge.Call(null, 1));
Expect<InvalidOperationException>(() => DelegateBridge.Throws()(0));
if (typeof(Scalar).BaseType != typeof(MulticastDelegate) || !typeof(Scalar).IsSealed ||
    typeof(Scalar).GetConstructors().Length != 1 || typeof(Scalar).GetConstructor(new[] {typeof(object), typeof(IntPtr)}) == null ||
    typeof(Scalar).Assembly.GetType("Hidden", true).IsPublic ||
    (typeof(Scalar).GetMethod("Invoke").GetMethodImplementationFlags() & MethodImplAttributes.CodeTypeMask) != MethodImplAttributes.Runtime)
    throw new Exception("delegate type/runtime method metadata failed");
value = DelegateBridge.Static();
IntPtr callback = Marshal.GetFunctionPointerForDelegate(value);
GC.Collect(2, GCCollectionMode.Forced, true, true);
if (Native.Call(callback, 21) != 42) throw new Exception("native-to-managed delegate thunk failed");
GC.KeepAlive(value);
Console.WriteLine("CLR delegate construction/invocation/GC/native callback OK");
static class Native {
    [DllImport("delegate-native.dll", EntryPoint="call_delegate", CallingConvention=CallingConvention.Cdecl)]
    public static extern int Call(IntPtr callback, int value);
}
