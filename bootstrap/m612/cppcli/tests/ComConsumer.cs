using System;
using System.Reflection;
using System.Runtime.InteropServices;
if (!typeof(IComProbe).IsImport || typeof(IComProbe).GUID != new Guid("138EF579-12A0-4B01-8913-675542958CBD") ||
    typeof(IComProbe).GetCustomAttribute<InterfaceTypeAttribute>().Value != ComInterfaceType.InterfaceIsIUnknown ||
    typeof(ITranslatedProbe).GUID != new Guid("271D54C4-1BE3-4F9F-ACB7-8242A2BB9A34"))
    throw new Exception("COM type metadata lost");
var read = typeof(IComProbe).GetMethod("Read");
var parameters = read.GetParameters();
if ((read.GetMethodImplementationFlags() & MethodImplAttributes.PreserveSig) == 0 ||
    !parameters[0].IsIn || parameters[0].GetCustomAttribute<MarshalAsAttribute>().Value != UnmanagedType.U8 ||
    !parameters[1].IsOut || parameters[1].GetCustomAttribute<MarshalAsAttribute>().Value != UnmanagedType.Bool ||
    parameters[1].ParameterType != typeof(bool).MakeByRefType())
    throw new Exception("COM method/parameter metadata lost");
ParameterFlags.Set(out int value);
if (value != 42 || ParameterFlags.Indexed() != 42) throw new Exception("C# out parameter consumption or ordinary array indexing failed");
IntPtr pointer = Native.Create();
object wrapper = null;
try {
    wrapper = Marshal.GetObjectForIUnknown(pointer);
    Marshal.Release(pointer); pointer = IntPtr.Zero;
    var probe = (IComProbe)wrapper;
    if (probe.Read(0xf123456789abcdefUL, out bool result) != 1 || !result ||
        probe.Read(0, out result) != 1 || result || probe.Fail() != unchecked((int)0x80070005))
        throw new Exception("native COM signature/BOOL/uint64/HRESULT mismatch");
    var translated = (ITranslatedProbe)wrapper;
    unsafe {
        IntPtr address = IntPtr.Zero;
        if (probe.WriteAddress(&address) != 0 || address == IntPtr.Zero) throw new Exception("native IntPtr pointer write failed");
        IntPtr expected = Marshal.GetIUnknownForObject(wrapper);
        try { if (address != expected) throw new Exception("native IntPtr pointer address mismatch"); }
        finally { Marshal.Release(expected); }
    }
    if (translated.GetValue() != 42) throw new Exception("COM retval translation failed");
    try { translated.FailWithoutSig(); throw new Exception("missing HRESULT exception translation"); }
    catch (UnauthorizedAccessException e) when (e.HResult == unchecked((int)0x80070005)) { }
} finally {
    if (pointer != IntPtr.Zero) Marshal.Release(pointer);
    if (wrapper != null) Marshal.FinalReleaseComObject(wrapper);
}
if (Native.Live() != 0) throw new Exception("native COM reference leak");
if (typeof(ManagedCallback).GetCustomAttribute<ClassInterfaceAttribute>().Value != ClassInterfaceType.None ||
    !typeof(ManagedCallback).GetCustomAttribute<ComVisibleAttribute>().Value ||
    !typeof(ManagedCallback).GetMethod("Value").GetCustomAttribute<ComVisibleAttribute>().Value)
    throw new Exception("COM export class metadata lost");
var managed = new ManagedCallback();
IntPtr callback = ComTypeTokens.Interface(managed);
try { if (Native.Managed(callback, 32) != 42) throw new Exception("native-to-managed COM interface dispatch failed"); }
finally { Marshal.Release(callback); }
GC.KeepAlive(managed);
Console.WriteLine("real MinGW COM metadata/marshalling/HRESULT checks OK");
static class Native {
    [DllImport("com-probe-native.dll", EntryPoint="create_probe", CallingConvention=CallingConvention.Cdecl)]
    public static extern IntPtr Create();
    [DllImport("com-probe-native.dll", EntryPoint="live_probes", CallingConvention=CallingConvention.Cdecl)]
    public static extern int Live();
    [DllImport("com-probe-native.dll", EntryPoint="call_managed_callback", CallingConvention=CallingConvention.Cdecl)]
    public static extern int Managed(IntPtr callback, int value);
}
