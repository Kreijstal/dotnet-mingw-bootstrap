using System;
using System.Reflection;
using System.Runtime.InteropServices;

if (ValuePointers.Stack() != 42) throw new Exception("stack value pointer/native mutation failed");
if (ValuePointers.BoxRead(ValuePointers.Box(17)) != 17) throw new Exception("boxed handles were confused with native pointers");
if (ValuePointers.PredefinedAddress() != 0xffffffff80000002UL) throw new Exception("predefined Win32 handle sign extension was lost");
var parameter = typeof(ValuePointers).GetMethod("Update").GetParameters()[0].ParameterType;
if (!parameter.IsPointer || parameter.GetElementType() != typeof(PointerPoint) || Marshal.SizeOf<PointerPoint>() != 16)
    throw new Exception("native value pointer signature/layout failed");
if (typeof(ValuePointers).GetMethod("Identity").ReturnType != parameter ||
    !typeof(PointerHolder).GetField("_pointer", BindingFlags.Instance | BindingFlags.NonPublic).FieldType.IsPointer)
    throw new Exception("native pointer return/field metadata failed");
unsafe {
    PointerPoint[] values = { new PointerPoint(5, 99) };
    fixed (PointerPoint* pointer = values) {
        if (ValuePointers.Update(pointer) != 42 || pointer->X != 12 || pointer->Y != 1234567890123L)
            throw new Exception("pinned C# value buffer lost its native ABI/storage");
        if (ValuePointers.Identity(pointer) != pointer) throw new Exception("native pointer identity was lost");
    }
}
Console.WriteLine("native value pointers, fields/returns, boxing distinction and native ABI OK");
