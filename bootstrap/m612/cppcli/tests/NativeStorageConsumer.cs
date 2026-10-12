using System;
using System.Linq;
using System.Runtime.InteropServices;

var guid = new Guid("b2d9f3ec-c9fe-4a11-a2ec-d86208f7c0a2");
if (NativeStorage.Check(guid) != 42 || NativeStorage.NativeReference() != 12 || NativeStorage.NativeAlias() != 10 || NativeStorage.Hidden() != 42)
    throw new Exception("native GUID layout or native reference mutation failed");
int[] elements = { 37 };
NativeStorage.IncrementPinned(elements);
if (elements[0] != 42) throw new Exception("native reference to pinned array storage failed");
var toGuid = typeof(NativeStorage).GetMethod("ToGUID");
var parameter = toGuid.GetParameters()[0];
if (!parameter.ParameterType.IsPointer || parameter.ParameterType.IsByRef ||
    parameter.ParameterType.GetElementType() != typeof(Guid) ||
    !parameter.GetOptionalCustomModifiers().Any(t => t.Name == "IsImplicitlyDereferenced"))
    throw new Exception("native reference was not emitted as a modified unmanaged pointer");
var nativeGuid = toGuid.ReturnType;
if (!nativeGuid.IsValueType || !nativeGuid.IsLayoutSequential || Marshal.SizeOf(nativeGuid) != 16 ||
    nativeGuid.StructLayoutAttribute.Pack != 4 || !nativeGuid.GetCustomAttributes(false).Any(a => a.GetType().Name == "NativeCppClassAttribute"))
    throw new Exception("native POD storage lost its Clang layout or native metadata");
Console.WriteLine("native references and real Windows GUID storage OK");
