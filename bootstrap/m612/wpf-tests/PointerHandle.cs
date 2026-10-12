using System;
using System.Runtime.InteropServices;
IntPtr memory = Marshal.AllocHGlobal(4);
try {
    var handle = new WpfPointerProbe(memory);
    var baseType = handle.GetType().BaseType;
    if (handle.IsInvalid || handle.Address() != memory || baseType.IsGenericType ||
        baseType.GetProperty("Value").PropertyType != typeof(int).MakePointerType() ||
        baseType.GetProperty("IsInvalid").GetMethod.GetBaseDefinition().DeclaringType != typeof(CriticalHandle))
        throw new Exception("WPF pointer template ABI or virtual property binding lost");
    handle.Dispose(); handle.Dispose();
    if (!handle.IsInvalid || !handle.IsClosed || WpfPointerProbe.Releases() != 1)
        throw new Exception("WPF pointer handle cleanup or invalid state failed");
    var zero = new WpfPointerProbe(IntPtr.Zero); zero.Dispose();
    if (!zero.IsInvalid || WpfPointerProbe.Releases() != 1)
        throw new Exception("invalid pointer handle unexpectedly released");
} finally { Marshal.FreeHGlobal(memory); }
Console.WriteLine("upstream WPF pointer handle constructor/accessors and CLR cleanup OK");
