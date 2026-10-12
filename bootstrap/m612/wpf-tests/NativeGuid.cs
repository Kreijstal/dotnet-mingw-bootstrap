using System;
using System.Linq;
using System.Runtime.InteropServices;

var guid = new Guid("b2d9f3ec-c9fe-4a11-a2ec-d86208f7c0a2");
if (WpfNativeGuid.Verify(guid) != 42 || WpfNativeGuid.Verify(Guid.Empty) != 0)
    throw new Exception("WPF ToGUID produced incorrect Windows GUID bytes");
var method = typeof(WpfNativeGuid).GetMethod("ToGUID");
var parameter = method.GetParameters()[0];
if (!parameter.ParameterType.IsPointer || parameter.ParameterType.GetElementType() != typeof(Guid) ||
    !parameter.GetOptionalCustomModifiers().Any(t => t.Name == "IsImplicitlyDereferenced") ||
    !method.ReturnType.IsLayoutSequential || Marshal.SizeOf(method.ReturnType) != 16 ||
    method.ReturnType.StructLayoutAttribute.Pack != 4)
    throw new Exception("WPF native Guid reference/layout metadata is incorrect");
Console.WriteLine("original WPF ToGUID helper and Windows GUID bytes OK");
