using System;
using System.Linq;
using System.Reflection;
using System.Runtime.CompilerServices;

foreach (var type in typeof(ScalarBoxes).Assembly.GetTypes())
    foreach (var method in type.GetMethods(BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Static | BindingFlags.Instance | BindingFlags.DeclaredOnly).Cast<MethodBase>()
        .Concat(type.GetConstructors(BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Static | BindingFlags.Instance | BindingFlags.DeclaredOnly)))
        if (!method.IsAbstract && method.GetMethodBody() != null) RuntimeHelpers.PrepareMethod(method.MethodHandle);
foreach (var method in typeof(ScalarBoxes).Assembly.ManifestModule.GetMethods(BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Static))
    if (method.GetMethodBody() != null) RuntimeHelpers.PrepareMethod(method.MethodHandle);

object box = ScalarBoxes.Make(31);
foreach (var (actual, expected) in new (object, object)[] {
    (ScalarBoxes.Bool(true), true), (ScalarBoxes.Byte(255), (byte)255),
    (ScalarBoxes.SignedByte(-128), (sbyte)-128), (ScalarBoxes.Short(-32768), (short)-32768),
    (ScalarBoxes.UnsignedShort(65535), (ushort)65535), (ScalarBoxes.Unsigned(uint.MaxValue), uint.MaxValue),
    (ScalarBoxes.Long(long.MinValue), long.MinValue), (ScalarBoxes.UnsignedLong(ulong.MaxValue), ulong.MaxValue),
    (ScalarBoxes.Char('λ'), 'λ'), (ScalarBoxes.Float(1.5f), 1.5f), (ScalarBoxes.Double(2.5), 2.5) })
    if (actual.GetType() != expected.GetType() || !actual.Equals(expected)) throw new Exception("scalar type/payload");
if (ScalarBoxes.Name(42) != "42") throw new Exception("boxed Object virtual dispatch");
if (ScalarBoxes.HandleSize() != IntPtr.Size) throw new Exception("handle size differs from native reference size");
if (ScalarBoxes.Updates() != 799 || ScalarBoxes.Receivers != 3) throw new Exception("boxed update payload/evaluation count");
if (box is not int || (int)box != 31 || ScalarBoxes.Read(box) != 31) throw new Exception("scalar box identity/payload");
if (!ReferenceEquals(box, ScalarBoxes.Echo((ValueType)box))) throw new Exception("typed parameter/return copied the box");
if (!ReferenceEquals(box, ScalarBoxes.SafeObject((ValueType)box)) || ScalarBoxes.SafeValue((ValueType)box) != 31 ||
    !ScalarBoxes.Same((ValueType)box, (ValueType)box) || ScalarBoxes.Same((ValueType)box, (ValueType)ScalarBoxes.Make(31)))
    throw new Exception("safe casts or handle equality changed box identity");
object flag = ScalarBoxes.Enum(ScalarFlags.High);
if (flag is not ScalarFlags || (ScalarFlags)flag != ScalarFlags.High || !ReferenceEquals(flag, ScalarBoxes.EchoEnum((ValueType)flag))) throw new Exception("enum box identity/payload");
if (ScalarBoxes.Echo(null) != null || ScalarBoxes.Dynamic(null) || ScalarBoxes.Dynamic("wrong")) throw new Exception("null/dynamic cast");
try { ScalarBoxes.Read("wrong"); throw new Exception("incorrect box cast accepted"); } catch (InvalidCastException) {}
try { ScalarBoxes.Read(null); throw new Exception("null dereference accepted"); } catch (NullReferenceException) {}
if ((int)ScalarBoxes.Slot(37) != 37) throw new Exception("typed box field/property/constructor");
var slot = new ScalarBoxSlot((ValueType)box);
GC.Collect(); GC.WaitForPendingFinalizers();
if (!ReferenceEquals(box, slot.Value) || !ReferenceEquals(box, slot.Current)) throw new Exception("field/property lost identity or GC root");
foreach (var (name, element) in new[] { ("Echo", typeof(int)), ("EchoEnum", typeof(ScalarFlags)) }) {
    var method = typeof(ScalarBoxes).GetMethod(name);
    if (method.ReturnType != typeof(ValueType) || !method.ReturnParameter.GetOptionalCustomModifiers().Contains(element) ||
        !method.ReturnParameter.GetOptionalCustomModifiers().Any(t => t.FullName == "System.Runtime.CompilerServices.IsBoxed") ||
        method.GetParameters()[0].ParameterType != typeof(ValueType) || !method.GetParameters()[0].GetOptionalCustomModifiers().Contains(element))
        throw new Exception("typed box metadata");
}
if (typeof(ScalarBoxes).Assembly.GetTypes().Any(t => t.Name.Contains("__cli_handle"))) throw new Exception("reference facade leaked into runtime");
Console.WriteLine("scalar boxes: all managed bodies prepared; CLR identity, alias mutation, null/casts, metadata, field/property storage and GC PASS");
